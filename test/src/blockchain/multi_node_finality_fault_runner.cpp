/**
 * @file multi_node_finality_fault_runner.cpp
 * @brief Invocation-owned POSIX launcher for the fixed-port Phase 12 test.
 *
 * This executable is deliberately a test runner.  It owns only the session
 * created by its direct child and never discovers or signals a process outside
 * that session.
 */

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <string>
#include <vector>

namespace
{
    constexpr std::array<uint16_t, 4> kFixturePorts{ 54631, 54632, 54633, 54634 };
    constexpr auto                    kCleanupDeadline = std::chrono::seconds( 3 );
    constexpr auto                    kSocketGateDeadline = std::chrono::seconds( 2 );
    constexpr char                    kHandshakeMagic[] = "P12PG01";

    struct Handshake
    {
        char  magic[sizeof( kHandshakeMagic )]{};
        pid_t pid = -1;
        pid_t sid = -1;
        pid_t pgid = -1;
    };

    struct OwnedChild
    {
        pid_t child = -1;
        pid_t pgid = -1;
    };

    [[nodiscard]] int Fail( const std::string &reason )
    {
        std::fprintf( stderr, "P12_PROCESS_OWNERSHIP_PREFLIGHT=failed reason=%s\n", reason.c_str() );
        return 1;
    }

    [[nodiscard]] bool WriteAll( int fd, const void *data, size_t size )
    {
        const auto *bytes = static_cast<const char *>( data );
        while ( size > 0 )
        {
            const auto written = ::write( fd, bytes, size );
            if ( written > 0 )
            {
                bytes += written;
                size -= static_cast<size_t>( written );
                continue;
            }
            if ( written < 0 && errno == EINTR ) continue;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ReadExact( int fd, void *data, size_t size )
    {
        auto *bytes = static_cast<char *>( data );
        while ( size > 0 )
        {
            const auto read_size = ::read( fd, bytes, size );
            if ( read_size > 0 )
            {
                bytes += read_size;
                size -= static_cast<size_t>( read_size );
                continue;
            }
            if ( read_size < 0 && errno == EINTR ) continue;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool SetCloseOnExec( int fd )
    {
        const int flags = ::fcntl( fd, F_GETFD );
        return flags >= 0 && ::fcntl( fd, F_SETFD, flags | FD_CLOEXEC ) == 0;
    }

    [[nodiscard]] bool BindLoopback( int socket_fd, uint16_t port )
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons( port );
        address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        return ::bind( socket_fd, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) == 0;
    }

    [[nodiscard]] int MakeLoopbackSocket()
    {
        return ::socket( AF_INET, SOCK_STREAM, 0 );
    }

    [[nodiscard]] bool RuntimePreflight( std::string &reason )
    {
        if ( ::getpid() <= 1 || ::getpgrp() <= 1 || ::getsid( 0 ) <= 1 )
        {
            reason = "identity-unavailable";
            return false;
        }

        int pipe_fds[2]{};
        if ( ::pipe( pipe_fds ) != 0 )
        {
            reason = "pipe-unavailable";
            return false;
        }
        const bool pipe_ok = SetCloseOnExec( pipe_fds[0] ) && SetCloseOnExec( pipe_fds[1] );
        ::close( pipe_fds[0] );
        ::close( pipe_fds[1] );
        if ( !pipe_ok )
        {
            reason = "pipe-close-on-exec-unavailable";
            return false;
        }

        sigset_t pending;
        if ( ::sigpending( &pending ) != 0 )
        {
            reason = "signal-pending-unavailable";
            return false;
        }

        const int listener = MakeLoopbackSocket();
        if ( listener < 0 || !BindLoopback( listener, 0 ) || ::listen( listener, 1 ) != 0 )
        {
            if ( listener >= 0 ) ::close( listener );
            reason = "loopback-listener-unavailable";
            return false;
        }
        sockaddr_in listener_address{};
        socklen_t   listener_size = sizeof( listener_address );
        if ( ::getsockname( listener, reinterpret_cast<sockaddr *>( &listener_address ), &listener_size ) != 0 )
        {
            ::close( listener );
            reason = "loopback-getsockname-unavailable";
            return false;
        }
        const int connector = MakeLoopbackSocket();
        const bool connect_ok = connector >= 0 &&
                                ::connect( connector, reinterpret_cast<const sockaddr *>( &listener_address ), listener_size ) == 0;
        if ( connector >= 0 ) ::close( connector );
        const int accepted = connect_ok ? ::accept( listener, nullptr, nullptr ) : -1;
        if ( accepted >= 0 ) ::close( accepted );
        ::close( listener );
        if ( !connect_ok || accepted < 0 )
        {
            reason = "loopback-connect-unavailable";
            return false;
        }
        return true;
    }

    [[nodiscard]] int NormalizeExit( int status )
    {
        if ( WIFEXITED( status ) ) return WEXITSTATUS( status );
        if ( WIFSIGNALED( status ) ) return 128 + WTERMSIG( status );
        return 1;
    }

    [[nodiscard]] int PendingCancellationSignal( const sigset_t &signals )
    {
        sigset_t pending;
        if ( ::sigpending( &pending ) != 0 ) return -1;
        for ( const int signal : { SIGINT, SIGTERM, SIGHUP })
            if ( sigismember( &signals, signal ) == 1 && sigismember( &pending, signal ) == 1 ) return signal;
        return 0;
    }

    [[nodiscard]] bool ConsumeCancellationSignal( const sigset_t &signals, int expected_signal )
    {
        int consumed_signal = 0;
        return ::sigwait( &signals, &consumed_signal ) == 0 && consumed_signal == expected_signal;
    }

    [[nodiscard]] bool WaitForChild( pid_t child, int &status, const sigset_t &signals, int &cancellation_signal )
    {
        while ( true )
        {
            const auto waited = ::waitpid( child, &status, WNOHANG );
            if ( waited == child ) return true;
            if ( waited < 0 && errno != EINTR ) return false;
            cancellation_signal = PendingCancellationSignal( signals );
            if ( cancellation_signal != 0 ) return false;
            if ( ::poll( nullptr, 0, 50 ) < 0 && errno != EINTR ) return false;
        }
    }

    [[nodiscard]] bool VerifyCurrentOwnership( const OwnedChild &owned )
    {
        return owned.child > 1 && owned.pgid > 1 && owned.child == owned.pgid && ::getpgrp() != owned.pgid &&
               ::getsid( 0 ) != owned.pgid && ::getpgid( owned.child ) == owned.pgid &&
               ::getsid( owned.child ) == owned.pgid;
    }

    [[nodiscard]] bool TerminateOwnedGroup( const OwnedChild &owned, int &status )
    {
        if ( !VerifyCurrentOwnership( owned ) ) return false;
        if ( ::kill( -owned.pgid, SIGTERM ) != 0 ) return false;

        const auto deadline = std::chrono::steady_clock::now() + kCleanupDeadline;
        bool       reaped = false;
        while ( std::chrono::steady_clock::now() < deadline )
        {
            const auto waited = ::waitpid( owned.child, &status, WNOHANG );
            if ( waited == owned.child )
            {
                reaped = true;
                break;
            }
            if ( waited < 0 && errno != EINTR ) return false;
            if ( ::poll( nullptr, 0, 50 ) < 0 && errno != EINTR ) return false;
        }
        if ( !reaped )
        {
            if ( !VerifyCurrentOwnership( owned ) || ::kill( -owned.pgid, SIGKILL ) != 0 ) return false;
            while ( ::waitpid( owned.child, &status, 0 ) < 0 )
                if ( errno != EINTR ) return false;
        }

        if ( ::kill( -owned.pgid, 0 ) != -1 || errno != ESRCH ) return false;
        return true;
    }

    [[nodiscard]] bool WaitForHandshake( int fd, pid_t child, OwnedChild &owned )
    {
        Handshake handshake{};
        if ( !ReadExact( fd, &handshake, sizeof( handshake ) ) ) return false;
        if ( std::memcmp( handshake.magic, kHandshakeMagic, sizeof( handshake.magic ) ) != 0 ||
             handshake.pid != child || handshake.pid <= 1 || handshake.sid != child || handshake.pgid != child )
            return false;
        owned = { child, child };
        return VerifyCurrentOwnership( owned );
    }

    [[nodiscard]] bool ReserveFixturePorts( std::vector<int> &sockets, std::string &reason )
    {
        for ( const auto port : kFixturePorts )
        {
            const int socket_fd = MakeLoopbackSocket();
            if ( socket_fd < 0 || !BindLoopback( socket_fd, port ) )
            {
                if ( socket_fd >= 0 ) ::close( socket_fd );
                reason = "fixture-port-unavailable-" + std::to_string( port );
                for ( const int held : sockets ) ::close( held );
                sockets.clear();
                return false;
            }
            sockets.push_back( socket_fd );
        }
        return true;
    }

    [[nodiscard]] bool RebindFixturePorts( std::string &reason )
    {
        std::vector<int> rebound;
        if ( !ReserveFixturePorts( rebound, reason ) ) return false;
        for ( const int socket_fd : rebound ) ::close( socket_fd );
        return true;
    }

    [[nodiscard]] bool ConnectGate()
    {
        const auto deadline = std::chrono::steady_clock::now() + kSocketGateDeadline;
        while ( std::chrono::steady_clock::now() < deadline )
        {
            for ( const auto port : kFixturePorts )
            {
                const int socket_fd = MakeLoopbackSocket();
                if ( socket_fd < 0 ) continue;
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons( port );
                address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
                const bool connected = ::connect( socket_fd, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) == 0;
                ::close( socket_fd );
                if ( connected ) return true;
            }
            (void)::poll( nullptr, 0, 50 );
        }
        return false;
    }

    [[nodiscard]] bool BlockCancellationSignals( sigset_t &signals, sigset_t &original_mask )
    {
        return sigemptyset( &signals ) == 0 && sigaddset( &signals, SIGINT ) == 0 &&
               sigaddset( &signals, SIGTERM ) == 0 && sigaddset( &signals, SIGHUP ) == 0 &&
               ::sigprocmask( SIG_BLOCK, &signals, &original_mask ) == 0;
    }

    [[nodiscard]] bool StartOwnedChild( char *const child_argv[], const sigset_t &original_mask, OwnedChild &owned,
                                         int &readiness_fd )
    {
        int readiness_pipe[2]{ -1, -1 };
        if ( ::pipe( readiness_pipe ) != 0 || !SetCloseOnExec( readiness_pipe[0] ) || !SetCloseOnExec( readiness_pipe[1] ) )
        {
            if ( readiness_pipe[0] >= 0 ) ::close( readiness_pipe[0] );
            if ( readiness_pipe[1] >= 0 ) ::close( readiness_pipe[1] );
            return false;
        }

        const pid_t child = ::fork();
        if ( child == 0 )
        {
            ::close( readiness_pipe[0] );
            if ( ::sigprocmask( SIG_SETMASK, &original_mask, nullptr ) != 0 ) _exit( 126 );
            const pid_t session = ::setsid();
            const pid_t pid = ::getpid();
            Handshake handshake{};
            std::memcpy( handshake.magic, kHandshakeMagic, sizeof( handshake.magic ) );
            handshake.pid = pid;
            handshake.sid = ::getsid( 0 );
            handshake.pgid = ::getpgrp();
            if ( session != pid || handshake.sid != pid || handshake.pgid != pid ||
                 !WriteAll( readiness_pipe[1], &handshake, sizeof( handshake ) ) )
                _exit( 126 );
            ::close( readiness_pipe[1] );
            ::execv( child_argv[0], child_argv );
            _exit( 127 );
        }
        ::close( readiness_pipe[1] );
        if ( child < 0 )
        {
            ::close( readiness_pipe[0] );
            return false;
        }
        readiness_fd = readiness_pipe[0];
        if ( !WaitForHandshake( readiness_fd, child, owned ) )
        {
            ::close( readiness_fd );
            int ignored_status = 0;
            (void)::kill( child, SIGKILL );
            while ( ::waitpid( child, &ignored_status, 0 ) < 0 && errno == EINTR ) {}
            return false;
        }
        ::close( readiness_fd );
        readiness_fd = -1;
        return true;
    }

    [[nodiscard]] int RunNormal( int argc, char **argv )
    {
        if ( argc < 2 ) return Fail( "missing-test-executable" );
        sigset_t signals{};
        sigset_t original_mask{};
        if ( !BlockCancellationSignals( signals, original_mask ) ) return Fail( "signal-mask-unavailable" );

        OwnedChild owned{};
        int        readiness_fd = -1;
        if ( !StartOwnedChild( argv + 1, original_mask, owned, readiness_fd ) ) return Fail( "owned-child-handshake-failed" );
        int status = 0;
        int cancellation_signal = 0;
        const bool completed = WaitForChild( owned.child, status, signals, cancellation_signal );
        if ( completed )
        {
            if ( ::sigprocmask( SIG_SETMASK, &original_mask, nullptr ) != 0 ) return Fail( "signal-mask-restore-failed" );
            return NormalizeExit( status );
        }
        if ( cancellation_signal > 0 && TerminateOwnedGroup( owned, status ) &&
             ConsumeCancellationSignal( signals, cancellation_signal ) &&
             ::sigprocmask( SIG_SETMASK, &original_mask, nullptr ) == 0 )
            return 128 + cancellation_signal;
        return Fail( cancellation_signal < 0 ? "signal-wait-failed" : "normal-child-wait-failed" );
    }

    [[nodiscard]] int RunControlledCancellation( const char *test_executable )
    {
        std::vector<int> reservations;
        std::string      reason;
        if ( !ReserveFixturePorts( reservations, reason ) ) return Fail( reason );
        for ( const int socket_fd : reservations ) ::close( socket_fd );

        const std::string filter = "--gtest_filter=PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch";
        std::array<char *, 3> child_argv{ const_cast<char *>( test_executable ), const_cast<char *>( filter.c_str() ), nullptr };
        sigset_t signals{};
        sigset_t original_mask{};
        if ( !BlockCancellationSignals( signals, original_mask ) ) return Fail( "signal-mask-unavailable" );
        OwnedChild owned{};
        int        readiness_fd = -1;
        if ( !StartOwnedChild( child_argv.data(), original_mask, owned, readiness_fd ) ) return Fail( "owned-child-handshake-failed" );

        const bool socket_gate_connected = ConnectGate();
        int        status = 0;
        const bool terminated = TerminateOwnedGroup( owned, status );
        const bool rebound = terminated && RebindFixturePorts( reason );
        const bool restored = ::sigprocmask( SIG_SETMASK, &original_mask, nullptr ) == 0;
        if ( !terminated ) return Fail( "owned-group-termination-or-reap-failed" );
        if ( !rebound ) return Fail( reason );
        if ( !restored ) return Fail( "signal-mask-restore-failed" );
        std::fprintf( stdout,
                      "P12_PROCESS_OWNERSHIP=passed pid=%d pgid=%d socket_gate=%s child_exit=%d ports=rebound-all-four\n",
                      static_cast<int>( owned.child ), static_cast<int>( owned.pgid ),
                      socket_gate_connected ? "connected" : "not-connected", NormalizeExit( status ) );
        return 0;
    }
}

int main( int argc, char **argv )
{
    std::string preflight_reason;
    if ( !RuntimePreflight( preflight_reason ) ) return Fail( preflight_reason );
    if ( argc == 3 && std::strcmp( argv[1], "--verify-controlled-cancellation" ) == 0 )
        return RunControlledCancellation( argv[2] );
    return RunNormal( argc, argv );
}

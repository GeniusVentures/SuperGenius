/** Invocation-owned POSIX launcher for the Phase 12 fixed-port test. */
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
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>

namespace
{
    constexpr std::array<uint16_t, 4> kPorts{ 54631, 54632, 54633, 54634 };
    constexpr auto kCleanupDeadline = std::chrono::seconds( 3 );
    constexpr auto kGateDeadline = std::chrono::seconds( 2 );
    constexpr auto kReportDeadline = std::chrono::seconds( 5 );
    constexpr char kMagic[] = "P12PG01";
    constexpr char kReady = 'R', kSuccess = 'S', kFailure = 'F';
    struct Handshake { char magic[sizeof( kMagic )]{}; pid_t pid = -1, sid = -1, pgid = -1; };
    struct OwnedChild { pid_t child = -1, pgid = -1; };

    [[nodiscard]] int Fail( const std::string &reason )
    {
        std::fprintf( stderr, "P12_PROCESS_OWNERSHIP_PREFLIGHT=failed reason=%s\n", reason.c_str() );
        return 1;
    }
    [[nodiscard]] bool WriteAll( int fd, const void *data, size_t size )
    {
        const auto *bytes = static_cast<const char *>( data );
        while ( size ) { const auto n = ::write( fd, bytes, size ); if ( n > 0 ) { bytes += n; size -= n; } else if ( n < 0 && errno == EINTR ) {} else return false; }
        return true;
    }
    [[nodiscard]] bool ReadAll( int fd, void *data, size_t size )
    {
        auto *bytes = static_cast<char *>( data );
        while ( size ) { const auto n = ::read( fd, bytes, size ); if ( n > 0 ) { bytes += n; size -= n; } else if ( n < 0 && errno == EINTR ) {} else return false; }
        return true;
    }
    [[nodiscard]] bool CloseOnExec( int fd )
    {
        const int flags = ::fcntl( fd, F_GETFD );
        return flags >= 0 && ::fcntl( fd, F_SETFD, flags | FD_CLOEXEC ) == 0;
    }
    [[nodiscard]] int Socket() { return ::socket( AF_INET, SOCK_STREAM, 0 ); }
    [[nodiscard]] bool RuntimePreflight( std::string &reason )
    {
        if ( ::getpid() <= 1 || ::getpgrp() <= 1 || ::getsid( 0 ) <= 1 ) { reason = "identity-unavailable"; return false; }
        int pipefd[2]{};
        if ( ::pipe( pipefd ) != 0 ) { reason = "pipe-unavailable"; return false; }
        const bool pipe_ok = CloseOnExec( pipefd[0] ) && CloseOnExec( pipefd[1] ); ::close( pipefd[0] ); ::close( pipefd[1] );
        if ( !pipe_ok ) { reason = "pipe-close-on-exec-unavailable"; return false; }
        sigset_t pending; if ( ::sigpending( &pending ) != 0 ) { reason = "signal-pending-unavailable"; return false; }
        const int fd = Socket();
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        if ( fd < 0 || ::bind( fd, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) != 0 )
        { if ( fd >= 0 ) ::close( fd ); reason = "loopback-bind-unavailable"; return false; }
        ::close( fd ); return true;
    }
    [[nodiscard]] bool Bind( int fd, uint16_t port )
    {
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons( port ); address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        return ::bind( fd, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) == 0;
    }
    [[nodiscard]] bool ReservePorts( std::vector<int> &held, std::string &reason )
    {
        for ( const auto port : kPorts )
        {
            const int fd = Socket();
            if ( fd < 0 || !Bind( fd, port ) )
            {
                if ( fd >= 0 ) ::close( fd );
                for ( const int existing : held ) ::close( existing );
                held.clear(); reason = "fixture-port-unavailable-" + std::to_string( port ); return false;
            }
            held.push_back( fd );
        }
        return true;
    }
    [[nodiscard]] bool RebindPorts( std::string &reason )
    {
        std::vector<int> held; const bool ok = ReservePorts( held, reason ); for ( const int fd : held ) ::close( fd ); return ok;
    }
    [[nodiscard]] bool ConnectGate()
    {
        const auto deadline = std::chrono::steady_clock::now() + kGateDeadline;
        while ( std::chrono::steady_clock::now() < deadline )
        {
            for ( const auto port : kPorts )
            {
                const int fd = Socket(); if ( fd < 0 ) continue;
                sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons( port ); address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
                const bool connected = ::connect( fd, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) == 0;
                ::close( fd ); if ( connected ) return true;
            }
            (void)::poll( nullptr, 0, 50 );
        }
        return false;
    }
    [[nodiscard]] int NormalizedExit( int status )
    {
        return WIFEXITED( status ) ? WEXITSTATUS( status ) : WIFSIGNALED( status ) ? 128 + WTERMSIG( status ) : 1;
    }
    [[nodiscard]] bool Reap( pid_t child, int &status )
    {
        while ( ::waitpid( child, &status, 0 ) < 0 ) if ( errno != EINTR ) return false;
        return true;
    }
    [[nodiscard]] bool OwnedNow( const OwnedChild &owned )
    {
        return owned.child > 1 && owned.pgid > 1 && owned.child == owned.pgid && ::getpgrp() != owned.pgid && ::getsid( 0 ) != owned.pgid &&
               ::getpgid( owned.child ) == owned.pgid && ::getsid( owned.child ) == owned.pgid;
    }
    // This deliberately does not discover a group by PID.  The caller must
    // already have either checked OwnedNow() or received this exact identity
    // through the private child handshake.
    [[nodiscard]] bool HandshakenGroup( const OwnedChild &owned )
    {
        return owned.child > 1 && owned.pgid > 1 && owned.child == owned.pgid && ::getpgrp() != owned.pgid && ::getsid( 0 ) != owned.pgid;
    }
    [[nodiscard]] bool GroupGone( const OwnedChild &owned )
    {
        return ::kill( -owned.pgid, 0 ) == -1 && errno == ESRCH;
    }
    [[nodiscard]] bool WaitForGroupGone( const OwnedChild &owned, int &status, bool &reaped )
    {
        const auto deadline = std::chrono::steady_clock::now() + kCleanupDeadline;
        while ( std::chrono::steady_clock::now() < deadline )
        {
            if ( !reaped )
            {
                const auto waited = ::waitpid( owned.child, &status, WNOHANG );
                if ( waited == owned.child ) reaped = true;
                else if ( waited < 0 && errno != EINTR ) return false;
            }
            if ( GroupGone( owned ) ) return reaped || Reap( owned.child, status );
            if ( ::poll( nullptr, 0, 50 ) < 0 && errno != EINTR ) return false;
        }
        return false;
    }
    [[nodiscard]] bool TerminateHandshakenGroup( const OwnedChild &owned, int &status )
    {
        if ( !HandshakenGroup( owned ) ) return false;
        const auto terminated = ::kill( -owned.pgid, SIGTERM );
        if ( terminated != 0 && errno != ESRCH ) return false;

        bool reaped = false;
        if ( WaitForGroupGone( owned, status, reaped ) ) return true;

        // The direct child may have exited after SIGTERM while one of its
        // descendants still owns the handshaken group.  That group remains
        // the only target this invocation is permitted to signal.
        if ( !HandshakenGroup( owned ) || ::kill( -owned.pgid, SIGKILL ) != 0 ) return false;
        return WaitForGroupGone( owned, status, reaped );
    }
    [[nodiscard]] bool TerminateOwned( const OwnedChild &owned, int &status )
    {
        return OwnedNow( owned ) && TerminateHandshakenGroup( owned, status );
    }
    [[nodiscard]] bool BlockSignals( sigset_t &signals, sigset_t &original )
    {
        return sigemptyset( &signals ) == 0 && sigaddset( &signals, SIGINT ) == 0 && sigaddset( &signals, SIGTERM ) == 0 &&
               sigaddset( &signals, SIGHUP ) == 0 && ::sigprocmask( SIG_BLOCK, &signals, &original ) == 0;
    }
    [[nodiscard]] int PendingSignal( const sigset_t &signals )
    {
        sigset_t pending; if ( ::sigpending( &pending ) != 0 ) return -1;
        for ( const int signal : { SIGINT, SIGTERM, SIGHUP }) if ( sigismember( &signals, signal ) == 1 && sigismember( &pending, signal ) == 1 ) return signal;
        return 0;
    }
    [[nodiscard]] bool StartOwned( char *const argv[], const sigset_t &original, OwnedChild &owned )
    {
        int pipefd[2]{ -1, -1 };
        if ( ::pipe( pipefd ) != 0 || !CloseOnExec( pipefd[0] ) || !CloseOnExec( pipefd[1] ) ) { if ( pipefd[0] >= 0 ) ::close( pipefd[0] ); if ( pipefd[1] >= 0 ) ::close( pipefd[1] ); return false; }
        const pid_t child = ::fork();
        if ( child == 0 )
        {
            ::close( pipefd[0] ); if ( ::sigprocmask( SIG_SETMASK, &original, nullptr ) != 0 ) _exit( 126 );
            const pid_t session = ::setsid(); const pid_t pid = ::getpid();
            Handshake handshake{}; std::memcpy( handshake.magic, kMagic, sizeof( handshake.magic ) ); handshake.pid = pid; handshake.sid = ::getsid( 0 ); handshake.pgid = ::getpgrp();
            if ( session != pid || handshake.sid != pid || handshake.pgid != pid || !WriteAll( pipefd[1], &handshake, sizeof( handshake ) ) ) _exit( 126 );
            ::close( pipefd[1] ); ::execv( argv[0], argv ); _exit( 127 );
        }
        ::close( pipefd[1] );
        if ( child < 0 ) { ::close( pipefd[0] ); return false; }
        Handshake handshake{}; const bool valid = ReadAll( pipefd[0], &handshake, sizeof( handshake ) ) &&
            std::memcmp( handshake.magic, kMagic, sizeof( handshake.magic ) ) == 0 && handshake.pid == child && handshake.pid > 1 && handshake.sid == child && handshake.pgid == child;
        ::close( pipefd[0] ); owned = { child, child };
        if ( valid && OwnedNow( owned ) ) return true;
        int ignored = 0;
        // A valid private handshake is sufficient to retain this exact
        // session identity even if the direct leader exits before our second
        // liveness check.  Never fall back to a positive-PID kill here.
        if ( valid ) (void)TerminateHandshakenGroup( owned, ignored );
        else { (void)::kill( child, SIGKILL ); (void)Reap( child, ignored ); }
        return false;
    }
    [[nodiscard]] bool LivenessClosed( int fd, bool &closed )
    {
        pollfd descriptor{ fd, POLLIN | POLLHUP, 0 }; int ready = 0;
        do { ready = ::poll( &descriptor, 1, 50 ); } while ( ready < 0 && errno == EINTR );
        if ( ready < 0 ) return false; if ( ready == 0 ) return true;
        char byte = 0; const auto read_size = ::read( fd, &byte, 1 );
        if ( read_size == 0 ) closed = true;
        return read_size >= 0 || errno == EINTR || errno == EAGAIN;
    }
    void Report( int fd, char value ) { if ( fd >= 0 ) (void)WriteAll( fd, &value, 1 ); }
    [[nodiscard]] int ControlledCleanupFailure( const OwnedChild &owned, int &status, int report_fd,
                                                const std::string &failure )
    {
        std::string reason;
        const bool terminated = TerminateOwned( owned, status );
        const bool rebound = terminated && RebindPorts( reason );
        Report( report_fd, kFailure );
        return Fail( !terminated ? "owned-group-termination-or-reap-failed" : !rebound ? reason : failure );
    }
    [[nodiscard]] int Supervisor( int liveness_fd, const sigset_t &original, char *const child_argv[], bool controlled, int report_fd )
    {
        if ( ::sigprocmask( SIG_SETMASK, &original, nullptr ) != 0 || !CloseOnExec( liveness_fd ) ||
             ( report_fd >= 0 && !CloseOnExec( report_fd ) ) ) { Report( report_fd, kFailure ); return Fail( "supervisor-signal-mask-restore-failed" ); }
        std::vector<int> reservations; std::string reason;
        if ( controlled && !ReservePorts( reservations, reason ) ) { Report( report_fd, kFailure ); return Fail( reason ); }
        for ( const int fd : reservations ) ::close( fd );
        OwnedChild owned{};
        if ( !StartOwned( child_argv, original, owned ) ) { Report( report_fd, kFailure ); return Fail( "owned-child-handshake-failed" ); }
        int status = 0;
        if ( controlled && !ConnectGate() )
            return ControlledCleanupFailure( owned, status, report_fd, "socket-gate-not-connected" );
        if ( controlled && report_fd >= 0 && !WriteAll( report_fd, &kReady, 1 ) )
            return ControlledCleanupFailure( owned, status, report_fd, "controlled-report-ready-failed" );
        bool launcher_dead = false;
        while ( true )
        {
            const auto waited = ::waitpid( owned.child, &status, WNOHANG );
            if ( waited == owned.child ) { if ( controlled ) { Report( report_fd, kFailure ); return Fail( "controlled-child-exited-before-launcher-cancellation" ); } return NormalizedExit( status ); }
            if ( waited < 0 && errno != EINTR )
                return controlled ? ControlledCleanupFailure( owned, status, report_fd, "supervisor-child-wait-failed" )
                                  : Fail( TerminateOwned( owned, status ) ? "supervisor-child-wait-failed" : "owned-group-termination-or-reap-failed" );
            if ( !LivenessClosed( liveness_fd, launcher_dead ) )
                return controlled ? ControlledCleanupFailure( owned, status, report_fd, "supervisor-liveness-wait-failed" )
                                  : Fail( TerminateOwned( owned, status ) ? "supervisor-liveness-wait-failed" : "owned-group-termination-or-reap-failed" );
            if ( launcher_dead )
            {
                const bool terminated = TerminateOwned( owned, status ); const bool rebound = !controlled || ( terminated && RebindPorts( reason ) ); const bool success = terminated && rebound;
                Report( report_fd, success ? kSuccess : kFailure );
                if ( !success ) return Fail( !terminated ? "owned-group-termination-or-reap-failed" : reason );
                std::fprintf( stdout, "P12_PROCESS_OWNERSHIP=cleaned pgid=%d ports=%s\n", static_cast<int>( owned.pgid ), controlled ? "rebound-all-four" : "not-applicable" );
                return controlled ? 0 : NormalizedExit( status );
            }
        }
    }
    [[nodiscard]] int Launcher( char *const child_argv[], bool controlled, int report_fd )
    {
        sigset_t signals{}, original{}; if ( !BlockSignals( signals, original ) ) return Fail( "signal-mask-unavailable" );
        int liveness[2]{ -1, -1 };
        if ( ::pipe( liveness ) != 0 ) { (void)::sigprocmask( SIG_SETMASK, &original, nullptr ); return Fail( "liveness-pipe-unavailable" ); }
        const pid_t supervisor = ::fork();
        if ( supervisor == 0 ) { ::close( liveness[1] ); const int result = Supervisor( liveness[0], original, child_argv, controlled, report_fd ); ::close( liveness[0] ); if ( report_fd >= 0 ) ::close( report_fd ); _exit( result ); }
        ::close( liveness[0] );
        if ( supervisor < 0 ) { ::close( liveness[1] ); (void)::sigprocmask( SIG_SETMASK, &original, nullptr ); return Fail( "supervisor-fork-failed" ); }
        int status = 0, cancellation = 0; bool complete = false;
        while ( true ) { const auto waited = ::waitpid( supervisor, &status, WNOHANG ); if ( waited == supervisor ) { complete = true; break; } if ( waited < 0 && errno != EINTR ) break; cancellation = PendingSignal( signals ); if ( cancellation != 0 || (::poll( nullptr, 0, 50 ) < 0 && errno != EINTR ) ) break; }
        ::close( liveness[1] ); // EOF makes the independently alive supervisor clean the verified test group.
        const bool reaped = complete || Reap( supervisor, status ); const bool restored = ::sigprocmask( SIG_SETMASK, &original, nullptr ) == 0;
        if ( !reaped ) return Fail( "supervisor-wait-failed" ); if ( !restored ) return Fail( "signal-mask-restore-failed" );
        if ( complete ) return NormalizedExit( status );
        if ( cancellation > 0 ) { int consumed = 0; if ( ::sigwait( &signals, &consumed ) == 0 && consumed == cancellation ) return 128 + cancellation; }
        return Fail( cancellation < 0 ? "signal-wait-failed" : "normal-supervisor-wait-failed" );
    }
    [[nodiscard]] bool ReadReport( int fd, char &report )
    {
        pollfd descriptor{ fd, POLLIN | POLLHUP, 0 }; const int timeout = static_cast<int>( std::chrono::duration_cast<std::chrono::milliseconds>( kReportDeadline ).count() );
        int ready = 0; do { ready = ::poll( &descriptor, 1, timeout ); } while ( ready < 0 && errno == EINTR );
        return ready > 0 && ReadAll( fd, &report, 1 );
    }
    [[nodiscard]] int ControlledCancellation( const char *runner, const char *test_executable )
    {
        int report_pipe[2]{ -1, -1 };
        if ( ::pipe( report_pipe ) != 0 || !CloseOnExec( report_pipe[0] ) ) { if ( report_pipe[0] >= 0 ) ::close( report_pipe[0] ); if ( report_pipe[1] >= 0 ) ::close( report_pipe[1] ); return Fail( "controlled-report-pipe-unavailable" ); }
        const pid_t launcher = ::fork();
        if ( launcher == 0 )
        {
            ::close( report_pipe[0] ); const std::string fd = std::to_string( report_pipe[1] );
            std::array<char *, 5> argv{ const_cast<char *>( runner ), const_cast<char *>( "--launcher-controlled" ), const_cast<char *>( test_executable ), const_cast<char *>( fd.c_str() ), nullptr };
            ::execv( runner, argv.data() ); _exit( 127 );
        }
        ::close( report_pipe[1]); if ( launcher < 0 ) { ::close( report_pipe[0] ); return Fail( "controlled-launcher-fork-failed" ); }
        char report = 0;
        if ( !ReadReport( report_pipe[0], report ) || report != kReady )
        {
            if ( ::kill( launcher, SIGKILL ) != 0 && errno != ESRCH ) { ::close( report_pipe[0] ); return Fail( "controlled-launcher-kill-failed" ); }
            int ignored = 0;
            if ( !Reap( launcher, ignored ) ) { ::close( report_pipe[0] ); return Fail( "controlled-launcher-not-reaped" ); }
            // A missing readiness report is never a pass: wait for the owned
            // supervisor's cleanup report and independently prove neutral port reuse.
            (void)ReadReport( report_pipe[0], report );
            ::close( report_pipe[0] );
            std::string reason;
            if ( !RebindPorts( reason ) ) return Fail( reason );
            return Fail( "controlled-fixture-listener-not-observed" );
        }
        if ( ::kill( launcher, SIGKILL ) != 0 ) { ::close( report_pipe[0] ); return Fail( "controlled-launcher-kill-failed" ); }
        int launcher_status = 0; const bool killed = Reap( launcher, launcher_status ) && WIFSIGNALED( launcher_status ) && WTERMSIG( launcher_status ) == SIGKILL;
        const bool cleaned = killed && ReadReport( report_pipe[0], report ) && report == kSuccess; ::close( report_pipe[0] );
        std::string reason; if ( !cleaned ) return Fail( "controlled-supervisor-cleanup-failed" ); if ( !RebindPorts( reason ) ) return Fail( reason );
        std::fprintf( stdout, "P12_PROCESS_OWNERSHIP=passed launcher=%d launcher_signal=SIGKILL ports=rebound-all-four\n", static_cast<int>( launcher ) ); return 0;
    }
}

int main( int argc, char **argv )
{
    std::string preflight_reason;
    if ( !RuntimePreflight( preflight_reason ) ) return Fail( preflight_reason );
    if ( argc == 3 && std::strcmp( argv[1], "--verify-controlled-cancellation" ) == 0 ) return ControlledCancellation( argv[0], argv[2] );
    if ( argc == 4 && std::strcmp( argv[1], "--launcher-controlled" ) == 0 )
    {
        const char *filter = "--gtest_filter=PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch";
        char *const child_argv[]{ argv[2], const_cast<char *>( filter ), nullptr };
        return Launcher( child_argv, true, std::atoi( argv[3] ) );
    }
    if ( argc < 2 ) return Fail( "missing-test-executable" );
    return Launcher( argv + 1, false, -1 );
}

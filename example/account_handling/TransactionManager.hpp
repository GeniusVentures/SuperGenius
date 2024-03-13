/**
 * @file       TransactionManager.hpp
 * @brief      
 * @date       2024-03-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSACTION_MANAGER_HPP_
#define _TRANSACTION_MANAGER_HPP_
#include <memory>
#include <boost/asio.hpp>
#include <deque>
#include <crdt/globaldb/globaldb.hpp>
#include "account/IGeniusTransactions.hpp"

namespace sgns
{
    class TransactionManager
    {
    public:
        TransactionManager( std::shared_ptr<crdt::GlobalDB> db, std::shared_ptr<boost::asio::io_context> ctx, std::string address ) :
            db_m( std::move( db ) ),           //
            ctx_m( std::move( ctx ) ),         //
            address_m( std::move( address ) ), //
            timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) )

        {
        }
        void Start()
        {
            auto task = std::make_shared<std::function<void()>>();

            *task = [this, task]()
            {
                this->Update();
                this->timer_m->expires_after( boost::asio::chrono::seconds( 1 ) );

                this->timer_m->async_wait( [this, task]( const boost::system::error_code & ) { this->ctx_m->post( *task ); } );
            };
            ctx_m->post( *task );
        }

        void Update()
        {
            std::unique_lock<std::mutex> lock( mutex_m );
            if ( !out_transactions.empty() )
            {
                auto elem = out_transactions.front();
                out_transactions.pop_front();
                //TODO - Write on the blockchain/transactions/proofs CRDT
            }
            lock.unlock(); // Manual unlock, no need to wait to run out of scope

            auto retval = db_m->Get( { "MYKEY" } );
            if ( retval )
            {
                std::cout << "*** received something ***" << retval.value() << std::endl;
            }
        }

        void SendTransaction()
        {
        }

        void EmplaceTransaction( std::shared_ptr<IGeniusTransactions> element )
        {
            std::lock_guard<std::mutex> lock( mutex_m );
            out_transactions.emplace_back( std::move( element ) );
        }

        ~TransactionManager() = default;

    private:
        std::shared_ptr<crdt::GlobalDB>                  db_m;
        std::shared_ptr<boost::asio::io_context>         ctx_m;
        std::string                                      address_m;
        std::uint64_t                                    latest_balance;
        std::deque<std::shared_ptr<IGeniusTransactions>> out_transactions;
        std::shared_ptr<boost::asio::steady_timer>       timer_m;
        mutable std::mutex                               mutex_m;
    };
}

#endif

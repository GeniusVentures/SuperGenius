/**
 * @file       base_crdt_test.hpp
 * @brief
 * @date       2024-04-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _BASE_CRDT_TEST_HPP_
#define _BASE_CRDT_TEST_HPP_

#include "testutil/storage/base_fs_test.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <string>

#include "crdt/globaldb/globaldb.hpp"

namespace soralog
{
    class LoggingSystem;
}

namespace test
{
    class CRDTFixture : public FSFixture
    {
    public:
        explicit CRDTFixture( fs::path path );

        ~CRDTFixture() override;

        static void SetUpTestSuite();
        static void TearDownTestSuite();

        std::shared_ptr<boost::asio::io_context>         io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubs_;

        std::shared_ptr<sgns::crdt::GlobalDB>            db_;
        std::string                                      keypair_path_;
        std::string                                      db_path_;
        static std::shared_ptr<::soralog::LoggingSystem> logging_system_;
    };

}

#endif

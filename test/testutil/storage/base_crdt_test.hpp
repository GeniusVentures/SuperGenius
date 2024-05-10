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

namespace test
{
    struct CRDTFixture : public FSFixture
    {
        CRDTFixture( fs::path path );

        ~CRDTFixture() override;

        static void SetUpTestSuite();

        static const std::string basePath;

        static bool                                             initializedDb;
        static std::shared_ptr<boost::asio::io_context>         io_;
        static std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubs_;
        static std::shared_ptr<sgns::crdt::GlobalDB>            db_;
    };

}

#endif

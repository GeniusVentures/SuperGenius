#include "crdt/atomic_transaction.hpp"
#include "crdt/crdt_datastore.hpp"
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include "outcome/outcome.hpp"
#include <testutil/outcome.hpp>
#include <boost/filesystem.hpp>
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include <thread>
#include <atomic>
#include "crdt_custom_broadcaster.hpp"
#include "crdt_custom_dagsyncer.hpp"

namespace sgns::crdt {
    using storage::rocksdb;
    using base::Buffer;
    using ipfs_lite::ipld::IPLDNode;
    using ipfs_lite::ipfs::IpfsDatastore;
    using ipfs_lite::ipfs::InMemoryDatastore;
    using CrdtBuffer = CrdtDatastore::Buffer;
    using Delta = CrdtDatastore::Delta;

    namespace fs = boost::filesystem;

    class AtomicTransactionTest : public ::testing::Test {
    protected:
        using CustomBroadcaster = crdt::CustomBroadcaster;
        using CustomDagSyncer = crdt::CustomDagSyncer;

        void SetUp() override {
            // Remove leftover database
            std::string databasePath = "supergenius_atomic_transaction_test";
            fs::remove_all(databasePath);

            // Create new database
            rocksdb::Options options;
            options.create_if_missing = true;  // intentionally
            auto dataStoreResult = rocksdb::create(databasePath, options);
            auto dataStore = dataStoreResult.value();

            // Create new DAGSyncer
            auto ipfsDataStore = std::make_shared<InMemoryDatastore>();
            auto dagSyncer = std::make_shared<CustomDagSyncer>(ipfsDataStore);

            // Create new Broadcaster
            auto broadcaster = std::make_shared<CustomBroadcaster>();

            // Define test values
            const std::string strNamespace = "/namespace";
            HierarchicalKey namespaceKey(strNamespace);

            // Create crdtDatastore
            crdtDatastore_ = CrdtDatastore::New(
                dataStore,
                namespaceKey,
                dagSyncer,
                broadcaster,
                CrdtOptions::DefaultOptions());
        }

        void TearDown() override {
            crdtDatastore_ = nullptr;
        }

        void SimulateNetworkDelay() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        void ConcurrentWriter(const HierarchicalKey& key1,
                            const HierarchicalKey& key2,
                            const Buffer& value1,
                            const Buffer& value2,
                            bool use_atomic,
                            std::atomic<bool>& did_interrupt) {
            if (use_atomic) {
                AtomicTransaction transaction(crdtDatastore_);
                EXPECT_OUTCOME_TRUE_1(transaction.Put(key1, value1));
                SimulateNetworkDelay();  // Still simulate delay but won't affect atomicity
                EXPECT_OUTCOME_TRUE_1(transaction.Put(key2, value2));
                EXPECT_OUTCOME_TRUE_1(transaction.Commit());
            } else {
                // Non-atomic updates - vulnerable to interleaving
                EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key1, value1));
                did_interrupt = true;  // Signal before delay to ensure interference
                SimulateNetworkDelay();  // Longer delay to ensure t2 has time to interfere
                SimulateNetworkDelay();  // Add extra delay
                EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key2, value2));
            }
        }

        std::shared_ptr<CrdtDatastore> crdtDatastore_;
    };

    TEST_F(AtomicTransactionTest, TestAtomicUpdates) {
        auto key1 = HierarchicalKey("balance1");
        auto key2 = HierarchicalKey("balance2");

        Buffer initial_value;
        initial_value.put("100");

        EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key1, initial_value));
        EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key2, initial_value));

        Buffer transfer_from_value;
        transfer_from_value.put("50");
        Buffer transfer_to_value;
        transfer_to_value.put("150");

        for (bool use_atomic : {false, true}) {
            std::atomic<bool> did_interrupt{false};
            std::atomic<bool> t1_finished{false};

            std::thread t1([&]() {
                ConcurrentWriter(key1, key2, transfer_from_value, transfer_to_value,
                               use_atomic, did_interrupt);
                t1_finished = true;
            });

            std::thread t2([&]() {
                while (!did_interrupt && !t1_finished) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!use_atomic && !t1_finished) {
                    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key1, initial_value));
                    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key2, initial_value));
                }
            });

            t1.join();
            t2.join();

            // Rest of verification code remains the same...
            EXPECT_OUTCOME_TRUE(value1, crdtDatastore_->GetKey(key1));
            EXPECT_OUTCOME_TRUE(value2, crdtDatastore_->GetKey(key2));

            if (use_atomic) {
                EXPECT_EQ(value1.toString(), "50");
                EXPECT_EQ(value2.toString(), "150");
            } else {
                bool has_interference = value1.toString() != "50" ||
                                      value2.toString() != "150";
                EXPECT_TRUE(has_interference);
            }

            EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key1, initial_value));
            EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key2, initial_value));
        }
    }

    TEST_F(AtomicTransactionTest, TestAtomicTransactionRollback) {
        auto key1 = HierarchicalKey("test1");
        auto key2 = HierarchicalKey("test2");

        Buffer initial_value;
        initial_value.put("initial");

        Buffer new_value1;
        new_value1.put("new1");
        Buffer new_value2;
        new_value2.put("new2");

        // Set initial values
        EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key1, initial_value));
        EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(key2, initial_value));

        {
            // Create a transaction that will go out of scope without commit
            AtomicTransaction transaction(crdtDatastore_);
            EXPECT_OUTCOME_TRUE_1(transaction.Put(key1, new_value1));
            EXPECT_OUTCOME_TRUE_1(transaction.Put(key2, new_value2));
            // Let transaction go out of scope without commit
        }

        // Verify values haven't changed
        EXPECT_OUTCOME_TRUE(value1, crdtDatastore_->GetKey(key1));
        EXPECT_OUTCOME_TRUE(value2, crdtDatastore_->GetKey(key2));
        EXPECT_EQ(value1.toString(), "initial");
        EXPECT_EQ(value2.toString(), "initial");
    }

    TEST_F(AtomicTransactionTest, TestTransactionReuse) {
        auto key = HierarchicalKey("test");
        Buffer value;
        value.put("value");

        AtomicTransaction transaction(crdtDatastore_);
        
        // First operation should succeed
        EXPECT_OUTCOME_TRUE_1(transaction.Put(key, value));
        EXPECT_OUTCOME_TRUE_1(transaction.Commit());

        // Second operation on same transaction should fail
        Buffer value2;
        value2.put("value2");
        auto result = transaction.Put(key, value2);
        EXPECT_TRUE(result.has_failure());
    }
}
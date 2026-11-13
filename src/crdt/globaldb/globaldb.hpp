#ifndef SUPERGENIUS_CRDT_GLOBALDB_HPP
#define SUPERGENIUS_CRDT_GLOBALDB_HPP

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/filesystem/path.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/graphsync_impl.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <libp2p/protocol/autonat/autonat.hpp>
#include <libp2p/protocol/holepunch/holepunch_client.hpp>
#include <libp2p/protocol/holepunch/holepunch_server.hpp>
#include <libp2p/protocol/identify/identify.hpp>

#include "crdt/atomic_transaction.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "outcome/outcome.hpp"
#include "pubsub_broadcaster_ext.hpp"

namespace sgns::crdt
{
    // Gated drainer service (v1.1 Phase 8); defined in ColdStorageManager.hpp,
    // reachable through the ColdStorageStack bridges only.
    class ColdStorageManager;

    // Gated cold-read chain service (v1.1 Phase 9); defined in
    // ColdReadService.hpp, reachable through the ColdStorageStack
    // bundle only. The DAGSyncer holds it through the UNGATED
    // ColdReadFallback interface (crdt/cold_read_fallback.hpp).
    class ColdReadService;

    // D-08 status triple for the cold-storage drainer; defined in
    // ColdStorageStack.hpp (always compiled, zero library types).
    enum class ColdStorageWatermark;
    struct ColdStorageStatus;
    enum class ColdUploadState;

    class GraphsyncDAGSyncer;

    class GlobalDB
    {
    public:
        struct BackupOptions
        {
            bool     enabled{ false };
            uint32_t interval_minutes{ 15 };
            uint32_t keep_count{ 12 };
            bool     auto_restore_on_repair_failure{ true };
        };

        /**
         * @brief      Construction-time configuration for the cold-storage
         *             (sgns-blockstore) integration, v1.1 Phase 7 (INTG-02).
         *
         * Parsed strictly from the nested "blockstore" object of
         * crdt_config.json (D-01/D-02): endpoint / bucket / access_key /
         * secret_key are the required set; prefix / path_style / tls are
         * optional. Credentials may resolve from the
         * SGNS_BLOCKSTORE_S3_ACCESS_KEY / SGNS_BLOCKSTORE_S3_SECRET_KEY env
         * vars (value override only - env never activates, D-07/D-08).
         *
         * @note accessKey/secretKey are NEVER-LOGGED credentials: no log
         *       line, error string, or exception message may format their
         *       values (redaction by omission; see LogSummary).
         */
        struct ColdStorageOptions
        {
            bool        enabled{ false };
            std::string endpoint;
            std::string bucket;
            std::string accessKey; ///< credential - never logged
            std::string secretKey; ///< credential - never logged
            std::string prefix;
            bool        pathStyle{ true };
            bool        tls{ false };
            /// Optional s3d admin listener URL - connection topology, NOT a
            /// secret and NOT a cadence knob; empty = documented s3d default
            /// "http://127.0.0.1:8001" (D-06, v1.1 Phase 10).
            std::string admin_endpoint;
            /// Admin listener credential - never logged, never at rest in
            /// config (env-only resolution via SGNS_BLOCKSTORE_S3_ADMIN_PASSWORD;
            /// T-10-01).
            std::string adminPassword;

            /**
             * @brief      Backpressure knobs for the cold-storage drainer
             *             (D-05/D-06/D-07, v1.1 Phase 8).
             *
             * Exactly four knobs live under the nested
             * "blockstore.backpressure" sub-object; 0 means the built-in
             * default (normalized post-parse): drain_interval_ms 5000,
             * backoff_cap_ms 60000, warn_pending 10000, critical_pending
             * 100000. The watermark alarm is ALWAYS active with no opt-in
             * (D-07); backpressure knobs are file-only (no env surface).
             * warn_pending must stay below critical_pending
             * (validated post-normalization).
             */
            struct BackpressureOptions
            {
                uint32_t drain_interval_ms{ 0 };  ///< poll cadence floor; 0 = default 5000
                uint32_t warn_pending{ 0 };       ///< warn watermark (pending-upload records); 0 = default 10000
                uint32_t critical_pending{ 0 };   ///< critical watermark; 0 = default 100000
                uint32_t backoff_cap_ms{ 0 };     ///< exponential-backoff cap; 0 = default 60000
            };
            BackpressureOptions backpressure;

            /**
             * @brief      Flush-cadence knobs for the flush-gated
             *             durability advancement (D-11/D-06, v1.1
             *             Phase 10).
             *
             * Exactly two knobs live under the nested
             * "blockstore.durability" sub-object; 0 means the built-in
             * default (normalized post-parse): flush_min_interval_ms
             * 30000, flush_max_interval_ms 300000. flush_min_interval_ms
             * must stay <= flush_max_interval_ms (validated
             * post-normalization on both EFFECTIVE values). A
             * durability-only section never activates cold storage
             * (Phase-7 (e) inheritance); the knobs are file-only (no env
             * surface), and no knob can lower the eviction durability
             * gate (compile-time UploadedDurable, D-10).
             */
            struct DurabilityOptions
            {
                uint32_t flush_min_interval_ms{ 0 }; ///< flush floor; 0 = default 30000
                uint32_t flush_max_interval_ms{ 0 }; ///< flush backstop; 0 = default 300000
            };
            DurabilityOptions durability;

            /**
             * @brief      Retention-GC knobs (GC-01/D-02..D-04/D-07/D-13,
             *             v1.1 Phase 11).
             *
             * Exactly four knobs live under the nested
             * "blockstore.retention" sub-object. retention_age_ms is
             * REQUIRED (present-but-absent or 0 => loud parse failure -
             * D-04: age 0 is impossible to configure meaningfully and a
             * 0-threshold would arm immediate eviction); dry_run parses
             * here and only suppresses deletes from 11-02 onward. The two
             * cadence knobs follow the 0-equals-default discipline
             * (gc_interval_ms 600000, grace_ms 60000 - normalized
             * post-parse; the defer-to-next-tick grace makes any grace
             * value valid, so NO cross-field rule). A retention-only
             * section never activates cold storage (Phase-7 (e)
             * inheritance) - retention_configured && !enabled is the
             * one-WARN inert posture, never a silent disable. The knobs
             * are file-only (no env surface).
             */
            struct RetentionOptions
            {
                uint64_t retention_age_ms{ 0 }; ///< REQUIRED age threshold (no default)
                bool     dry_run{ false };      ///< report candidates without deleting (11-02+)
                uint32_t gc_interval_ms{ 0 };   ///< pass cadence; 0 = default 600000
                uint32_t grace_ms{ 0 };         ///< evicting->delete grace; 0 = default 60000
            };
            RetentionOptions retention;
            /// True when the parse saw a "blockstore.retention" sub-object
            /// (drives the inert WARN and the LogSummary extension).
            bool retention_configured{ false };

            /**
             * @brief      Sanitized one-line summary: endpoint, bucket,
             *             prefix, path_style, tls, and the four backpressure
             *             knobs ONLY - the credential members are never
             *             referenced (redaction by omission, T-7-03).
             * @return     log-safe summary string
             */
            std::string LogSummary() const;
        };

        /**
         * @brief      Resolved cold-storage credential overrides from the
         *             process environment (D-07/D-08, v1.1 Phase 7).
         *
         * Production resolution (DefaultColdStorageEnvLookup, globaldb.cpp)
         * reads std::getenv for exactly SGNS_BLOCKSTORE_S3_ACCESS_KEY and
         * SGNS_BLOCKSTORE_S3_SECRET_KEY; unset OR empty reads as empty (no
         * override). Tests inject values directly without touching the
         * process environment. Env overrides credential VALUES only - env
         * alone can never activate cold storage (D-08).
         */
        struct ColdStorageEnvLookup
        {
            std::string access_key; ///< override for blockstore access_key; empty = none
            std::string secret_key; ///< override for blockstore secret_key; empty = none
            /// Admin listener password (SGNS_BLOCKSTORE_S3_ADMIN_PASSWORD;
            /// unset or empty = none - flush then cannot authenticate and
            /// alarms through the D-07 discipline). Env-only: no config key
            /// ever carries it (D-06).
            std::string admin_password;
        };

        /**
         * @brief      Production env resolution for the two credential
         *              variables (unset or empty => empty member).
         * @return      resolved overrides
         */
        static ColdStorageEnvLookup DefaultColdStorageEnvLookup();

        /// Parse outcome: ColdStorageOptions on success; on failure a
        /// human-readable message naming the offending key or the missing
        /// field(s) - never a field VALUE (T-7-03).
        using ColdStorageParseResult = outcome::result<ColdStorageOptions, std::string>;

        /**
         * @brief      Default-constructed cold-storage options (the
         *             disabled posture) for GlobalDB::New's trailing
         *             parameter.
         * @note       Out-of-line body by necessity: clang rejects a
         *             default argument that constructs an NSDMI
         *             aggregate inside the enclosing class definition
         *             ("default member initializer needed within
         *             definition of enclosing class" - the SC4 CI
         *             failures, v1.1 Phase 12 plan 12-03); the thunk
         *             defers the construction to a complete-class
         *             context. MSVC accepts both shapes.
         */
        static ColdStorageOptions DefaultColdStorageOptions();

        /**
         * @brief      Parses the strict tri-state "blockstore" section of a
         *              crdt_config.json document (INTG-02, D-01..D-08).
         *
         * Semantics (strict section inside a lenient-at-file-level loader):
         *  - D-03: no section / empty object / optional-fields-only =>
         *      disabled options (not an error)
         *  - D-04: partial fill (a required field non-blank in the file
         *      while another required field is blank AFTER env resolution)
         *      => error naming the missing field(s)
         *  - D-05: unknown key or wrong-typed known key => error naming
         *      the key
         *  - D-06: whole-file JSON parse failure => disabled options (the
         *      caller's legacy lenient behavior, NOT an error)
         *  - D-07/D-08: env overrides credential values only; a section
         *      whose four required fields are all blank in the file stays
         *      disabled regardless of environment
         *
         * @param[in]  crdt_config_text the whole config file contents
         * @return     parsed options (check .has_error() / guard before
         *              .value() - the result alias terminates on misuse)
         */
        static ColdStorageParseResult ParseColdStorageConfig( const std::string &crdt_config_text );

        /**
         * @brief      Injectable-env overload; identical semantics with an
         *              explicit env resolution (tests inject without
         *              touching the process environment).
         * @param[in]  crdt_config_text the whole config file contents
         * @param[in]  env_lookup pre-resolved credential overrides
         * @return     parsed options or a naming error message
         */
        static ColdStorageParseResult ParseColdStorageConfig( const std::string         &crdt_config_text,
                                                              const ColdStorageEnvLookup &env_lookup );

        using Buffer             = base::Buffer;
        using QueryResult        = CrdtDatastore::QueryResult;
        using RocksDB            = storage::rocksdb;
        using CRDTHeadListResult = CrdtHeads::CRDTListResult;

        /**
         * @brief       Factory method to create a GlobalDB instance
         * @param[in]   context The io context used to run its inner methods
         * @param[in]   databasePath Local system's path where data will be stored, not used if datastore is not nullptr
         * @param[in]   pubsub The pubsub instance used to communicate
         * @param[in]   crdtOptions CRDT options
         * @param[in]   graphsyncnetwork The graphsync networks used
         * @param[in]   scheduler libp2p scheduler
         * @param[in]   generator The request ID generator from graphsync
         * @param[in]   datastore datastore to be used. If not defined, created using databasePath
         * @param[in]   backup_options configuration for automatic backups of the CRDT data
         * @param[in]   cold_storage_options configuration for the cold-storage (sgns-blockstore)
         *              integration; default-constructed = disabled = today's behavior (v1.1 Phase 7)
         * @return      Instance of the GlobalDB initialized or Error
         */
        static outcome::result<std::shared_ptr<GlobalDB>> New(
            std::shared_ptr<boost::asio::io_context>                              context,
            std::string                                                           databasePath,
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                      pubsub,
            std::shared_ptr<CrdtOptions>                                          crdtOptions,
            std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
            std::shared_ptr<libp2p::basic::Scheduler>                             scheduler,
            std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
            std::shared_ptr<RocksDB>                                              datastore = nullptr,
            BackupOptions          backup_options      = BackupOptions{ false, 15, 12, true },
            ColdStorageOptions     cold_storage_options = DefaultColdStorageOptions() );

        /**
         * @brief      Destructor or GlobalDB
         */
        ~GlobalDB();

        /// Pair of key and value to be stored in CRDT
        using DataPair = std::pair<HierarchicalKey, Buffer>;
        /// CRDT Filter callback type
        using GlobalDBFilterCallback         = CrdtDatastore::CRDTElementFilterCallback;
        using GlobalDBNewElementCallback     = CrdtDatastore::CRDTNewElementCallback;
        using GlobalDBDeletedElementCallback = CrdtDatastore::CRDTDeletedElementCallback;

        /**
         * @enum        Error
         * @brief       Enumeration of error codes used in the proof classes.
         */
        enum class Error : uint8_t
        {
            ROCKSDB_IO = 0,                 ///< RocksDB wasn't opened
            IPFS_DB_NOT_CREATED,            ///< IPFS datastore not created
            DAG_SYNCHER_NOT_LISTENING,      ///< DAG Syncher listen error
            CRDT_DATASTORE_NOT_CREATED,     ///< CRDT DataStore not created
            PUBSUB_BROADCASTER_NOT_CREATED, ///< CRDT DataStore not created
            INVALID_PARAMETERS,             ///< Invalid parameters
            GLOBALDB_NOT_STARTED,           ///< Start wasn't called
            COLD_STORAGE_INIT_FAILED,       ///< Cold storage stack failed to open (v1.1 Phase 8)
            POST_COMMIT_ERASE_ANOMALY,      ///< Erase already committed; post-commit seal re-check failed
        };

        /**
         * @brief Puts key-value pair to the CRDT store, optionally specifying a broadcast topic.
         * @param[in] key The hierarchical key where the value should be stored.
         * @param[in] value The value to store.
         * @param[in] topics Topics to publish to.
         * @return outcome::success on success, or outcome::failure otherwise.
         */
        outcome::result<CID> Put( const HierarchicalKey                 &key,
                                  const Buffer                          &value,
                                  const std::unordered_set<std::string> &topics );

        /** Writes an authoritative immutable record that converges by SHA-256 content ordering. */
        outcome::result<CID> PutConvergentImmutable( const HierarchicalKey                 &key,
                                                      const Buffer                          &value,
                                                      const std::unordered_set<std::string> &topics );

        /**
         * @brief       Writes a batch of CRDT data all at once
         * @param[in]   data_vector A set of crdt to be written in a single transaction
         * @param[in]   topics Topics to publish to.
         * @return      outcome::failure on error or success otherwise
         */
        outcome::result<CID> Put( const std::vector<DataPair>           &data_vector,
                                  const std::unordered_set<std::string> &topics );

        /**
         * @brief       Puts a key-value pair directly into local storage, bypassing DAG broadcast.
         *              See CrdtDatastore::PutKeyLocal for the full rationale/contract.
         * @param[in]   key The hierarchical key where the value should be stored.
         * @param[in]   value The value to store.
         * @param[in]   id Provenance/tie-break identifier for the local write.
         * @return      outcome::success on success, or outcome::failure otherwise.
         */
        outcome::result<void> PutLocal( const HierarchicalKey &key, const Buffer &value, const std::string &id );

        /** Gets a value that corresponds to specified key.
        * @param key - value key
        * @return value as a Buffer
        */
        outcome::result<Buffer> Get( const HierarchicalKey &key );

        /** Removes value for a given key.
        * @param key to remove from storage
        * @param topics Topics to publish to
        * @return outcome::failure on error or success otherwise
        */
        outcome::result<CID> Remove( const HierarchicalKey &key, const std::unordered_set<std::string> &topics );

        /** Queries CRDT key-value pairs by prefix. If the prefix is empty returns all elements that were not tombstoned
        * @param keyPrefix - keys prefix to match. An empty prefix matches any key.
        * @return list of key-value pairs matches prefix
        */
        outcome::result<QueryResult> QueryKeyValues( std::string_view keyPrefix );

        /**
         * @brief       Queries with a middle part that can be a wildcard, negated string or normal string
         * @param[in]   prefix_base: The base prefix to query
         * @param[in]   middle_part: Either a string (normal query), '*' or !string
         * @param[in]   remainder_prefix: The remainder part of the query prefix
         * @return      A list of key value pairs
         */
        outcome::result<QueryResult> QueryKeyValues( const std::string &prefix_base,
                                                     const std::string &middle_part,
                                                     const std::string &remainder_prefix );

        /** Converts a unique key part to a string representation
        * @param key - binary key to convert
        * @return string represenation of a unique key part
        */
        outcome::result<std::string> KeyToString( const Buffer &key ) const;

        /** Create a transaction object
        * @return new transaction
        */
        std::shared_ptr<AtomicTransaction> BeginTransaction();

        outcome::result<void> AddBroadcastTopic( const std::string &topicName );
        void                  AddTopicName( std::string topicName );
        void                  AddListenTopic( std::string topicName );

        void PrintDataStore();

        std::shared_ptr<RocksDB>                          GetDataStore();

        /**
         * @brief Reads a node-local key straight from RocksDB, bypassing the CRDT layer.
         * @param[in] key Raw key, not a HierarchicalKey; nothing here is replicated.
         * @return The stored value, or `operation_canceled` once shutdown released the store.
         */
        outcome::result<Buffer> GetRaw( const Buffer &key ) const;

        /**
         * @brief Writes a node-local key straight to RocksDB, bypassing the CRDT layer.
         * @param[in] key Raw key, not a HierarchicalKey; nothing here is replicated.
         * @param[in] value Value to store.
         * @return Failure on I/O error, or `operation_canceled` once shutdown released the store.
         */
        outcome::result<void> PutRaw( const Buffer &key, const Buffer &value );

        /**
         * @brief Prefix-scans node-local keys straight from RocksDB, bypassing the CRDT layer.
         * @param[in] key_prefix Raw key prefix.
         * @return Matching key/value pairs, or `operation_canceled` once shutdown released the store.
         */
        outcome::result<QueryResult> QueryRaw( const Buffer &key_prefix ) const;

        /**
         * @brief Generic node-local physical erase: removes, in ONE atomic
         *        RocksDB WriteBatch, the CRDT set entries for @p key (via the
         *        CrdtSet owner), the raw block bytes for @p cid (via the
         *        DAGSyncer owner), and the node-local raw bookkeeping entry at
         *        @p key itself.
         *
         * This is the ONLY entry to physical erasure. Guards run inside the
         * API and reject fail-closed (Error::INVALID_PARAMETERS) BEFORE any
         * staging: a CID that is the head of ANY topic, the root of a
         * pending/active sync job, LRU-cached, or unresolved is not erased.
         *
         * Compose critical section (D-01): the ENTIRE compose -- guard
         * evaluation, staging, the single commit, and the post-commit seal
         * re-assert -- runs inside one owner-vended CrdtSet critical section
         * (CrdtDatastore::MakeComposeLock), the same discipline PutElems
         * already uses across build+commit. The former CHECK-THEN-ACT window
         * (WR-02 -- a concurrent PutElems re-creating the erased entries
         * between the under-lock staging and an outside-the-lock commit) is
         * CLOSED by construction: every CrdtSet-mediated write takes the same
         * recursive mutex, so a concurrent PutElems can never interleave
         * between a guard check and the commit. The hold is scoped to this
         * single per-tx compose (acquired as the first act, released after
         * the commit and the post-commit seal re-assert) and is NEVER held
         * across evaluations or multiple composes -- D-01 bounded holds, as
         * documented on CrdtSet::MakeComposeLock. The seal marker of @p cid
         * is never addressed and is re-asserted after the commit -- a
         * vanished seal fails loudly with Error::POST_COMMIT_ERASE_ANOMALY
         * (a COMMITTED erase -- see @return).
         *
         * Fires NO CRDT hooks and publishes NO tombstones: this is a local
         * prune operation, not a CRDT-logical delete (the network-wide
         * tombstone the CRDT delete path publishes must never fire here).
         *
         * @p key addresses both the CRDT element key and the raw map entry;
         * the caller (TransactionManager::EraseTransactionEntry / the future
         * TxPruner) builds domain keys -- this facade stays generic.
         *
         * @param[in] key CRDT element key == raw bookkeeping key, built by the
         *            domain owner; never concatenated here.
         * @param[in] cid CID whose raw block bytes are erased alongside the
         *            entries for @p key.
         * @param[in] extra_entry OPTIONAL caller-built key+value buffer pair
         *            staged into the SAME batch after the raw-map removal and
         *            before the single commit (default: none -- existing
         *            two-arg callers are unchanged). The buffers are OPAQUE:
         *            GlobalDB stays generic and never inspects or builds key
         *            shapes. The value-XOR-marker atomicity contract: when
         *            the caller stages the prune marker here, one commit
         *            establishes "value absent AND marker present" together
         *            -- an interrupted batch leaves BOTH sides in their
         *            pre-erase state, never either alone.
         * @return outcome::success on success, or outcome::failure on a guard
         *         rejection, a staging error, or a commit error.
         *         Error::POST_COMMIT_ERASE_ANOMALY reports a COMMITTED erase:
         *         the batch landed, then the post-commit seal re-check
         *         failed. For that value the data IS erased -- callers must
         *         not treat it as "nothing was erased" (guard rejections,
         *         Error::INVALID_PARAMETERS, erase nothing).
         */
        outcome::result<void> EraseEntry( const std::string                              &key,
                                          const CID                                      &cid,
                                          const std::optional<std::pair<Buffer, Buffer>> &extra_entry = std::nullopt );

        /**
         * @brief Test-only interruption seam for EraseEntry's compose window.
         * @param hook invoked after each staging step (1 = after CRDT-entry
         *        staging, 2 = after block staging, 3 = after raw-map staging);
         *        a @c true return aborts the erase WITHOUT committing, so the
         *        dying batch proves all-or-nothing. Pass nullptr to clear.
         * @note No production caller may invoke this method.
         */
        static void SetEraseInterruptHookForTest( std::function<bool( int step )> hook );

        std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> GetBroadcaster();
        std::shared_ptr<CRDTWorkJournal>                  GetWorkJournal() const;

        /** Registers a filter callback for elements matching a pattern.
         * @param pattern The pattern to match elements against.
         * @param filter The callback to invoke for matching elements.
         * @return true if the filter was successfully registered, false otherwise.
         */
        bool RegisterElementFilter( const std::string &pattern, GlobalDBFilterCallback filter );

        /** Registers a callback for new elements matching a pattern.
         * @param pattern The pattern to match new elements against.
         * @param callback The callback to invoke for matching new elements.
         * @return true if the callback was successfully registered, false otherwise.
         */
        bool RegisterNewElementCallback( const std::string &pattern, GlobalDBNewElementCallback callback );

        /** Registers a callback for deleted elements matching a pattern.
         * @param pattern The pattern to match deleted elements against.
         * @param callback The callback to invoke for matching deleted elements.
         * @return true if the callback was successfully registered, false otherwise.
         */
        bool RegisterDeletedElementCallback( const std::string &pattern, GlobalDBDeletedElementCallback callback );

        /** Unregisters the filter callback for a pattern.
         * @param pattern The pattern to unregister the filter for.
         */
        void UnregisterElementFilter( const std::string &pattern );

        /**
         * @brief Unregisters the new element callback for a pattern.
         * @param pattern The pattern to unregister the new element callback for.
         */

        void UnregisterNewElementCallback( const std::string &pattern );
        /**
         * @brief Unregisters the deleted element callback for a pattern.
         * @param pattern The pattern to unregister the deleted element callback for.
         */
        void UnregisterDeletedElementCallback( const std::string &pattern );

        /**
         * @brief      Read the cold-storage drainer status triple (D-08,
         *             v1.1 Phase 8): pending-upload record count, watermark
         *             state, drainer health - from drainer atomics, never a
         *             journal scan. Rides the active-accessor pattern:
         *             the default triple once shutdown has started.
         * @return     live status, or defaults when disabled/shut down
         */
        ColdStorageStatus GetColdStorageStatus() const;

        /**
         * @brief      Per-CID cold-upload state through the ungated
         *             bridge (D-09, v1.1 Phase 10): NoRecord / Pending /
         *             Accepted / Durable. Rides the active-accessor
         *             pattern: NoRecord once shutdown has started.
         * @param[in]  cid the block whose journal record to read
         * @return     the mapped state (never a library type)
         */
        ColdUploadState GetColdUploadState( const CID &cid ) const;

        /**
         * @brief Starts the GlobalDB instance.
         */
        void Start();

        /**
         * @brief Immediately quiesce and shut down CRDT intake and workers.
         * Safe to call multiple times.
         */
        void ShutdownNow();

        /**
         * @brief Starts receiving CIDs.
         */
        void StartCIDReceiving();

        /**
         * @brief Starts CIC synchronization.
         */
        void StartCICSync();

        /**
         * @brief Starts rebroadcasting heads.
         */
        void StartRebroadcastHeads();

        outcome::result<CRDTHeadListResult> GetCRDTHeadList();

        /** Read a key's per-key CRDT priority through the CRDT datastore
        * (read-only). Contract mirrors CrdtDatastore::GetPriority /
        * CrdtSet::GetPriority: returns the stored priority minus 1, and a key
        * with NO priority record reads as success with value 0 (the NOT_FOUND
        * case is tolerated) -- 0 never implies existence; callers must
        * establish key existence separately.
        * @param key key string
        * @return priority of the key or outcome::failure on error
        */
        outcome::result<uint64_t> GetKeyPriority( const std::string &key ) const;

        outcome::result<uint64_t> GetCRDTHeadHeight( const CID &aCid, const std::string &topic );
        outcome::result<void>     CRDTHeadRemove( const CID &aCid, const std::string &topic );
        outcome::result<void>     CRDTHeadAdd( const CID &aCid, const std::string &topic, uint64_t priority );
        outcome::result<crdt::CrdtDatastore::JobStatus> GetCIDJobStatus( const CID &cid ) const;

        /**
         * @brief       Request head broadcast for specified topics
         * @param[in]   topics Vector of topic names to broadcast heads for
         * @return      outcome::success on success, or outcome::failure on error
         */
        outcome::result<void> RequestHeadBroadcast( const std::set<std::string> &topics );

        /**
         * @brief       Get the topics that are being listened to
         * @return      A set of the monitored topic names
         */
        outcome::result<std::unordered_set<std::string>> GetMonitoredTopics() const;

        std::shared_ptr<crdt::CrdtDatastore> GetCRDTDataStore();

        outcome::result<std::vector<std::pair<std::string, base::Buffer>>> GetLocalDeltaKeyValues(
            const std::string &cid_string );

        /** Enumerate the keys a delta block carries -- elements AND
        * tombstones -- keys-only, through the CRDT datastore (read-only,
        * parse-at-call-time, strictly local: never fetches from the network,
        * unknown CID fails). Elements-only precedent GetLocalDeltaKeyValues
        * is left untouched.
        * @param cid_string CID string of the delta block
        * @return carried keys (element keys then tombstone keys) or outcome::failure
        */
        outcome::result<std::vector<std::string>> GetLocalDeltaKeys( const std::string &cid_string ) const;

        /** Byte-level local block presence (read-only): true iff raw block
        * BYTES exist for @p cid_string in the block store, with NO parse
        * attempted. Unlike the parse-based read paths (and HasBlock), a
        * PRESENT-but-corrupt block answers TRUE here -- the erase-side block
        * gate uses this to tell an ABSENT block (already erased --
        * erase-completing evidence) from a corrupt one (fail closed).
        * Delegates to GraphsyncDAGSyncer::HasRawBlockBytes.
        * @param cid_string CID string of the delta block
        * @return true when bytes exist; false when absent; failure on a
        *         malformed CID string, an uninitialized DAG syncer, or a
        *         store read error (callers fail closed on error)
        */
        outcome::result<bool> HasLocalBlock( const std::string &cid_string ) const;

    private:
        /**
         * @brief       Constructs a new Global D B object
         * @param[in]   context the io context used to run inner methods
         * @param[in]   databasePath the local path where the files are gonna be stored
         * @param[in]   pubsub the pubsub instance used to communicate
         */
        GlobalDB( std::shared_ptr<boost::asio::io_context>         context,
                  std::string                                      databasePath,
                  std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub );

        /**
         * @brief       Initializes the GlobalDB instance by creating the CRDT datastore and broadcaster
         * @param[in]   crdtOptions CRDT options
         * @param[in]   graphsyncnetwork The graphsync network used for DAG sync
         * @param[in]   scheduler libp2p scheduler
         * @param[in]   generator The request ID generator from graphsync
         * @param[in]   datastore datastore to be used. If not defined, created using databasePath
         * @return      outcome::success on success, or outcome::failure on error
         */
        outcome::result<void> Init( std::shared_ptr<CrdtOptions>                               crdtOptions,
                                    std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> graphsyncnetwork,
                                    std::shared_ptr<libp2p::basic::Scheduler>                  scheduler,
                                    std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                                    std::shared_ptr<RocksDB> datastore = nullptr );

        void scheduleBootstrap( std::shared_ptr<boost::asio::io_context> io_context,
                                std::shared_ptr<libp2p::Host>            host );

        std::shared_ptr<boost::asio::io_context> m_context;
        std::string                              m_databasePath;
        int                                      m_dagSyncPort;
        std::string                              m_graphSyncAddrs;

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>  m_pubsub;
        std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> m_broadcaster;
        std::shared_ptr<RocksDB>                          m_datastore;
        /// Concrete DAGSyncer (never the base interface) so EraseEntry can
        /// stage through GraphsyncDAGSyncer::StageBlockRemoval -- test doubles
        /// keep their own implementations untouched.
        std::shared_ptr<GraphsyncDAGSyncer>               m_dagSyncer = nullptr;
        std::atomic_bool                                  started_;
        bool                                              cid_sync_started_;
        bool                                              cid_receiving_started_;
        bool                                              head_broadcasting_started_;
        BackupOptions                                     backup_options_{};
        ColdStorageOptions                                cold_storage_options_;
        /// Journal-spill decorator handed to the DAG syncer when cold
        /// storage is enabled (v1.1 Phase 8); null when disabled.
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>   cold_data_store_;
        /// Drainer-thread manager (v1.1 Phase 8); null until plan 08-02
        /// and whenever cold storage is disabled. Declared AFTER the
        /// datastore members' cluster so reverse-declaration destruction
        /// favors us (Pitfall-5 ordering; shared_ptr ownership is
        /// belt-and-suspenders).
        std::shared_ptr<ColdStorageManager>               cold_manager_;
        /// Cold-read chain service (v1.1 Phase 9); null whenever cold
        /// storage is disabled. Declared AFTER cold_manager_ so reverse-
        /// declaration destruction favors the same ordering rationale.
        std::shared_ptr<ColdReadService>                  cold_read_service_;
        /// The ONE mutex shared by the publisher (decorator set) and the
        /// drainer thread over the LocalBlockStore (v1.1 Phase 8).
        std::shared_ptr<std::mutex>                       cold_store_mutex_;
        std::string                                       backup_directory_;
        std::atomic_bool                                  stop_backup_thread_{ false };
        std::atomic_bool                                  shutdown_started_{ false };
        std::thread                                       backup_thread_;
        std::mutex                                        backup_wait_mutex_;
        std::condition_variable                           backup_wait_cv_;

        //std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht_;
        //std::shared_ptr<libp2p::protocol::Identify> identify_;
        //std::shared_ptr<libp2p::protocol::IdentifyMessageProcessor> identifymsgproc_;
        //std::shared_ptr<libp2p::protocol::HolepunchClient> holepunch_;
        //std::shared_ptr<libp2p::protocol::HolepunchClientMsgProc> holepunchmsgproc_;

        int obsAddrRetries = 0;

        /// Test-only EraseEntry interruption hook; see
        /// SetEraseInterruptHookForTest. Inert (nullptr) in production.
        static std::function<bool( int step )> erase_interrupt_hook_for_test_;

        std::shared_ptr<CrdtDatastore> m_crdtDatastore;
        mutable std::mutex             lifecycle_mutex_; ///< Guards service pointers during shutdown.

        std::shared_ptr<RocksDB>       ActiveDataStore() const;

        std::shared_ptr<CrdtDatastore>        ActiveCRDTDataStore() const;
        std::shared_ptr<PubSubBroadcasterExt> ActiveBroadcaster() const;

        /** @brief Resolves the backup directory path based on the database path. */
        std::string ResolveBackupDirectory( const std::string &databasePathAbsolute ) const;
        /** @brief Creates a backup immediately. */
        void CreateBackupNow();
        /** @brief Starts the backup loop in a separate thread. */
        void StartBackupLoop();
        /** @brief Stops the backup loop and waits for the thread to finish. */
        void StopBackupLoop();

        /** @brief Starts the cold-storage drainer beside the backup loop
         *         (D-10, v1.1 Phase 8); delegates to the stack bridge. */
        void StartColdStorageManager();
        /** @brief Stops the cold-storage drainer with bounded grace then
         *         join (D-11, v1.1 Phase 8); delegates to the stack bridge. */
        void StopColdStorageManager();

        /** @brief Starts the re-warm worker (READ-02, v1.1 Phase 9);
         *         delegates to the stack bridge (null-safe). */
        void StartColdReadService();
        /** @brief Stops the re-warm worker with bounded grace then join
         *         (Phase-8 D-11 shape, v1.1 Phase 9); delegates to the
         *         stack bridge (null-safe). */
        void StopColdReadService();

        sgns::base::Logger m_logger = sgns::base::createLogger( "GlobalDB" );
    };

    // Complete-class context (out-of-line): the NSDMI-dependent aggregate
    // construction moved out of GlobalDB's definition - clang-compatible
    // (SC4, v1.1 Phase 12 plan 12-03).
    inline GlobalDB::ColdStorageOptions GlobalDB::DefaultColdStorageOptions()
    {
        return ColdStorageOptions{};
    }
}

/**
 * @brief       Macro for declaring error handling in the GlobalDB class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GlobalDB::Error );

#endif // SUPERGENIUS_CRDT_GLOBALDB_HPP

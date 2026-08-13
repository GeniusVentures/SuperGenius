/**
 * @file       Blockchain.hpp
 * @brief      Header file for the Blockchain class, which provides an interface for block storage operations.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef SGNS_BLOCKCHAIN_HPP
#define SGNS_BLOCKCHAIN_HPP

#include <memory>
#include <map>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <atomic>
#include <optional>
#include <unordered_map>

#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/proto/delta.pb.h"
#include "account/GeniusAccount.hpp"
#include "blockchain/impl/proto/SGBlockchain.pb.h"
#include "blockchain/Consensus.hpp"
#include "base/buffer.hpp"
#include "crdt/crdt_callback_manager.hpp"
#include "base/sgns_version.hpp"

namespace sgns
{
    class ValidatorRegistry;

    class Migration3_5_0To3_6_0;
    class Migration3_6_0To3_7_0;

    /**
     * @brief Manages genesis/account-creation blocks and consensus integration.
     *
     * This class coordinates CRDT-backed persistence of blockchain bootstrap
     * blocks, validates signatures, tracks authoritative CIDs, and exposes a
     * high-level interface to submit and verify consensus subjects/proposals.
     */
    class Blockchain : public std::enable_shared_from_this<Blockchain>
    {
    public:
        /**
         * @brief      Error class of the Blockchain module
         */
        enum class Error
        {
            GENESIS_BLOCK_CREATION_FAILED = 0,           ///< Failed to create the genesis block
            GENESIS_BLOCK_INVALID_SIGNATURE,             ///< Genesis block has invalid signature
            GENESIS_BLOCK_UNAUTHORIZED_CREATOR,          ///< Genesis block created by unauthorized key
            GENESIS_BLOCK_SERIALIZATION_FAILED,          ///< Failed to serialize/deserialize genesis block
            GENESIS_BLOCK_MISSING,                       ///< Genesis block wasn't received
            ACCOUNT_CREATION_BLOCK_MISSING,              ///< Account creation is missing
            ACCOUNT_CREATION_BLOCK_CREATION_FAILED,      ///< Failed to create account creation block
            ACCOUNT_CREATION_BLOCK_INVALID_SIGNATURE,    ///< Account creation block has invalid signature
            ACCOUNT_CREATION_BLOCK_SERIALIZATION_FAILED, ///< Failed to serialize/deserialize account creation block
            ACCOUNT_CREATION_BLOCK_INVALID_GENESIS_LINK, ///< Account creation block not properly linked to genesis
            VALIDATOR_REGISTRY_CREATION_FAILED,          ///< Failed to create validator registry
            BLOCKCHAIN_NOT_INITIALIZED,                  ///< Blockchain not fully initialized yet
        };

        /**
         * @brief Callback invoked when blockchain initialization/processing finishes.
         */
        using BlockchainCallback = std::function<void( outcome::result<void> )>;

        /**
         * @brief Factory method to create Blockchain as shared_ptr.
         * @param[in] global_db CRDT database instance.
         * @param[in] account GeniusAccount instance.
         * @param[in] pubsub PubSub instance used by consensus manager.
         * @param[in] callback Called when initialization completes.
         * @return Shared pointer to blockchain instance.
         */
        static std::shared_ptr<Blockchain> New( std::shared_ptr<crdt::GlobalDB>            global_db,
                                                std::shared_ptr<GeniusAccount>             account,
                                                std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                BlockchainCallback                         callback );

        /**
         * @brief Destroys the blockchain instance.
         */
        ~Blockchain();

        /**
         * @brief Start the blockchain with async genesis block handling.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> Start();
        /**
         * @brief Stops blockchain background processing and callbacks.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> Stop();

        /**
         * @brief Handle received genesis block from pubsub
         * @param serialized_genesis The received genesis block data
         * @return outcome::success when processed, otherwise an error.
         */
        outcome::result<void> OnGenesisBlockReceived( const base::Buffer &serialized_genesis );

        /**
         * @brief Handle received account creation block from pubsub
         * @param serialized_account_creation The received account creation block data
         * @return outcome::success when processed, otherwise an error.
         */
        outcome::result<void> OnAccountCreationBlockReceived( const base::Buffer &serialized_account_creation );

        /**
         * @brief Set the authorized full node public address (for testing purposes)
         * @param pub_address The public address that is authorized to create genesis blocks
         */
        static void SetAuthorizedFullNodeAddress( const std::string &pub_address );

        /**
         * @brief Get the current authorized full node public address
         * @return The authorized full node public address
         */
        static std::string GetAuthorizedFullNodeAddress();

        /**
         * @brief Registers additional validator addresses to include in the genesis registry.
         *
         * Must be called before the genesis block is created. The authorized full-node
         * address is always the first entry; these addresses are appended.
         * @param addresses Additional validator public addresses.
         */
        static void SetAdditionalGenesisValidatorAddresses( const std::vector<std::string> &addresses );

        /**
         * @brief Returns additional genesis validator addresses previously set.
         * @return Vector of additional genesis validator public addresses.
         */
        static std::vector<std::string> GetAdditionalGenesisValidatorAddresses();

        /**
         * @brief Returns the stored CID of the selected genesis block.
         * @return Genesis CID on success, otherwise an error.
         */
        outcome::result<std::string> GetGenesisCID() const;
        /**
         * @brief Returns the stored CID of the selected account-creation block.
         * @return Account-creation CID on success, otherwise an error.
         */
        outcome::result<std::string> GetAccountCreationCID() const;
        /**
         * @brief Returns validator registry owned by this blockchain instance.
         * @return Shared pointer to validator registry, possibly null when unavailable.
         */
        std::shared_ptr<ValidatorRegistry> GetValidatorRegistry() const;

        /**
         * @brief Forces full-node mode behavior for bootstrap/generation flow.
         */
        void SetFullNodeMode();

        /**
         * @brief Registers a consensus subject handler by canonical subject type string.
         * @param[in] subject_type Canonical subject type to handle.
         * @param[in] handler Callback invoked for matching subjects.
         * @return `true` on successful registration.
         */
        bool RegisterSubjectHandler( std::string_view subject_type, ConsensusManager::SubjectHandler handler );
        /**
         * @brief Unregisters a consensus subject handler by canonical subject type string.
         * @param[in] subject_type Canonical subject type to remove.
         */
        void UnregisterSubjectHandler( std::string_view subject_type );
        /**
         * @brief Registers a consensus certificate handler by canonical subject type string.
         * @param[in] subject_type Canonical subject type associated with certificate callback.
         * @param[in] handler Callback invoked for matching certificates.
         * @return `true` on successful registration.
         */
        bool RegisterCertificateHandler( std::string_view                            subject_type,
                                         ConsensusManager::CertificateSubjectHandler handler );
        /**
         * @brief Unregisters a consensus certificate handler by canonical subject type string.
         * @param[in] subject_type Canonical subject type to remove.
         */
        void UnregisterCertificateHandler( std::string_view subject_type );
        /**
         * @brief Registers a proposal cleanup callback by canonical subject type string.
         * @param[in] subject_type Canonical subject type to handle.
         * @param[in] handler Callback invoked when a proposal slot is cleaned up due to timeout.
         * @return `true` on successful registration via the consensus manager.
         */
        bool RegisterProposalCleanupHandler( std::string_view                         subject_type,
                                             ConsensusManager::ProposalCleanupHandler handler );

        /**
         * @brief Registers a slot key handler for a specific embedded transaction oneof case.
         * @param[in] transaction_case EmbeddedTransaction oneof case number (e.g. kMintV2).
         * @param[in] handler Callback that produces a deterministic slot key for proposals
         *                    carrying this embedded transaction type.
         */
        void RegisterSlotKeyHandler( std::string_view subject_type, ConsensusManager::SlotKeyHandler handler );

        /**
         * @brief      Forwards a slot-hash populator to the consensus manager (Phase 6, D-01).
         * @param[in]  populator  Callback invoked during CreateVote before signing.
         * @details    GeniusNode wires this during blockchain initialization to bridge
         *             TransactionManager::GetPublicChainInputValidator() into vote creation.
         */
        void SetSlotHashPopulator( ConsensusManager::SlotHashPopulator populator );

        void UnregisterSlotKeyHandler( std::string_view subject_type );

        /**
         * @brief Creates a consensus subject for nonce/transaction transition.
         * @param[in] account_id Account identifier.
         * @param[in] nonce Account nonce.
         * @param[in] tx_hash Transaction hash.
         * @param[in] transaction EmbeddedTransaction proto with typed oneof field set.
         * @param[in] utxo_commitment Optional UTXO commitment payload.
         * @param[in] utxo_witness Optional UTXO witness payload.
         * @return Constructed subject or an error.
         */
        static outcome::result<ConsensusManager::Subject> CreateConsensusNonceSubject(
            const std::string                             &account_id,
            uint64_t                                       nonce,
            const std::string                             &tx_hash,
            const EmbeddedTransaction                     &transaction,
            const std::optional<UTXOTransitionCommitment> &utxo_commitment,
            const std::optional<UTXOWitness>              &utxo_witness );

        /**
         * @brief Creates a signed proposal for nonce/transaction transition.
         * @param[in] account_id Account identifier.
         * @param[in] nonce Account nonce.
         * @param[in] tx_hash Transaction hash.
         * @param[in] transaction EmbeddedTransaction proto with typed oneof field set.
         * @param[in] utxo_commitment Optional UTXO commitment payload.
         * @param[in] utxo_witness Optional UTXO witness payload.
         * @return Constructed proposal or an error.
         */
        outcome::result<ConsensusManager::Proposal> CreateConsensusProposal(
            const std::string                             &account_id,
            uint64_t                                       nonce,
            const std::string                             &tx_hash,
            const EmbeddedTransaction                     &transaction,
            const std::optional<UTXOTransitionCommitment> &utxo_commitment,
            const std::optional<UTXOWitness>              &utxo_witness );

        /**
         * @brief Submits a proposal through consensus manager.
         * @param[in] proposal Proposal to submit.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> SubmitProposal( const ConsensusManager::Proposal &proposal );

        /**
         * @brief Attempts to resume deferred handling for a subject hash.
         * @param[in] hash Subject hash key.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> TryResumeProposal( const std::string &hash );
        /**
         * @brief Attempts to resume deferred handling for a typed pending dependency.
         * @param[in] dependency Dependency key that became available.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> TryResumePendingDependency( const ConsensusManager::PendingDependencyKey &dependency );
        /**
         * @brief Checks whether any certificate exists for subject hash.
         * @param[in] subject_hash Subject hash key.
         * @return `true` when certificate exists.
         */
        bool CheckCertificate( const std::string &subject_hash ) const;
        /**
         * @brief Performs strict certificate check for a specific subject object.
         * @param[in] subject Subject to evaluate.
         * @return `true` when certificate exists and matches strictly.
         */
        bool CheckCertificateStrict( const ConsensusManager::Subject &subject ) const;
        /**
         * @brief Loads certificate by subject hash.
         * @param[in] subject_hash Subject hash key.
         * @return Certificate on success, otherwise an error.
         */
        outcome::result<ConsensusManager::Certificate> GetCertificateBySubjectHash(
            const std::string &subject_hash ) const;

        /**
         * @brief Chooses the preferred hash among two candidates.
         * @param[in] a First hash candidate.
         * @param[in] b Second hash candidate.
         * @return Reference to selected hash.
         */
        static const std::string &BestHash( const std::string &a, const std::string &b );

    protected:
        friend class Migration3_5_0To3_6_0;
        friend class Migration3_6_0To3_7_0;
        friend class MultiAccountTestAccess;
        friend class CertificateFallbackTestAccess;

        /**
         * @brief Migrates blockchain-related CIDs between GlobalDB instances.
         * @param[in] old_db Source GlobalDB.
         * @param[in] new_db Target GlobalDB.
         * @return outcome::success on success, otherwise an error.
         */
        static outcome::result<void> MigrateCids( const std::shared_ptr<crdt::GlobalDB> &old_db,
                                                  const std::shared_ptr<crdt::GlobalDB> &new_db );

    private:
        /**
         * @brief Private constructor. Use @ref New.
         * @param[in] global_db CRDT database instance.
         * @param[in] account GeniusAccount instance.
         * @param[in] callback Initialization callback.
         */
        Blockchain( std::shared_ptr<crdt::GlobalDB> global_db,
                    std::shared_ptr<GeniusAccount>  account,
                    BlockchainCallback              callback );

        /**
         * @brief Initializes cached/stored genesis CID state.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> InitGenesisCID();
        /**
         * @brief Initializes account-creation CID state for account address.
         * @param[in] address Account address key.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> InitAccountCreationCID( const std::string &address );
        /**
         * @brief Persists selected genesis CID.
         * @param[in] cid Genesis CID.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> SaveGenesisCID( const std::string &cid );
        /**
         * @brief Persists selected account-creation CID for address.
         * @param[in] address Account address key.
         * @param[in] cid Account-creation CID.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> SaveAccountCreationCID( const std::string &address, const std::string &cid );

        /**
         * @brief Builds canonical bytes to verify/sign genesis block.
         * @param[in] g Genesis block.
         * @return Canonical signature bytes.
         */
        std::vector<uint8_t> ComputeSignatureData( const GenesisBlock &g ) const;
        /**
         * @brief Builds canonical bytes to verify/sign account-creation block.
         * @param[in] ac Account-creation block.
         * @return Canonical signature bytes.
         */
        std::vector<uint8_t> ComputeSignatureData( const AccountCreationBlock &ac ) const;
        /**
         * @brief Verifies genesis block signature.
         * @param[in] g Genesis block.
         * @return `true` when signature is valid.
         */
        bool VerifySignature( const GenesisBlock &g ) const;
        /**
         * @brief Verifies account-creation block signature.
         * @param[in] ac Account-creation block.
         * @return `true` when signature is valid.
         */
        bool VerifySignature( const AccountCreationBlock &ac ) const;

        /**
         * @brief Creates and publishes a genesis block when needed.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> CreateGenesisBlock();
        /**
         * @brief Verifies serialized genesis block payload.
         * @param[in] serialized_genesis Serialized genesis protobuf.
         * @return outcome::success when valid, otherwise an error.
         */
        outcome::result<void> VerifyGenesisBlock( const std::string &serialized_genesis );

        /**
         * @brief Creates and publishes an account-creation block when needed.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> CreateAccountCreationBlock();
        /**
         * @brief Verifies serialized account-creation block payload.
         * @param[in] serialized_account_creation Serialized account-creation protobuf.
         * @return outcome::success when valid, otherwise an error.
         */
        outcome::result<void> VerifyAccountCreationBlock( const std::string &serialized_account_creation );

        /**
         * @brief Filters CRDT elements to genesis-block entries.
         * @param[in] element Incoming CRDT element.
         * @return Filtered element vector, or `std::nullopt` when rejected.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterGenesis( const crdt::pb::Element &element );
        /**
         * @brief Filters CRDT elements to account-creation entries.
         * @param[in] element Incoming CRDT element.
         * @return Filtered element vector, or `std::nullopt` when rejected.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterAccountCreation( const crdt::pb::Element &element );
        /**
         * @brief Determines whether candidate genesis should replace existing one.
         * @param[in] existing Existing cached/selected genesis block.
         * @param[in] candidate Candidate genesis block.
         * @return `true` when candidate should replace existing.
         */
        bool ShouldReplaceGenesis( const GenesisBlock &existing, const GenesisBlock &candidate ) const;
        /**
         * @brief Determines whether candidate account block should replace existing.
         * @param[in] existing Existing cached/selected account block.
         * @param[in] candidate Candidate account block.
         * @return `true` when candidate should replace existing.
         */
        bool ShouldReplaceAccountCreation( const AccountCreationBlock &existing,
                                           const AccountCreationBlock &candidate ) const;

        /**
         * @brief Callback for incoming genesis CRDT updates.
         * @param[in] new_data New key/value pair from CRDT.
         * @param[in] cid CID associated with update.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> GenesisReceivedCallback( const crdt::CRDTCallbackManager::NewDataPair &new_data,
                                                       const std::string                            &cid );
        /**
         * @brief Callback for incoming account-creation CRDT updates.
         * @param[in] new_data New key/value pair from CRDT.
         * @param[in] cid CID associated with update.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> AccountCreationReceivedCallback( const crdt::CRDTCallbackManager::NewDataPair &new_data,
                                                               const std::string                            &cid );
        /**
         * @brief Delivers overall blockchain processing result to callback.
         * @param[in] result Processing result.
         * @return outcome::success when callback handling succeeds, otherwise an error.
         */
        outcome::result<void> InformBlockchainResult( outcome::result<void> result ) const;
        /**
         * @brief Processes/report result of genesis acquisition/creation.
         * @param[in] result Genesis CID result.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> InformGenesisResult( outcome::result<std::string> result );
        /**
         * @brief Processes/report result of account-creation acquisition/creation.
         * @param[in] creation_result Account-creation CID result.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> InformAccountCreationResponse( outcome::result<std::string> creation_result );
        /**
         * @brief Watches CID download completion with timeout handling.
         * @param[in] cid CID being tracked.
         * @param[in] error_on_failure Error code to emit on timeout/failure.
         * @param[in] timeout_ms Timeout in milliseconds.
         */
        void WatchCIDDownload( const std::string &cid, Error error_on_failure, uint64_t timeout_ms );
        /**
         * @brief Ensures validator registry is initialized and available.
         * @return outcome::success when registry is ready, otherwise an error.
         */
        outcome::result<void> EnsureValidatorRegistry() const;
        void                  RequestValidatorRegistry();

        static constexpr std::string_view BLOCKCHAIN_TOPIC =
            "gnus-blockchain"; ///< Topic used for blockchain CRDT data.
        static constexpr std::string_view DEFAULT_FULL_NODE_PUB_ADDRESS =
            "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2"; ///< Default authorized full-node public key.
        static constexpr std::string_view GENESIS_KEY =
            "gnus-genesis-block"; ///< Datastore key for genesis block payload.
        static constexpr std::string_view GENESIS_CID_KEY =
            "gnus-genesis-block-cid"; ///< Datastore key for selected genesis CID.
        static constexpr std::string_view ACCOUNT_CREATION_KEY_PREFIX =
            "gnus-account-creation-"; ///< Prefix for account-creation payload keys.
        static constexpr std::string_view ACCOUNT_CREATION_CID_KEY_PREFIX =
            "gnus-account-creation-cid-";                          ///< Prefix for account-creation CID keys.
        static constexpr uint64_t TIMEOUT_GENESIS_BLOCK_MS = 8000; ///< Genesis CID download timeout in milliseconds.
        static constexpr uint64_t TIMEOUT_ACC_CREATION_BLOCK_MS =
            8000; ///< Account-creation CID download timeout in milliseconds.

        std::shared_ptr<crdt::GlobalDB> db_;      ///< CRDT database instance
        std::shared_ptr<GeniusAccount>  account_; ///< GeniusAccount instance

        BlockchainCallback   blockchain_processed_callback_; ///< Callback when the processing of the blockchain is done
        GenesisBlock         genesis_block_;                 ///< Cached genesis block for easy access
        AccountCreationBlock account_creation_block_;        ///< Cached account creation block

        struct BlockchainCIDs
        {
            std::optional<std::string> genesis_; ///< Selected genesis CID.
            std::unordered_map<std::string, std::string>
                account_creation_; ///< Selected account-creation CIDs keyed by address.

            /**
             * @brief Checks whether a genesis CID is available.
             * @return `true` when genesis CID exists.
             */
            bool hasGenesis() const
            {
                return genesis_.has_value();
            }

            /**
             * @brief Checks whether an address has account-creation CID.
             * @param[in] address Account address key.
             * @return `true` when address entry exists.
             */
            bool hasAccount( const std::string &address ) const
            {
                return account_creation_.find( address ) != account_creation_.end();
            }

            /**
             * @brief Checks whether any account-creation CID exists.
             * @return `true` when at least one account entry exists.
             */
            bool hasAnyAccount() const
            {
                return !account_creation_.empty();
            }

            /**
             * @brief Checks whether both genesis and account CID exist for address.
             * @param[in] address Account address key.
             * @return `true` when both required CIDs are present.
             */
            bool isCompleteFor( const std::string &address ) const
            {
                return hasGenesis() && hasAccount( address );
            }
        };

        BlockchainCIDs cids_; ///< Cached CID selection state.

        /**
         * @brief Returns mutable process-wide storage for authorized full-node pub key.
         * @return Reference to static storage string.
         */
        static std::string &AuthorizedFullNodeAddressStorage();

        /**
         * @brief Returns mutable process-wide storage for additional genesis validator addresses.
         * @return Reference to static storage vector.
         */
        static std::vector<std::string> &AdditionalGenesisValidatorAddressesStorage();

        std::shared_ptr<ValidatorRegistry> validator_registry_; ///< Validator registry component.

        base::Logger logger_ = base::createLogger( "Blockchain" ); ///< Logger instance

        std::atomic<bool> stop_started_{ false };                   ///< Makes account-bound teardown one-shot.
        std::atomic<bool> validator_registry_initialized_{ false }; ///< Signals registry initialization completion.
        std::atomic<bool> start_deferred_{
            false }; ///< Start() returned BLOCKCHAIN_NOT_INITIALIZED; retry once the registry is ready.
        bool genesis_ready_          = false; ///< Indicates genesis block is ready.
        bool account_creation_ready_ = false; ///< Indicates account-creation block is ready.

        std::shared_ptr<ConsensusManager> consensus_manager_; ///< Consensus manager used for proposals/certificates.
    };

}

/**
 * @brief       Macro for declaring error handling in the IBasicProof class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns, Blockchain::Error );

#endif // SGNS_BLOCKCHAIN_HPP

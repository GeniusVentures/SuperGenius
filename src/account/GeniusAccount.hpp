/**
 * @file       GeniusAccount.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_ACCOUNT_HPP_
#define _GENIUS_ACCOUNT_HPP_
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <tuple>

#include <ProofSystem/ElGamalKeyGenerator.hpp>
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include "account/AccountMessenger.hpp"
#include "account/TokenID.hpp"
#include "outcome/outcome.hpp"


namespace sgns
{
    using namespace boost::multiprecision;
    class AccountManager;

    class GeniusAccount : public std::enable_shared_from_this<GeniusAccount>
    {
    public:
        static const std::array<uint8_t, 32> ELGAMAL_PUBKEY_PREDEFINED;

        static std::shared_ptr<GeniusAccount> New( TokenID          token_id,
                                                   std::string_view base_path,
                                                   const char      *eth_private_key );
        bool InitMessenger( std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub, bool full_node );

        ~GeniusAccount();

        [[nodiscard]] std::string GetAddress() const;

        template <typename T>
        [[nodiscard]] T GetBalance() const;

        uint64_t GetBalance( const TokenID token_id ) const;

        [[nodiscard]] TokenID GetToken() const;

        [[nodiscard]] std::string GetNonce() const
        {
            return std::to_string( confirmed_nonce_ );
        }

        bool PutUTXO( const GeniusUTXO &new_utxo );

        bool RefreshUTXOs( const std::vector<InputUTXOInfo> &infos );

        static bool          VerifySignature( std::string address, std::string sig, std::vector<uint8_t> data );
        std::vector<uint8_t> Sign( std::vector<uint8_t> data );

        void     SetLocalConfirmedNonce( uint64_t nonce );
        uint64_t GetLocalConfirmedNonce();

        outcome::result<uint64_t> GetConfirmedNonce( uint64_t timeout_ms );
        uint64_t                  GetProposedNonce();
        void                      IncProposedNonce();

        TokenID                 token;
        std::vector<GeniusUTXO> utxos;

    private:
        std::shared_ptr<ethereum::EthereumKeyGenerator> eth_keypair;
        std::shared_ptr<KeyGenerator::ElGamal>          elgamal_address;
        int64_t                                         confirmed_nonce_;
        uint64_t                                        proposed_nonce_;
        std::mutex                                      nonce_mutex_;
        std::shared_ptr<AccountMessenger>               messenger_;

        GeniusAccount( TokenID token_id );

        static outcome::result<std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>> GenerateGeniusAddress(
            std::string_view base_path,
            const char      *eth_private_key );
    };
}

#endif

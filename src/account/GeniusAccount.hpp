/**
 * @file       GeniusAccount.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_ACCOUNT_HPP_
#define _GENIUS_ACCOUNT_HPP_
#include <string>
#include <cstdint>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/fwd.hpp>

#include <ProofSystem/ElGamalKeyGenerator.hpp>
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include "outcome/outcome.hpp"
#include <vector>
#include <array>

namespace sgns
{
    using namespace boost::multiprecision;

    class GeniusAccount
    {
    public:
        static const std::array<uint8_t, 32> ELGAMAL_PUBKEY_PREDEFINED;

        GeniusAccount( TokenID token_id, std::string_view base_path, const char *eth_private_key );

        ~GeniusAccount();

        [[nodiscard]] std::string GetAddress() const;

        template <typename T>
        [[nodiscard]] T GetBalance() const;

        uint64_t GetBalance( const TokenID token_id ) const;

        [[nodiscard]] TokenID GetToken() const;

        [[nodiscard]] std::string GetNonce() const
        {
            return std::to_string( nonce );
        }

        bool PutUTXO( const GeniusUTXO &new_utxo );

        bool RefreshUTXOs( const std::vector<InputUTXOInfo> &infos );

        static bool          VerifySignature( std::string address, std::string sig, std::vector<uint8_t> data );
        static std::vector<uint8_t> Sign(std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key, std::vector<uint8_t> data );
        std::vector<uint8_t> Sign( std::vector<uint8_t> data );

        TokenID                                         token;
        uint64_t                                        nonce;
        std::vector<GeniusUTXO>                         utxos;
        std::shared_ptr<ethereum::EthereumKeyGenerator> eth_keypair;

    private:
        std::shared_ptr<KeyGenerator::ElGamal> elgamal_address;

        static outcome::result<std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>> GenerateGeniusAddress(
            std::string_view base_path,
            const char      *eth_private_key );
    };
}

#endif

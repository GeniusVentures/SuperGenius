/**
 * @file gossip_auth.hpp
 *
 * @brief Application-layer payload authentication for private-network gossip
 *        (CR-G01).
 *
 * The vendored gossip wire Message carries signature/key fields, but the
 * subscriber-facing type Gossip::Message exposes ONLY {from, topic, data}
 * (thirdparty/libp2p .../protocol/gossip/gossip.hpp:129-135) and the vendored
 * receive/forward path performs zero signature verification -- so those
 * gossip-layer fields are NOT reachable by any SGNUS subscriber and are NOT
 * consumed by any SGNUS gate. The owner-sanctioned equivalent authenticated
 * mapping is this application-layer envelope:
 *
 *   - Publishers seal gossip payloads with the SAME libp2p keypair that
 *     constructs their gossip host, so PeerId::fromPublicKey(embedded key)
 *     equals the from-field the vendored gossip stamps (GossipCore::publish
 *     sets from = local_peer_id_ unconditionally) and equals the identity
 *     carried in NetworkRegistry membership records.
 *   - Gates verify the envelope BEFORE consulting membership: unmarshal the
 *     embedded public key, check PeerId::fromPublicKey(key) equals
 *     PeerId::fromBytes(from), and verify the signature over the canonical
 *     signable bytes. ANY failure = deny (fail-closed, mirroring the
 *     empty-from denial of 15-11/15-13).
 *
 * Envelope wire format (all integers big-endian):
 *
 *   offset  size  field
 *   0       12    ASCII magic prefix (kGossipAuthEnvelopeMagic)
 *   12      4     marshaled-public-key length N
 *   16      N     marshaled protobuf public key (Ed25519 etc.)
 *   16+N    4     signature length S
 *   20+N    S     signature over the canonical signable bytes
 *   20+N+S  *     payload remainder
 *
 * Canonical signable bytes = magic prefix + u32be(from length) + from bytes +
 * payload. The signature covers the payload AND the transport from-field, so a
 * sealed payload replayed under a different from (or any payload byte flip)
 * fails verification.
 *
 * Sealing changes the wire format ONLY inside private networks: callers seal
 * exactly when a membership filter is installed; public nodes publish raw and
 * pass through raw (byte-identical). The envelope intentionally carries no
 * topic/freshness binding (residual accepted, T-15-14-04): gossip message-id
 * dedup bounds naive replay and downstream protobuf/state machines carry
 * their own ids.
 *
 * Header-only: no networkregistry/ and no crdt/ dependencies -- the gates in
 * crdt and processing include only this header.
 */

#ifndef SUPERGENIUS_BASE_GOSSIP_AUTH_HPP
#define SUPERGENIUS_BASE_GOSSIP_AUTH_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gsl/span>
#include <libp2p/common/byteutil.hpp>
#include <libp2p/crypto/crypto_provider.hpp>
#include <libp2p/crypto/crypto_provider/crypto_provider_impl.hpp>
#include <libp2p/crypto/ecdsa_provider/ecdsa_provider_impl.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>
#include <libp2p/crypto/hmac_provider/hmac_provider_impl.hpp>
#include <libp2p/crypto/key.hpp>
#include <libp2p/crypto/key_marshaller.hpp>
#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/crypto/key_validator.hpp>
#include <libp2p/crypto/key_validator/key_validator_impl.hpp>
#include <libp2p/crypto/protobuf/protobuf_key.hpp>
#include <libp2p/crypto/random_generator/boost_generator.hpp>
#include <libp2p/crypto/rsa_provider/rsa_provider_impl.hpp>
#include <libp2p/crypto/secp256k1_provider/secp256k1_provider_impl.hpp>
#include <libp2p/outcome/outcome.hpp>
#include <libp2p/peer/peer_id.hpp>

namespace sgns::base
{
    /// 12-byte ASCII magic prefix identifying an authenticated gossip envelope.
    inline constexpr std::array<uint8_t, 12> kGossipAuthEnvelopeMagic = {
        'S', 'G', 'N', 'S', 'G', 'O', 'S', 'S', 'I', 'P', '0', '1' };

    /// Failure kinds reported by OpenGossipPayload / SealGossipPayload.
    enum class GossipPayloadAuthError
    {
        NOT_AN_ENVELOPE = 1, ///< wire data carries no envelope magic (raw public payload)
        MALFORMED_ENVELOPE,  ///< truncated header/lengths or unparseable embedded public key
        KEY_FROM_MISMATCH,   ///< PeerId::fromPublicKey(embedded key) != PeerId::fromBytes(from)
        SIGNATURE_INVALID,   ///< signature does not verify over the canonical signable bytes
        SEAL_FAILED,         ///< sealing-side failure (marshal/sign)
        DERIVE_FAILED,       ///< from-bytes derivation failure (marshal/PeerId)
    };

    /// Result type carrying the local error enum. The terminate policy avoids
    /// instantiating outcome's exception-throw path (which only supports
    /// std::error_code error types); every caller MUST check has_error()
    /// before value() -- all gate call sites do.
    template <typename T>
    using GossipAuthResult =
        libp2p::outcome::result<T, GossipPayloadAuthError, libp2p::outcome::policy::terminate>;

    /// Successful result of OpenGossipPayload: the AUTHENTICATED sender
    /// identity (derived from the embedded public key AND equal to the
    /// from-field PeerId) plus a view of the inner payload bytes.
    struct OpenedGossipPayload
    {
        libp2p::peer::PeerId authenticated_peer;
        gsl::span<const uint8_t> payload; ///< points into the wire data passed to OpenGossipPayload
    };

    namespace detail
    {
        /// std::string -> byte-span convenience for string-serialized
        /// protobuf payloads handed to SealGossipPayload.
        inline gsl::span<const uint8_t> StringSpan( const std::string &s )
        {
            return gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( s.data() ), s.size() );
        }

        /// Crypto provider instantiation recipe copied from
        /// crdt/globaldb/keypair_file_storage.cpp:20-52.
        inline std::shared_ptr<libp2p::crypto::CryptoProviderImpl> MakeGossipAuthProvider()
        {
            return std::make_shared<libp2p::crypto::CryptoProviderImpl>(
                std::make_shared<libp2p::crypto::random::BoostRandomGenerator>(),
                std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>(),
                std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>(),
                std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>(),
                std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>(),
                std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>() );
        }

        /// Function-local singletons (thread-safe magic statics);
        /// sign/verify/marshal are const operations on the underlying providers.
        inline libp2p::crypto::CryptoProvider &GossipAuthCryptoProvider()
        {
            static const auto kProvider = MakeGossipAuthProvider();
            return *kProvider;
        }

        inline libp2p::crypto::marshaller::KeyMarshaller &GossipAuthKeyMarshaller()
        {
            static const auto kValidator =
                std::make_shared<libp2p::crypto::validator::KeyValidatorImpl>( MakeGossipAuthProvider() );
            static libp2p::crypto::marshaller::KeyMarshallerImpl kMarshaller{ kValidator };
            return kMarshaller;
        }

        inline void AppendU32Be( std::vector<uint8_t> &out, uint32_t value )
        {
            out.push_back( static_cast<uint8_t>( value >> 24 ) );
            out.push_back( static_cast<uint8_t>( value >> 16 ) );
            out.push_back( static_cast<uint8_t>( value >> 8 ) );
            out.push_back( static_cast<uint8_t>( value ) );
        }

        /// Reads a u32 big-endian at \p offset, advancing it by 4. Fails when
        /// fewer than 4 bytes remain.
        inline bool ReadU32Be( gsl::span<const uint8_t> data, size_t &offset, uint32_t &value )
        {
            const size_t size = static_cast<size_t>( data.size() );
            if ( size < offset || size - offset < 4 )
            {
                return false;
            }
            value = ( static_cast<uint32_t>( data[offset] ) << 24 )
                  | ( static_cast<uint32_t>( data[offset + 1] ) << 16 )
                  | ( static_cast<uint32_t>( data[offset + 2] ) << 8 )
                  | static_cast<uint32_t>( data[offset + 3] );
            offset += 4;
            return true;
        }

        /// Canonical signable bytes: magic + u32be(from length) + from + payload.
        inline std::vector<uint8_t> BuildSignableBytes( gsl::span<const uint8_t> from_bytes,
                                                        gsl::span<const uint8_t> payload )
        {
            std::vector<uint8_t> signable;
            signable.reserve( kGossipAuthEnvelopeMagic.size() + 4 + from_bytes.size() + payload.size() );
            signable.insert( signable.end(), kGossipAuthEnvelopeMagic.begin(), kGossipAuthEnvelopeMagic.end() );
            AppendU32Be( signable, static_cast<uint32_t>( from_bytes.size() ) );
            signable.insert( signable.end(), from_bytes.begin(), from_bytes.end() );
            signable.insert( signable.end(), payload.begin(), payload.end() );
            return signable;
        }
    } // namespace detail

    /// @brief Derives the transport from-bytes a publisher sealing with
    ///        \p keypair must present: PeerId::fromPublicKey(marshalled public
    ///        key).toVector(). Call once and reuse -- this equals the
    ///        local_peer_id_ the vendored gossip stamps into from at publish.
    inline GossipAuthResult<libp2p::common::ByteArray> DeriveGossipFromBytes(
        const libp2p::crypto::KeyPair &keypair )
    {
        auto marshalled = detail::GossipAuthKeyMarshaller().marshal( keypair.publicKey );
        if ( marshalled.has_error() )
        {
            return GossipPayloadAuthError::DERIVE_FAILED;
        }
        auto peer_id = libp2p::peer::PeerId::fromPublicKey( marshalled.value() );
        if ( peer_id.has_error() )
        {
            return GossipPayloadAuthError::DERIVE_FAILED;
        }
        return outcome::success( peer_id.value().toVector() );
    }

    /// @brief Seals \p payload into an authenticated envelope signed with
    ///        \p keypair's private key (CR-G01 publisher side).
    /// @param keypair    Gossip-host keypair (the same one that constructed
    ///                   the GossipPubSub host).
    /// @param from_bytes Publisher's own from-bytes -- MUST equal
    ///                   DeriveGossipFromBytes(keypair) or receivers will
    ///                   reject the binding.
    /// @param payload    Serialized application payload to seal.
    /// @return Envelope bytes to publish, or SEAL_FAILED on marshal/sign error.
    inline GossipAuthResult<libp2p::common::ByteArray> SealGossipPayload(
        const libp2p::crypto::KeyPair &keypair,
        gsl::span<const uint8_t>       from_bytes,
        gsl::span<const uint8_t>       payload )
    {
        auto marshalled_key = detail::GossipAuthKeyMarshaller().marshal( keypair.publicKey );
        if ( marshalled_key.has_error() )
        {
            return GossipPayloadAuthError::SEAL_FAILED;
        }

        const auto signable = detail::BuildSignableBytes( from_bytes, payload );
        auto signature = detail::GossipAuthCryptoProvider().sign( signable, keypair.privateKey );
        if ( signature.has_error() )
        {
            return GossipPayloadAuthError::SEAL_FAILED;
        }

        const auto &key_bytes = marshalled_key.value().key;

        std::vector<uint8_t> envelope;
        envelope.reserve( kGossipAuthEnvelopeMagic.size() + 4 + key_bytes.size() + 4
                          + signature.value().size() + payload.size() );
        envelope.insert( envelope.end(), kGossipAuthEnvelopeMagic.begin(), kGossipAuthEnvelopeMagic.end() );
        detail::AppendU32Be( envelope, static_cast<uint32_t>( key_bytes.size() ) );
        envelope.insert( envelope.end(), key_bytes.begin(), key_bytes.end() );
        detail::AppendU32Be( envelope, static_cast<uint32_t>( signature.value().size() ) );
        envelope.insert( envelope.end(), signature.value().begin(), signature.value().end() );
        envelope.insert( envelope.end(), payload.begin(), payload.end() );
        return outcome::success( std::move( envelope ) );
    }

    /// @brief Verifies an authenticated gossip envelope (CR-G01 gate side).
    ///
    /// Check order (every failure denies under a set membership filter):
    ///   1. magic present -- else NOT_AN_ENVELOPE (raw public payload; a set
    ///      filter treats this as deny, fail-closed);
    ///   2. lengths parse and the embedded public key unmarshals -- else
    ///      MALFORMED_ENVELOPE;
    ///   3. PeerId::fromPublicKey(embedded key) == PeerId::fromBytes(from)
    ///      (an empty/malformed from also fails here) -- else
    ///      KEY_FROM_MISMATCH: a same-PSK peer forging from=<member> cannot
    ///      pass, because the embedded key does not derive that PeerId;
    ///   4. signature verifies over the recomputed signable bytes -- else
    ///      SIGNATURE_INVALID: covers payload AND from, so tampering either
    ///      fails.
    ///
    /// @param from_bytes Transport gossip from-field (wire-supplied).
    /// @param wire_data  Message payload as received (envelope or raw).
    /// @return OpenedGossipPayload with the authenticated PeerId (equal to
    ///         the from PeerId) and the inner payload view.
    inline GossipAuthResult<OpenedGossipPayload> OpenGossipPayload(
        gsl::span<const uint8_t> from_bytes,
        gsl::span<const uint8_t> wire_data )
    {
        if ( static_cast<size_t>( wire_data.size() ) < kGossipAuthEnvelopeMagic.size()
             || !std::equal( kGossipAuthEnvelopeMagic.begin(),
                             kGossipAuthEnvelopeMagic.end(),
                             wire_data.begin() ) )
        {
            return GossipPayloadAuthError::NOT_AN_ENVELOPE;
        }

        size_t offset = kGossipAuthEnvelopeMagic.size();
        const size_t wire_size = static_cast<size_t>( wire_data.size() );
        uint32_t key_length = 0;
        uint32_t signature_length = 0;
        if ( !detail::ReadU32Be( wire_data, offset, key_length )
             || wire_size - offset < static_cast<size_t>( key_length ) )
        {
            return GossipPayloadAuthError::MALFORMED_ENVELOPE;
        }
        const auto key_bytes = wire_data.subspan( offset, static_cast<size_t>( key_length ) );
        offset += static_cast<size_t>( key_length );
        if ( !detail::ReadU32Be( wire_data, offset, signature_length )
             || wire_size - offset < static_cast<size_t>( signature_length ) )
        {
            return GossipPayloadAuthError::MALFORMED_ENVELOPE;
        }
        const auto signature_bytes = wire_data.subspan( offset, static_cast<size_t>( signature_length ) );
        offset += static_cast<size_t>( signature_length );
        const auto payload = wire_data.subspan( offset );

        std::vector<uint8_t> protobuf_key_bytes( key_bytes.begin(), key_bytes.end() );
        auto public_key = detail::GossipAuthKeyMarshaller().unmarshalPublicKey(
            libp2p::crypto::ProtobufKey{ std::move( protobuf_key_bytes ) } );
        if ( public_key.has_error() )
        {
            return GossipPayloadAuthError::MALFORMED_ENVELOPE;
        }

        // Key <-> from binding (the CR-G01 core): the embedded public key must
        // derive exactly the PeerId the wire from-field names.
        auto key_peer_id = libp2p::peer::PeerId::fromPublicKey(
            libp2p::crypto::ProtobufKey{ std::vector<uint8_t>( key_bytes.begin(), key_bytes.end() ) } );
        auto from_peer_id = libp2p::peer::PeerId::fromBytes( from_bytes );
        if ( key_peer_id.has_error() || from_peer_id.has_error()
             || key_peer_id.value() != from_peer_id.value() )
        {
            return GossipPayloadAuthError::KEY_FROM_MISMATCH;
        }

        const auto signable = detail::BuildSignableBytes( from_bytes, payload );
        auto verified = detail::GossipAuthCryptoProvider().verify( signable, signature_bytes, public_key.value() );
        if ( verified.has_error() || !verified.value() )
        {
            return GossipPayloadAuthError::SIGNATURE_INVALID;
        }

        return outcome::success( OpenedGossipPayload{ key_peer_id.value(), payload } );
    }

} // namespace sgns::base

#endif // SUPERGENIUS_BASE_GOSSIP_AUTH_HPP

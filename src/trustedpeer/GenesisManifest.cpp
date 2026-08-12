#include "trustedpeer/GenesisManifest.hpp"

namespace sgns::trustedpeer
{
    std::optional<GenesisManifest> GenesisManifest::Canonicalized() const { return std::nullopt; }
    std::optional<std::vector<uint8_t>> GenesisManifest::CanonicalBytes() const { return std::nullopt; }
    std::optional<std::string> GenesisManifest::Fingerprint() const { return std::nullopt; }
    std::optional<GenesisManifest> GenesisManifest::DecodeCanonical( const std::vector<uint8_t> & )
    {
        return std::nullopt;
    }
    std::optional<GenesisManifest> GenesisManifest::DecodeAndVerify( const std::vector<uint8_t> &,
                                                                     const std::string & )
    {
        return std::nullopt;
    }
    bool GenesisManifest::operator==( const GenesisManifest & ) const { return false; }
} // namespace sgns::trustedpeer

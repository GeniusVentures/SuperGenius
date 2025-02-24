

#include "crypto/sha/sha256.hpp"

#include <openssl/evp.h>

namespace sgns::crypto
{
    base::Hash256 sha256(std::string_view input) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto *bytes_ptr = reinterpret_cast<const uint8_t *>(input.data());
        return sha256(gsl::make_span(bytes_ptr, input.length()));
    }

    base::Hash256 sha256(gsl::span<const uint8_t> input) {
        base::Hash256 out;
        unsigned int digest_len = 0;

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(ctx, input.data(), input.size());
        EVP_DigestFinal_ex(ctx, out.data(), &digest_len);
        EVP_MD_CTX_free(ctx);

        return out;
    }
} // namepace sgns::crypto

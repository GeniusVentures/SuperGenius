#ifndef SUPERGENIUS_SRC_CRYPTO_RANDOM_GENERATOR_BOOST_GENERATOR_HPP
#define SUPERGENIUS_SRC_CRYPTO_RANDOM_GENERATOR_BOOST_GENERATOR_HPP

#include <libp2p/crypto/random_generator/boost_generator.hpp>

namespace sgns::crypto
{
    using BoostRandomGenerator = libp2p::crypto::random::BoostRandomGenerator;
    using CSPRNG               = libp2p::crypto::random::CSPRNG;
}

#endif

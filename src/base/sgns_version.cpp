/**
 * @file sgns_version.cpp
 * @brief Implementation of version retrieval functions for SuperGenius.
 *
 * This file provides functions to retrieve version information for SuperGenius.
 * All version-related macros (e.g. SUPERGENIUS_VERSION_NUMBER) are defined via CMake.
 *
 */
#include <string>
#include "base/sgnsv.h"
#include "sgns_version.hpp"

uint64_t sgns::version::SuperGeniusVersionNum()
{
    return SUPERGENIUS_VERSION_NUMBER;
}

uint32_t sgns::version::SuperGeniusVersionMajor()
{
    return SUPERGENIUS_VERSION_MAJOR;
}

uint32_t sgns::version::SuperGeniusVersionMinor()
{
    return SUPERGENIUS_VERSION_MINOR;
}

uint32_t sgns::version::SuperGeniusVersionPatch()
{
    return SUPERGENIUS_VERSION_PATCH;
}

std::string sgns::version::SuperGeniusVersionString()
{
    return SUPERGENIUS_VERSION_STR;
}

std::string sgns::version::SuperGeniusVersionFullString()
{
    return SUPERGENIUS_FULL_VERSION_STR;
}

std::string sgns::version::SuperGeniusVersionText()
{
    return SUPERGENIUS_VERSION_TEXT;
}

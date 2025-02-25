/**
 * @file sgns_version.cpp
 * @brief Implementation of version retrieval functions for SuperGenius.
 *
 * This file provides functions to retrieve version information for SuperGenius.
 * All version-related macros (e.g. SUPERGENIUS_VERSION_NUMBER) are defined via CMake.
 *
 */
#include "sgnsv.h"
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

const char *sgns::version::SuperGeniusVersionPreRelease()
{
    return SUPERGENIUS_VERSION_PRE_RELEASE;
}

const char *sgns::version::SuperGeniusVersionBuildMetadata()
{
    return SUPERGENIUS_VERSION_BUILD_METADATA;
}
const char *sgns::version::SuperGeniusVersionText()
{
    return SUPERGENIUS_VERSION_TEXT;
}

/**
 * @file sgns_version.cpp
 * @brief Implementation of version retrieval functions for SuperGenius.
 *
 * This file provides functions to retrieve version information for SuperGenius.
 * All version-related macros (e.g. SUPERGENIUS_VERSION_NUMBER) are defined via CMake.
 *
 */
#include <string>
#include <boost/format.hpp>
#include "base/sgnsv.h"
#include "sgns_version.hpp"

static uint16_t network_id_ = sgns::version::DEV_NET_ID; // Default to DEV_NET_ID

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

uint32_t sgns::version::ProcessingVersion()
{
    return SUPERGENIUS_PROCESSING_VERSION;
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

uint16_t sgns::version::GetNetworkID()
{
    return network_id_;
}

std::string sgns::version::GetNetAndVersionAppendix()
{
    return GetNetAndVersionAppendix( sgns::version::SuperGeniusVersionMajor(),
                                     sgns::version::SuperGeniusVersionMinor(),
                                     sgns::version::GetNetworkID() );
}

std::string sgns::version::GetNetAndVersionAppendix( uint32_t version_major, uint32_t version_minor, uint16_t net_id )
{
    return ( boost::format( std::string( sgns::version::SGNS_VERSION_APPENDIX ) ) % version_major % version_minor )
               .str() +
           ( boost::format( std::string( sgns::version::NET_ID_APPENDIX ) ) % net_id ).str();
}

void sgns::version::SetNetworkId( uint16_t net_id )
{
    if ( net_id == MAIN_NET_ID || net_id == TEST_NET_ID || net_id == DEV_NET_ID )
    {
        network_id_ = net_id;
    }
    else
    {
        throw std::invalid_argument( "Invalid network ID" );
    }
}

#include "Apple.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <iostream>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace rj = rapidjson;

namespace sgns
{
    outcome::result<rapidjson::Document> AppleSecureStorage::LoadJSON() const
    {
        CFMutableDictionaryRef query = CFDictionaryCreateMutable( kCFAllocatorDefault,
                                                                  0,
                                                                  &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks );
        CFDictionarySetValue( query, kSecClass, kSecClassGenericPassword );
        CFDictionarySetValue( query,
                              kSecAttrService,
                              CFStringCreateWithCString( kCFAllocatorDefault, "SuperGenius", kCFStringEncodingUTF8 ) );
        CFDictionarySetValue( query,
                              kSecAttrAccount,
                              CFStringCreateWithCString( kCFAllocatorDefault, "SuperGenius", kCFStringEncodingUTF8 ) );
        CFDictionarySetValue( query, kSecReturnData, kCFBooleanTrue );
        CFDictionarySetValue( query, kSecMatchLimit, kSecMatchLimitOne );

        CFDataRef data   = nullptr;
        auto      status = SecItemCopyMatching( query, reinterpret_cast<CFTypeRef *>( &data ) );

        rj::Document d( rj::Type::kObjectType );

        if ( status == errSecSuccess )
        {
            d.Parse( reinterpret_cast<const char *>( CFDataGetBytePtr( data ) ), CFDataGetLength( data ) );
            CFRelease( data );
        }
        CFRelease( query );

        if ( status == errSecSuccess || status == errSecItemNotFound )
        {
            return d;
        }
        return outcome::failure( std::errc::bad_message );
    }

    outcome::result<void> AppleSecureStorage::SaveJSON( rapidjson::Document document )
    {
        rj::StringBuffer password;
        rj::Writer       writer( password );
        document.Accept( writer );

        CFDataRef binaryData = CFDataCreate( kCFAllocatorDefault,
                                             reinterpret_cast<const UInt8 *>( password.GetString() ),
                                             password.GetLength() );

        CFMutableDictionaryRef query = CFDictionaryCreateMutable( kCFAllocatorDefault,
                                                                  0,
                                                                  &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks );

        CFDictionarySetValue( query, kSecClass, kSecClassGenericPassword );
        CFDictionarySetValue( query,
                              kSecAttrService,
                              CFStringCreateWithCString( kCFAllocatorDefault, "SuperGenius", kCFStringEncodingUTF8 ) );
        CFDictionarySetValue( query,
                              kSecAttrAccount,
                              CFStringCreateWithCString( kCFAllocatorDefault, "SuperGenius", kCFStringEncodingUTF8 ) );

        CFMutableDictionaryRef addQuery = CFDictionaryCreateMutableCopy( kCFAllocatorDefault, 0, query );
        CFDictionarySetValue( addQuery, kSecValueData, binaryData );

        auto status = SecItemAdd( addQuery, nullptr );

        if ( status == errSecDuplicateItem )
        {
            CFMutableDictionaryRef updateQuery = CFDictionaryCreateMutable( kCFAllocatorDefault,
                                                                            0,
                                                                            &kCFTypeDictionaryKeyCallBacks,
                                                                            &kCFTypeDictionaryValueCallBacks );
            CFDictionarySetValue( updateQuery, kSecValueData, binaryData );

            status = SecItemUpdate( query, updateQuery );
            CFRelease( updateQuery );
        }

        CFRelease( binaryData );
        CFRelease( addQuery );
        CFRelease( query );

        if ( status == errSecSuccess )
        {
            return outcome::success();
        }
        return outcome::failure( std::errc::bad_message );
    }
}

#include "Android.hpp"

#include <cstddef>
#include <rapidjson/document.h>
#include <stdexcept>

#include <android/log.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "outcome/outcome.hpp"

#define LOG_TAG "AndroidSecureStorage"
#define LOGI( ... ) __android_log_print( ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__ )
#define LOGE( ... ) __android_log_print( ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__ )

namespace
{
    JavaVM *g_jvm = nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad( JavaVM *vm, void *_reserved )
{
    LOGI( "SuperGenius SDK initializing" );

    g_jvm = vm;

    JNIEnv *env = nullptr;
    if ( vm->GetEnv( reinterpret_cast<void **>( &env ), JNI_VERSION_1_6 ) != JNI_OK )
    {
        LOGE( "Failed to get JNI environment" );
        return JNI_ERR;
    }

    LOGI( "SuperGenius SDK initialized successfully" );
    return JNI_VERSION_1_6;
}

namespace rj = rapidjson;

namespace sgns
{
    AndroidSecureStorage::AndroidSecureStorage( JavaVM *jvm ) : jvm_( jvm )
    {
        if ( jvm_ == nullptr )
        {
            if ( g_jvm == nullptr )
            {
                throw std::runtime_error( "Could not find JVM pointer" );
            }
            jvm_ = g_jvm;
        }

        JNIEnv *env = GetJNIEnv();
        if ( env == nullptr )
        {
            throw std::runtime_error( "Failed to get JNI environment" );
        }

        jclass local_class = env->FindClass( "ai/gnus/sdk/KeyStoreHelper" );
        if ( local_class == nullptr )
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
            throw std::runtime_error( "Failed to find KeyStoreHelper class" );
        }

        key_store_helper_class_ = static_cast<jclass>( env->NewGlobalRef( local_class ) );
        env->DeleteLocalRef( local_class );

        // Get STATIC method IDs (note the "Static" in the function names)
        load_method_   = env->GetStaticMethodID( key_store_helper_class_, "load", "()Ljava/lang/String;" );
        save_method_   = env->GetStaticMethodID( key_store_helper_class_, "save", "(Ljava/lang/String;)Z" );
        delete_method_ = env->GetStaticMethodID( key_store_helper_class_, "delete", "(Ljava/lang/String;)Z" );

        if ( load_method_ == nullptr || save_method_ == nullptr || delete_method_ == nullptr )
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
            throw std::runtime_error( "Failed to find KeyStoreHelper methods" );
        }

        LOGI( "AndroidSecureStorage initialized successfully" );
    }

    AndroidSecureStorage::~AndroidSecureStorage()
    {
        auto env = GetJNIEnv();

        if ( env != nullptr )
        {
            if ( key_store_helper_class_ != nullptr )
            {
                env->DeleteGlobalRef( key_store_helper_class_ );
            }
        }
    }

    outcome::result<rapidjson::Document> AndroidSecureStorage::LoadJSON() const
    {
        auto *env = GetJNIEnv();
        if ( env == nullptr )
        {
            return outcome::failure( std::errc::not_connected );
        }

        auto result = static_cast<jstring>( env->CallStaticObjectMethod( key_store_helper_class_, load_method_ ) );

        if ( env->ExceptionCheck() != 0U )
        {
            LOGE( "Failed to call load method from KeyStore" );
            env->ExceptionDescribe();
            env->ExceptionClear();
            return outcome::failure( std::errc::connection_aborted );
        }

        if ( result == nullptr )
        {
            return rj::Document( rj::Type::kObjectType );
        }

        const char *data = env->GetStringUTFChars( result, nullptr );
        if ( data == nullptr )
        {
            LOGE( "Failed to get message from KeyStore's response" );
            env->DeleteLocalRef( result );
            return outcome::failure( std::errc::bad_message );
        }

        rj::Document d;
        d.Parse( data );

        env->ReleaseStringUTFChars( result, data );
        env->DeleteLocalRef( result );

        if ( d.HasParseError() || ( !d.IsObject() && !d.Empty() ) )
        {
            LOGE( "Failed to parse JSON document" );
            return outcome::failure( std::errc::bad_message );
        }

        return d;
    }

    outcome::result<void> AndroidSecureStorage::SaveJSON( rapidjson::Document document )
    {
        JNIEnv *env = GetJNIEnv();
        if ( env == nullptr )
        {
            return outcome::failure( std::errc::not_connected );
        }

        rj::StringBuffer             buffer;
        rj::Writer<rj::StringBuffer> writer( buffer );
        document.Accept( writer );

        jstring jdata = env->NewStringUTF( buffer.GetString() );
        if ( jdata == nullptr )
        {
            LOGE( "Failed to create jstring to store JSON" );
            return outcome::failure( std::errc::bad_message );
        }

        // Call STATIC method instead of instance method
        auto success = env->CallStaticBooleanMethod( key_store_helper_class_, save_method_, jdata );

        env->DeleteLocalRef( jdata );

        if ( env->ExceptionCheck() != 0U )
        {
            LOGE( "Exception occurred while saving to KeyStore" );
            env->ExceptionDescribe();
            env->ExceptionClear();
            return outcome::failure( std::errc::bad_message );
        }

        if ( success == 0U )
        {
            LOGE( "Failed to save to KeyStore" );
            return outcome::failure( std::errc::connection_aborted );
        }

        return outcome::success();
    }

    JNIEnv *AndroidSecureStorage::GetJNIEnv() const
    {
        JNIEnv *env    = nullptr;
        jint    result = g_jvm->GetEnv( reinterpret_cast<void **>( &env ), JNI_VERSION_1_6 );

        if ( result == JNI_EDETACHED )
        {
            result = g_jvm->AttachCurrentThread( &env, nullptr );
            if ( result != JNI_OK )
            {
                LOGE( "Failed to attach current thread" );
                return nullptr;
            }
        }
        else if ( result != JNI_OK )
        {
            LOGE( "Failed to get JNI environment" );
            return nullptr;
        }

        return env;
    }

    outcome::result<ISecureStorage::SecureBufferType> AndroidSecureStorage::Load( const std::string &key )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        if ( !d.HasMember( key.c_str() ) )
        {
            return outcome::failure( std::errc::no_message );
        }

        auto &value = d[key.c_str()];
        if ( !value.IsString() )
        {
            return outcome::failure( std::errc::bad_message );
        }

        SecureBufferType ret( value.GetString(), value.GetStringLength() );

        return ret;
    }

    outcome::result<void> AndroidSecureStorage::Save( const std::string &key, const SecureBufferType &buffer )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        rj::Value val( rj::StringRef( buffer.c_str(), buffer.length() ), d.GetAllocator() );

        if ( d.HasMember( key.c_str() ) )
        {
            d[key.c_str()] = val;
        }
        else
        {
            d.AddMember( rj::StringRef( key.c_str(), key.size() ), val, d.GetAllocator() );
        }

        return SaveJSON( std::move( d ) );
    }

    outcome::result<bool> AndroidSecureStorage::DeleteKey( const std::string &key )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        bool ret = d.RemoveMember( key.c_str() );

        OUTCOME_TRY( SaveJSON( std::move( d ) ) );

        return ret;
    }
}

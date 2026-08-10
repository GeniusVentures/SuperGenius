#include "Android.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include <android/log.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "outcome/outcome.hpp"

// Forward-declare InitGeniusSDKAndroid (defined in GeniusSDK/src/GeniusSDKAndroid.cpp,
// compiled into the same .so). Avoids including GeniusSDKAndroid.hpp to prevent
// circular dependency (GeniusSDK depends on SuperGenius, not vice versa).
namespace sgns
{
    void InitGeniusSDKAndroid( JavaVM *vm );
}

#define LOG_TAG "AndroidSecureStorage"
#define LOGI( ... ) __android_log_print( ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__ )
#define LOGE( ... ) __android_log_print( ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__ )

namespace
{
    JavaVM *g_jvm = nullptr;
    jclass  g_keystore_helper_class = nullptr;
    
    // Helper function to find class using app ClassLoader instead of system ClassLoader
    jclass FindClassUsingAppClassLoader( JNIEnv *env, jobject context, const char *className )
    {
        // Get the application's ClassLoader through the Context
        jclass contextClass = env->GetObjectClass( context );
        if ( contextClass == nullptr )
        {
            LOGE( "Failed to get Context class" );
            return nullptr;
        }
        
        jmethodID getClassLoaderMethod = env->GetMethodID( contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;" );
        if ( getClassLoaderMethod == nullptr )
        {
            LOGE( "Failed to get getClassLoader method" );
            env->DeleteLocalRef( contextClass );
            return nullptr;
        }
        
        jobject classLoader = env->CallObjectMethod( context, getClassLoaderMethod );
        env->DeleteLocalRef( contextClass );
        
        if ( classLoader == nullptr )
        {
            LOGE( "Failed to get ClassLoader" );
            return nullptr;
        }
        
        // Use ClassLoader.loadClass() to find our class
        jclass classLoaderClass = env->GetObjectClass( classLoader );
        jmethodID loadClassMethod = env->GetMethodID( classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;" );
        env->DeleteLocalRef( classLoaderClass );
        
        if ( loadClassMethod == nullptr )
        {
            LOGE( "Failed to get loadClass method" );
            env->DeleteLocalRef( classLoader );
            return nullptr;
        }
        
        // Convert className from JNI format (ai/gnus/sdk/KeyStoreHelper) to Java format (ai.gnus.sdk.KeyStoreHelper)
        std::string javaClassName( className );
        std::replace( javaClassName.begin(), javaClassName.end(), '/', '.' );
        
        jstring classNameStr = env->NewStringUTF( javaClassName.c_str() );
        jclass foundClass = static_cast<jclass>( env->CallObjectMethod( classLoader, loadClassMethod, classNameStr ) );
        
        env->DeleteLocalRef( classNameStr );
        env->DeleteLocalRef( classLoader );
        
        return foundClass;
    }
}

// JNI function callable from Java to initialize the KeyStoreHelper class reference
extern "C" JNIEXPORT void JNICALL
Java_ai_gnus_sdk_KeyStoreHelper_nativeInit( JNIEnv *env, jclass clazz, jobject context )
{
    LOGI( "KeyStoreHelper native initialization called" );
    
    if ( g_keystore_helper_class != nullptr )
    {
        LOGI( "KeyStoreHelper class already initialized" );
        return;
    }
    
    // Find the KeyStoreHelper class using app ClassLoader
    jclass local_class = FindClassUsingAppClassLoader( env, context, "ai/gnus/sdk/KeyStoreHelper" );
    
    if ( local_class == nullptr )
    {
        LOGE( "Failed to find KeyStoreHelper class using app ClassLoader" );
        // Fallback to FindClass (may work on main thread)
        local_class = env->FindClass( "ai/gnus/sdk/KeyStoreHelper" );
        if ( local_class == nullptr )
        {
            LOGE( "Failed to find KeyStoreHelper class with FindClass fallback" );
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }
    }
    
    // Cache as global reference
    g_keystore_helper_class = static_cast<jclass>( env->NewGlobalRef( local_class ) );
    env->DeleteLocalRef( local_class );
    
    LOGI( "KeyStoreHelper class cached successfully" );
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad( JavaVM *vm, void *_reserved )
{
    LOGI( "SuperGenius SDK initializing" );

    g_jvm = vm;

    // Initialize GeniusSDK Android background bridge
    sgns::InitGeniusSDKAndroid( vm );

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
    AndroidSecureStorage::AndroidSecureStorage( std::string identifier, JavaVM *jvm ): jvm_(jvm) {
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

        // Use cached global reference instead of FindClass
        if ( g_keystore_helper_class == nullptr )
        {
            LOGE( "KeyStoreHelper class not cached. Did you call KeyStoreHelper.initialize() from Java?" );
            throw std::runtime_error( "KeyStoreHelper class not initialized. Call KeyStoreHelper.initialize(context) first." );
        }
        
        key_store_helper_class_ = g_keystore_helper_class;

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
        // We don't delete key_store_helper_class_ here anymore since it's a shared global reference
        // It will be cleaned up when the JVM unloads
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
}

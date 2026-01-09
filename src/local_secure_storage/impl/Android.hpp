#pragma once

#include "../ISecureStorage.hpp"

#include <jni.h>
#include <rapidjson/document.h>

namespace sgns
{
    class AndroidSecureStorage : public ISecureStorage
    {
    public:
        explicit AndroidSecureStorage( JavaVM *jvm = nullptr );
        ~AndroidSecureStorage() override;

        std::string GetName() override
        {
            return "AndroidSecureStorage";
        }

        outcome::result<SecureBufferType> Load( const std::string &key ) override;
        outcome::result<void>             Save( const std::string &key, const SecureBufferType &buffer ) override;
        outcome::result<bool>             DeleteKey( const std::string &key ) override;

    private:
        outcome::result<rapidjson::Document> LoadJSON() const;
        outcome::result<void>                SaveJSON( rapidjson::Document document );
        JNIEnv                              *GetJNIEnv() const;

        JavaVM *jvm_;
        jclass  key_store_helper_class_;

        jmethodID load_method_;
        jmethodID save_method_;
        jmethodID delete_method_;
    };
}

#ifndef SGNS_ANDROID_SECURE_STORAGE_HPP
#define SGNS_ANDROID_SECURE_STORAGE_HPP

#include "JSONBackend.hpp"

#include <jni.h>

namespace sgns
{
    class AndroidSecureStorage : public JSONBackend
    {
    public:
        explicit AndroidSecureStorage( std::string identifier, JavaVM *jvm = nullptr );

        ~AndroidSecureStorage() override;

        std::string GetName() override
        {
            return "AndroidSecureStorage";
        }

        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;

    private:
        JNIEnv *GetJNIEnv() const;

        JavaVM *jvm_;
        jclass  key_store_helper_class_;

        jmethodID load_method_;
        jmethodID save_method_;
        jmethodID delete_method_;
    };
}

#endif // SGNS_ANDROID_SECURE_STORAGE_HPP

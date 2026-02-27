# Android Secure Storage AAR

This directory contains the Android Gradle project for building the SuperGenius secure storage AAR.

## Overview

The AAR contains:
- `ai.gnus.sdk.KeyStoreHelper` - Java class providing secure key storage using Android KeyStore

## Building

### Via CMake (Recommended)
When building the Android version of SuperGenius, the AAR will be built automatically during `make`:

```bash
cd build/OSX
mkdir -p Android && cd Android
cmake ../.. \
  -DCMAKE_ANDROID_NDK=/path/to/ndk \
  -DANDROID_ABI=arm64-v8a \
  -DCMAKE_BUILD_TYPE=Release
make
make install
```

The Gradle wrapper will be set up automatically if needed during the CMake configure step.
The AAR will be built during `make` and installed to `${CMAKE_INSTALL_PREFIX}/lib/android/securestorage-release.aar` during `make install`.

### Via Gradle Directly
You can also build the AAR directly using Gradle:

```bash
cd build/Android/securestorage
./gradlew :library:assembleRelease
```

Output: `build/Android/securestorage/library/build/outputs/aar/library-release.aar`

## Usage in Unity/GeniusSDK

1. Include the AAR in your Unity project's `Assets/Plugins/Android/` directory
2. Initialize KeyStoreHelper before using native secure storage:

```java
import ai.gnus.sdk.KeyStoreHelper;

public class YourUnityActivity extends UnityPlayerActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        KeyStoreHelper.initialize(this);
    }
}
```

## JNI Contract

The C++ code in `src/local_secure_storage/impl/Android.cpp` calls these methods:
- `ai.gnus.sdk.KeyStoreHelper.initialize(Landroid/content/Context;)V`
- `ai.gnus.sdk.KeyStoreHelper.load()Ljava/lang/String;`
- `ai.gnus.sdk.KeyStoreHelper.save(Ljava/lang/String;)Z`
- `ai.gnus.sdk.KeyStoreHelper.delete(Ljava/lang/String;)Z`

## Requirements

- Android SDK 33
- Min SDK 28 (Android 9.0)
- Gradle 8.1+

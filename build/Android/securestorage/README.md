# Android Secure Storage AAR

This directory contains the Android Gradle project for building the SuperGenius secure storage AAR.

## Overview

The AAR contains:
- `ai.gnus.sdk.KeyStoreHelper` - Java class providing secure key storage using Android KeyStore

**Source Location**: The Java source is maintained in `src/local_secure_storage/impl/KeyStoreHelper.java` and referenced by the Gradle build. This ensures a single source of truth alongside the C++ implementation.

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
2. Include your SDK `.so` file in `Assets/Plugins/Android/libs/[ABI]/`
3. Initialize KeyStoreHelper before using native secure storage:

```java
import ai.gnus.sdk.KeyStoreHelper;

public class YourUnityActivity extends UnityPlayerActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // This must be called AFTER the native SDK .so is loaded
        // It caches the class reference using the app's ClassLoader
        KeyStoreHelper.initialize(this);
    }
}
```

**Important**: The AAR's `nativeInit` method is implemented in your SDK's `.so` file. Make sure Unity loads the native library before calling `KeyStoreHelper.initialize()`.

## JNI Contract

The C++ code in `src/local_secure_storage/impl/Android.cpp`:
- Implements: `Java_ai_gnus_sdk_KeyStoreHelper_nativeInit` - Caches class reference using app ClassLoader
- Calls these static Java methods:
  - `ai.gnus.sdk.KeyStoreHelper.load()Ljava/lang/String;`
  - `ai.gnus.sdk.KeyStoreHelper.save(Ljava/lang/String;)Z`
  - `ai.gnus.sdk.KeyStoreHelper.delete(Ljava/lang/String;)Z`

**ClassLoader Fix**: The native code now uses the app's ClassLoader (via the Context passed to `initialize()`) instead of the system ClassLoader, which fixes crashes when secure storage is accessed from worker threads.

## Requirements

- Android SDK 33
- Min SDK 28 (Android 9.0)
- Gradle 8.1+

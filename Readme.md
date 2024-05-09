This is the block-lattice super fast cryptotoken system based on the original nanocurrency

## Download SuperGenius project

```bash
git clone git@github.com:GeniusVentures/SuperGenius.git --recursive 
cd SuperGenius
git checkout develop
```

## Download thirdparty project

```bash
cd ..
git clone git@github.com:GeniusVentures/thirdparty.git --recursive 
cd thirdparty
git checkout develop
```
## [Build thirdparty project](../../../thirdparty/blob/master/README.md)

The folder structure must be as follows:

```bash
.
├── thirdparty                           # geniustokens thirdparty
│   ├── grpc                             # grpc latest version (current v1.28.1)
│   ├── libp2p                           # libp2p cross-compile branch
│   └── ipfs-lite-cpp                    # current repo
│        ├── ipfs-lite                   # sub folder
│        ├── readme.md                   # readme
│        └── CMakeList.txt               # CMake file
└── SuperGenius   
    ├── readme.md                        # readme
    └── CMakeList.txt                    # CMake file
```

## Building

### Windows

I used visual studio 2019 to compile SuperGenius project.
1. Download OpenSSL and install
2. Build SuperGenius using following commands in Release configuration:
    
```bash
cd SuperGenius 
md .build 
cd .build 
cmake ../build/Windows -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] -DTESTING=OFF
cmake --build . --config Release
```

If you are going to build and test, then use following commands

```bash
cmake ../build/Windows -G "Visual Studio 16 2019" -A x64 -DTESTING=ON -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE]
cmake --build . --config Release
cd SuperGenius/src/SuperGenius-build
ctest -C Release
```

To run sepecific test with detailed log, you can use following commands.

```bash
ctest -C Release -R <test_name> --verbose
```

To run all tests and display log for failed tests, you can use following commands.

```bash
ctest -C Release --output-on-failure
```

You can use Debug configuration to debug in Visual Studio.
 
 example build commands

```bash
cmake ../build/Windows -G "Visual Studio 16 2019" -A x64 -DTESTING=ON  -DCMAKE_BUILD_TYPE=Debug  -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_DEBUG]

cmake --build . --config Debug
cd SuperGenius/src/SuperGenius-build
ctest -C Debug
```

### Linux

```bash
cd SuperGenius 
mkdir .build 
cd .build 
cmake ../build/Linux -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] 
cmake --build . --config Release
```

### Linux for Android cross compile

#### Preinstall
- CMake 
- Android NDK Latest LTS Version (r21e) [(link)](https://developer.android.com/ndk/downloads#lts-downloads)
- ([Build thirdparty project](../thirdparty/README.md))

#### Building
```bash
export ANDROID_NDK=/path/to/android-ndk-r21e
export ANDROID_TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
export PATH="$ANDROID_TOOLCHAIN":"$PATH" 
```

* armeabi-v7a

```bash
mkdir .build/Android.armeabi-v7a
cd ./.build/Android.armeabi-v7a
cmake ../../build/Android/ -DANDROID_ABI="armeabi-v7a" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_Android.armeabi-v7a] 
make -j4
```

* arm64-v8a

```bash
mkdir .build/Android.arm64-v8a
cd ./.build/Android.arm64-v8a
cmake ../../build/Android/ -DANDROID_ABI="arm64-v8a" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_Android.arm64-v8a] 
make -j4
```

* x86

```bash
mkdir .build/Android.x86
cd ./.build/Android.x86
cmake ../../build/Android/ -DANDROID_ABI="x86" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_Android.x86]
make -j4
```

* x86_64
```bash
mkdir .build/Android.x86_64
cd ./.build/Android.x86_64
cmake ../../build/Android/ -DANDROID_ABI="x86_64" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_Android.x86_64]
make -j4
```

### OSX 

```bash
cd SuperGenius 
mkdir .build/OSX 
cd .build/OSX
cmake ../../build/OSX -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] 
make -j4
```

#### For iOS cross compile 

```bash
cd SuperGenius 
mkdir .build/iOS 
cd .build/iOS
cmake ../../build/iOS -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] -DCMAKE_TOOLCHAIN_FILE=[/ABSOLUTE/PATH/TO/GeniusTokens/SuperGenius/build/iOS/iOS.cmake] -DiOS_ABI=arm64-v8a -DIOS_ARCH="arm64" -DENABLE_ARC=0 -DENABLE_BITCODE=0 -DENABLE_VISIBILITY=1  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_SYSTEM_PROCESSOR=arm64
make -j4
```

### Setting up VS Code intellisense

This requires installing the (C/C++ Extension Pack)[https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack]. Configure CMake Tools to appear under the toolbar, with this setting:

```json
"cmake.options.statusBarVisibility": "visible",
```

And configure the output build directory to be `.build`, with this setting:

```json
"cmake.buildDirectory": "${workspaceFolder}/.build",
```

By pressing `CTRL + P` and picking `CMake: Configure`, choose the `CMakeLists.txt` file associated with your build. Microsoft's C++ extension (and clangd) works by looking at the file `compile_commands.json`, which is generated by CMake in the output directory. To tell the extension to let CMake Tools control it, use this setting:

```json
"C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
"cmake.configureArgs": ["THIRDPARTY_DIR=${workspaceFolder}/../thirdparty"],
```

This will also configure the `thirdparty` directory. Now, it should be working.

## Running tests

### Linux

Enter the `.build` directory and run `ctest -C Release`.
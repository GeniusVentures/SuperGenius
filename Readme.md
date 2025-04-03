This is the block-lattice super fast cryptotoken system based on the original nanocurrency



## Download thirdparty project

```bash
cd ..
git clone git@github.com:GeniusVentures/thirdparty.git
cd thirdparty
git checkout develop
git submodule update --init --recursive
```
## [Build thirdparty project](../../../thirdparty/blob/master/README.md)

## Download SuperGenius project

```bash
git clone git@github.com:GeniusVentures/SuperGenius.git 
cd SuperGenius
git checkout develop
git submodule update --init --recursive
```

Ideally the folder structure should be as follows, this way cmake will use relative directory for thirdparty automatically

```bash
.
├── thirdparty                          # SuperGenius project
│   └── build                            # build directory
│        ├── Android                     # Android build directory
│        ├── Linux                       # Linux build directory
│        ├── Windows                     # Windows build directory
│        ├── OSX                         # OSX build directory
|            └── Release                 # Release build of OSX (Created when building for OSX Release)
|        └── iOS                         # iOS build directory
└── SuperGenius   
│   └── build                            # build directory
│        ├── Android                     # Android build directory
│        ├── Linux                       # Linux build directory
│        ├── Windows                     # Windows build directory
│        ├── OSX                         # OSX build directory
|            └── Release                 # Release build of OSX (Created when building for OSX Release)
|        └── iOS                         # iOS build directory
```

## Building

Chose the CMAKE_BUILD_TYPE according to the desired configuration (Debug or Release). Chose TESTING ON of OFF to enable unit tests.

### Windows

Use Visual Studio 17 2022 to compile SuperGenius project. 

```bash
cd build
cd Windows 
md [Debug or Release] 
cd [Debug or Release]
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=[Debug or Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DTESTING=[ON or OFF]
cmake --build . --config [Debug or Release]
```


### Linux

```bash
cd build/Linux
mkdir [Debug or Release] 
cd [Debug or Release]
cmake .. -DCMAKE_BUILD_TYPE=[Debug or Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DTESTING=[ON or OFF]
cmake --build . --config [Debug or Release]
```

### Android Cross-Compile on Linux/OSX Hosts

#### Preinstall
- CMake 
- Android NDK Latest LTS Version (r25b) [(link)](https://github.com/android/ndk/wiki/Unsupported-Downloads)

#### Building
```bash
export ANDROID_NDK=/path/to/android-ndk-r25b
export ANDROID_TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
export PATH="$ANDROID_TOOLCHAIN":"$PATH" 
```

* armeabi-v7a

```bash
cd build/Android
mkdir -p [Debug or Release]/armeabi-v7a
cd [Debug or Release]/armeabi-v7a
cmake ../../ -DANDROID_ABI="armeabi-v7a" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] 
cmake --build . --config [Debug or Release]
```

* arm64-v8a

```bash
cd build/Android
mkdir -p [Debug or Release]/arm64-v8a
cd [Debug or Release]/arm64-v8a
cmake ../../ -DANDROID_ABI="arm64-v8a" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DSUPERGENIUS_SRC_DIR=[ABSOLUTE_PATH_TO_SUPERGENIUS_PROJECT]
cmake --build . --config [Debug or Release]
```

* x86

```bash
cd build/Android
mkdir -p [Debug or Release]/x86
cd [Debug or Release]/x86
cmake ../../ -DANDROID_ABI="x86" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DSUPERGENIUS_SRC_DIR=[ABSOLUTE_PATH_TO_SUPERGENIUS_PROJECT]
cmake --build . --config [Debug or Release]
```

* x86_64
```bash
cd build/Android
mkdir -p [Debug or Release]/x86_64
cd [Debug or Release]/x86_64
cmake ../../ -DANDROID_ABI="x86_64" -DCMAKE_ANDROID_NDK=$ANDROID_NDK -DANDROID_TOOLCHAIN=clang -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DSUPERGENIUS_SRC_DIR=[ABSOLUTE_PATH_TO_SUPERGENIUS_PROJECT]
cmake --build . --config [Debug or Release]
```

### OSX (x86_64 & Arm64) 

```bash
cd build/OSX
mkdir [Debug or Release] 
cd [Debug or Release]
cmake .. -DCMAKE_BUILD_TYPE=[Debug or Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT]
cmake --build . --config [Debug or Release]
```

### iOS cross compile 

```bash
cd build/iOS
mkdir [Debug or Release] 
cd [Debug or Release]
cmake .. -DCMAKE_BUILD_TYPE=[Debug or Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DCMAKE_TOOLCHAIN_FILE=../iOS.cmake -DiOS_ABI=arm64-v8a -DIOS_ARCH="arm64" -DENABLE_ARC=0 -DENABLE_BITCODE=0 -DENABLE_VISIBILITY=1  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_SYSTEM_PROCESSOR=arm64 
cmake --build . --config [Debug or Release]
```
## Unit tests
If you configured and built with -DTESTING=ON, you can execute the unit tests on the root of the build directory.

```bash
ctest -C [Debug or Release]
```

To run sepecific test with detailed log, you can use following commands.

```bash
ctest -C [Debug or Release] -R <test_name> --verbose
```

To run all tests and display log for failed tests, you can use following commands.

```bash
ctest -C [Debug or Release] --output-on-failure
```

## Setting up VS Code intellisense

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



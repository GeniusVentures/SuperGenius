# SuperGenius

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

Ideally the folder structure should be as follows, this way CMake will use relative directory for `thirdparty` automatically

```bash
.
├── thirdparty                          # SuperGenius project
│   └── build                            # build directory
│        ├── Android                     # Android build directory
│        ├── Linux                       # Linux build directory
│        ├── Windows                     # Windows build directory
│        ├── OSX                         # OSX build directory
|            └── Release                 # Release build of OSX (Created when building for macOS Release)
|        └── iOS                         # iOS build directory
└── SuperGenius   
│   └── build                            # build directory
│        ├── Android                     # Android build directory
│        ├── Linux                       # Linux build directory
│        ├── Windows                     # Windows build directory
│        ├── OSX                         # OSX build directory
|            └── Release                 # Release build of OSX (Created when building for macOS Release)
|        └── iOS                         # iOS build directory
```

By having this directory structure, you won't need to pass the hard-coded value for the `thirdparty` directory.

## Building

Chose the `CMAKE_BUILD_TYPE` according to the desired configuration (Debug or Release). Chose TESTING ON of OFF to enable unit tests.

### Note on ABIs

When cross-compiling to a different ABI, it is recommended to have the `thirdparty` libraries on `build/[Config]/[ABI]`, e.g. for Linux release on ARM it would look like `build/Release/aarch64`. You'll also need to provide this ABI to SuperGenius in the CMake variable `ABI_SUBFOLDER_NAME` so that it can properly find the necessary directories.

### Windows

Use the Visual Studio 17 2022 generator to compile SuperGenius project. 

```bash
cd build\Windows 
cmake -B [Debug | Release] -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=[Debug | Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT]
cmake --build [Debug | Release] --config [Debug | Release]
```

### Linux

```bash
cd build/Linux
cmake -B [Debug | Release] -DCMAKE_BUILD_TYPE=[Debug | Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] -DTESTING=[ON | OFF] -DABI_SUBFOLDER_NAME=[x86_64 | aarch64]
cmake --build . --config [Debug | Release]
```

### Android Cross-Compile on Linux/OSX Hosts

#### Preinstall
- Android NDK Version `r27b` [(link)](https://github.com/android/ndk/wiki/Unsupported-Downloads)

#### Building

Note that Android is only built on our CI using Linux as the host system.

```bash
export ANDROID_NDK_HOME=[path to Android NDK]
export ANDROID_NDK_ROOT=$ANDROID_NDK_ROOT
export CMAKE_ANDROID_NDK=$ANDROID_NDK_HOME
```

* armeabi-v7a

```bash
cd build/Android
cmake -B[Debug | Release]/armeabi-v7a -DANDROID_ABI="armeabi-v7a" -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] 
cmake --build [Debug | Release]/armeabi-v7a --config [Debug | Release]
```

* arm64-v8a

```bash
cd build/Android
cmake [Debug | Release]/arm64-v8a -DANDROID_ABI="arm64-v8a" -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT]
cmake --build [Debug | Release]/arm64-v8a --config [Debug | Release]
```
* x86_64

```bash
cd build/Android
cmake [Debug | Release]/x86_64 -DANDROID_ABI="x86_64" -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT]
cmake --build [Debug | Release]/arm64-v8a --config [Debug | Release]
```
### macOS

This builds a fat library for both `x86` and `ARM` architectures.

```bash
cd build/OSX
cmake -B [Debug | Release] -DCMAKE_BUILD_TYPE=[Debug | Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT]
cmake --build [Debug | Release] --config [Debug | Release]
```

### iOS

You must use a macOS system to cross-compile for iOS.

```bash
cd build/iOS
cmake -B [Debug | Release] -DCMAKE_BUILD_TYPE=[Debug | Release] -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_PROJECT] 
cmake --build [Debug | Release] --config [Debug | Release]
```

## Unit tests

If you configured and built with `-DTESTING=ON`, you can execute the unit tests on the root of the build directory.

```bash
ctest -C [Debug | Release]
```

To run specific test with detailed log, you can use following commands.

```bash
ctest -C [Debug | Release] -R <test_name> --verbose
```

To run all tests and display log for failed tests, you can use following commands.

```bash
ctest -C [Debug | Release] --output-on-failure
```

## Setting up VS Code intellisense

This requires installing the (C/C++ Extension Pack)[https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack]. Configure CMake Tools to appear under the toolbar, with this setting:

```json
"cmake.options.statusBarVisibility": "visible",
```

And configure the output build directory with this setting:

```json
"cmake.buildDirectory": "${workspaceFolder}/build/[Platform]/[Debug | Release]",
```

By pressing `CTRL + P` and picking `CMake: Configure`, choose the `CMakeLists.txt` file associated with your build. Microsoft's C++ extension (and clangd) works by looking at the file `compile_commands.json`, which is generated by CMake in the output directory. To tell the extension to let CMake Tools control it, use this setting:

```json
"C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
"cmake.configureArgs": ["THIRDPARTY_DIR=${workspaceFolder}/../thirdparty"],
```

This will also configure the `thirdparty` directory. Now, it should be working.

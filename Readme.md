This is the block-lattice super fast cryptotoken system based on the original nanocurrency

# Download SuperGenius project
   
    git clone ssh://git@gitlab.geniusventures.io:8487/GeniusVentures/SuperGenius.git --recursive 
    cd SuperGenius
    git checkout develop
    
# Download thirdparty project

    cd ..
    git clone ssh://git@gitlab.geniusventures.io:8487/GeniusVentures/thirdparty.git --recursive 
    cd thirdparty
    git checkout develop
# [Build thirdparty project](../thirdparty/README.md)
    
Then folder structure as follows:

    .
    ├── thirdparty                          # geniustokens thirdparty
    │   ├── grpc                             # grpc latest version (current v1.28.1)
    │   ├── libp2p                           # libp2p cross-compile branch
    │   └── ipfs-lite-cpp                    # current repo
    │        ├── ipfs-lite                   # sub folder
    │        ├── readme.md                   # readme
    │        └── CMakeList.txt               # CMake file
    └── SuperGenius   
        ├── readme.md                       # readme
        └── CMakeList.txt                   # CMake file    
 
# Build on Windows
I used visual studio 2019 to compile SuperGenius project.
2. download OpenSSL and install
3. build SuperGenius using following commands in Release configuration:
    
    ```
    cd SuperGenius 
    md .build 
    cd .build 
    cmake ../build/Windows -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] -DTESTING=OFF
    cmake --build . --config Release
    ```

if you are going to build and test , then use following commands

    cmake ../build/Windows -G "Visual Studio 16 2019" -A x64 -DTESTING=ON -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE]
    cmake --build . --config Release
    cd SuperGenius/src/SuperGenius-build
    ctest -C Release
    
To run sepecific test with detailed log, you can use following commands.

    ctest -C Release -R <test_name> --verbose
    
To run all tests and display log for failed tests, you can use following commands.

    ctest -C Release --output-on-failure
   
You can use Debug configuration to debug in Visual Studio.
 
 example build commands

    cmake ../build/Windows -G "Visual Studio 16 2019" -A x64 -DTESTING=ON  -DCMAKE_BUILD_TYPE=Debug  -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_DEBUG]

    cmake --build . --config Debug
    cd SuperGenius/src/SuperGenius-build
    ctest -C Debug

# Build on Linux

    cd SuperGenius 
    mkdir .build 
    cd .build 
    cmake ../build/Linux -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] 
    cmake --build . --config Release

# Build on OSX 

    cd SuperGenius 
    mkdir .build 
    cd .build 
    cmake ../build/OSX -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=[ABSOLUTE_PATH_TO_THIRDPARTY_BUILD_RELEASE] 
    cmake --build . --config Release

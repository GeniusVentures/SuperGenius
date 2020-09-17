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
    
Then folder structure as follows:

    .
    ├── thirdparty                          # geniustokens thirdparty
    │   ├── grpc                             # grpc latest version (current v1.28.1)
    │   ├── leveldb                          # leveldb latest version
    │   ├── libp2p                           # libp2p cross-compile branch
    │   └── ipfs-lite-cpp                    # current repo
    │        ├── ipfs-lite                   # sub folder
    │        ├── readme.md                   # readme
    │        └── CMakeList.txt               # CMake file
    └── SuperGenius   
        ├── block-lattice-node
        ├── readme.md                       # readme
        └── CMakeList.txt                   # CMake file    
 
# Build on Windows
I used visual studio 2017 to compile SuperGenius project.
2. download OpenSSL and install
3. build SuperGenius using following commands in Release configuration:
    
    ```
    cd SuperGenius 
    md .build 
    cd .build 
    cmake ../build/Windows -G "Visual Studio 15 2017 Win64" -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR="/path_to_prebuilt_thirdparty" -DTESTING=OFF
    cmake --build . --config Release
    ```

if you are going to build and test , then use following commands

    cmake ../build/Windows -G "Visual Studio 15 2017 Win64" -DTESTING=ON -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR="/path_to_prebuilt_thirdparty"
    cmake --build . --config Release
    cd SuperGenius/src/SuperGenius-build
    ctest -C Release
    
To run sepecifi test with detailed log, you can use following commands.

    ctest -C Release -R <test_name> --verbose
    
To run all tests and display log for failed tests, you can use following commands.

    ctest -C Release --output-on-failure
   
You can use Debug configuration to debug in Visual Studio.
 
 example build commands

    cmake ../build/Windows -G "Visual Studio 15 2017 Win64" -DTESTING=ON  -DCMAKE_BUILD_TYPE=Debug  -DTHIRDPARTY_DIR="D:/03_TASK/01_blockchain/03-GNUS/work/geniustokens/thirdparty/build/Debug"

    cmake --build . --config Debug
    cd SuperGenius/src/SuperGenius-build
    ctest -C Debug
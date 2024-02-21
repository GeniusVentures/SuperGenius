/**
 * @file       runner.cpp
 * @brief      
 * @date       2024-02-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <iostream>
#include "SuperGeniusDemoApp.hpp"

SuperGeniusDemoApp app;
/**
 * @brief       Just the entry point to calling sgns app
 * @param[in]   argc 
 * @param[in]   argv 
 * @return      A @ref int 
 */
int main( int argc, char **argv )
{
    std::cout << "Starting SuperGenius demo app " << std::endl;

    app.init( argc, argv );
    app.run();
    app.exit();
    return 0;
}
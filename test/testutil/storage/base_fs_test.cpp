#include "testutil/storage/base_fs_test.hpp"

namespace test
{

    void BaseFS_Test::clear()
    {
        if ( fs::exists( base_path ) )
        {
            fs::remove_all( base_path );
        }
    }

    BaseFS_Test::BaseFS_Test( fs::path path ) : base_path( std::move( path ) )
    {
        clear();
        mkdir();

        logger = sgns::base::createLogger( getPathString() );
        logger->set_level( spdlog::level::debug );
    }

    void BaseFS_Test::SetUp()
    {
        clear();
        mkdir();
    }

    void BaseFS_Test::TearDown()
    {
    }
}

#include "testutil/storage/base_fs_test.hpp"

namespace test
{

    void FSFixture::clear()
    {
        if ( fs::exists( base_path ) )
        {
            fs::remove_all( base_path );
        }
    }

    FSFixture::FSFixture( fs::path path ) : base_path( std::move( path ) )
    {
        clear();
        mkdir();

        logger = sgns::base::createLogger( getPathString() );
        logger->set_level( spdlog::level::debug );
    }

    void FSFixture::SetUp()
    {
        clear();
        mkdir();
    }

    void FSFixture::TearDown()
    {
    }
}

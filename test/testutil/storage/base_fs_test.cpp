#include "testutil/storage/base_fs_test.hpp"

#include <thread>

namespace test
{
    FSFixture::FSFixture( fs::path path ) : base_path( std::move( path ) )
    {
        clear();
        fs::create_directory( base_path );

        logger = sgns::base::createLogger( getPathString() );
        logger->set_level( spdlog::level::debug );
    }

    FSFixture::~FSFixture()
    {
        clear();
    }

    void FSFixture::clear() const
    {
        if ( fs::exists( base_path ) )
        {
            for ( int retry = 0; retry < 3; ++retry )
            {
                boost::system::error_code ec;
                fs::remove_all( base_path, ec );
                if ( !fs::exists( base_path ) )
                    break;
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            }
        }
    }
}

#include "testutil/storage/base_fs_test.hpp"
#include "testutil/remove_all.hpp"

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
            std::error_code ec;
            sgns::test::removeAllWithRetry( base_path.string(), ec );
        }
    }
}

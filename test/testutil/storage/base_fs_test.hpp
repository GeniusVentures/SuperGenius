#ifndef SUPERGENIUS_BASE_FS_TEST_HPP
#define SUPERGENIUS_BASE_FS_TEST_HPP

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

#include "base/logger.hpp"

// intentionally here, so users can use fs shortcut
namespace fs = boost::filesystem;

namespace test
{
    /**
   * @brief Base test, which involves filesystem. Can be created with given
   * path. Clears path before test and after test.
   */
    struct FSFixture : public ::testing::Test
    {
        // not explicit, intentionally
        FSFixture( fs::path path );

        ~FSFixture() override
        {
            clear();
        }

        void clear();

        inline void mkdir()
        {
            fs::create_directory( base_path );
        }

        [[nodiscard]] std::string getPathString() const
        {
            return fs::canonical( base_path ).string();
        }

        void TearDown() override;

        void SetUp() override;

    protected:
        fs::path           base_path;
        sgns::base::Logger logger;
    };
}

#endif

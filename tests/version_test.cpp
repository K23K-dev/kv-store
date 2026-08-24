#include "kv/version.hpp"

#include <gtest/gtest.h>

#include <string_view>

TEST(VersionTest, ReportsDevelopmentVersion) {
    EXPECT_EQ(kv::version(), std::string_view{"0.1.0-dev"});
}
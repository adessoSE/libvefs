#include "test-utils.hpp"

#include <filesystem>

namespace vefs_tests
{

vefs::llfio::path_handle const current_path = [] {
    auto currentPath = std::filesystem::current_path();
    return vefs::llfio::path(currentPath).value();
}();

}

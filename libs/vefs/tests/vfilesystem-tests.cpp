#include "../src/vfilesystem.hpp"

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

struct vfilesystem_pre_create_fixture
{
    static constexpr std::array<std::byte, 32> default_user_prk{};

    vefs::llfio::mapped_file_handle testFile;
    std::unique_ptr<sector_device> device;

    master_file_info filesystemInfo;

};

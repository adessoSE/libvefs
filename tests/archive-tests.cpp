#include <vefs/archive.hpp>
#include "boost-unit-test.hpp"

#include <vefs/crypto/provider.hpp>
#include "memfs.hpp"

using namespace vefs::blob_literals;
constexpr auto default_user_prk_raw
    = 0x0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000_as_bytes;
constexpr auto default_user_prk = vefs::blob_view{ default_user_prk_raw };
static_assert(default_user_prk.size() == 32);


BOOST_AUTO_TEST_SUITE(vefs_archive_tests)

BOOST_AUTO_TEST_CASE(archive_create)
{
    using namespace vefs;

    auto memfs = tests::memory_filesystem::create();
    auto fs = std::static_pointer_cast<filesystem>(memfs);
    auto cprov = crypto::debug_crypto_provider();

    archive ac{ fs, "./test-archive.vefs", cprov, default_user_prk, archive::create };
}


BOOST_AUTO_TEST_SUITE_END()

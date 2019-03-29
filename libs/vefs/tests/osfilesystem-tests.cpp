#include <vefs/filesystem.hpp>
#include "boost-unit-test.hpp"

using namespace vefs;
using namespace std::string_view_literals;

BOOST_AUTO_TEST_SUITE(osfs)


BOOST_AUTO_TEST_CASE(instantiation)
{
    auto fs = os_filesystem();
}

BOOST_AUTO_TEST_CASE(create_delete_file)
{
    const auto fileName = "./test_file.xx";

    auto fs = os_filesystem();

    BOOST_TEST_PASSPOINT();
    auto cfile = fs->open(fileName, file_open_mode::readwrite | file_open_mode::create);
    cfile.reset();

    BOOST_TEST_CHECKPOINT("creation succeeded; trying to open the file.");
    auto ofile = fs->open(fileName, file_open_mode::read);
    ofile.reset();

    BOOST_TEST_CHECKPOINT("trying to delete the file");
    fs->remove(fileName);
}

BOOST_AUTO_TEST_CASE(sync_read_write)
{
    constexpr auto fileName = "./test_file.xx"sv;
    const auto data = "some more string data right into memory..."_bv;
    constexpr auto offset = 55;

    auto fs = os_filesystem();

    BOOST_TEST_PASSPOINT();
    auto cfile = fs->open(fileName, file_open_mode::readwrite | file_open_mode::create);

    BOOST_TEST_CHECKPOINT("trying to write some part of the file.");
    cfile->write(data, offset);
    BOOST_TEST(cfile->size() == offset + data.size());
    cfile.reset();

    BOOST_TEST_CHECKPOINT("creation succeeded; trying to open the file.");
    auto ofile = fs->open(fileName, file_open_mode::read);
    std::vector<std::byte> readBackMem(data.size());
    blob readBack{ readBackMem };
    ofile->read(readBack, offset);
    BOOST_TEST(mismatch(data, readBack) == data.size());
    ofile.reset();

    BOOST_TEST_CHECKPOINT("trying to delete the file");
    fs->remove(fileName);
}


BOOST_AUTO_TEST_SUITE_END()

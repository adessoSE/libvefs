#include "memfs.hpp"
#include "boost-unit-test.hpp"

using namespace vefs;
using namespace vefs::tests;
using namespace std::string_view_literals;

BOOST_AUTO_TEST_SUITE(memfs)


BOOST_AUTO_TEST_CASE(instantiation)
{
    auto fs = memory_filesystem::create();
}

BOOST_AUTO_TEST_CASE(create_delete_file)
{
    const auto fileName = "/test_file.xx";

    auto fs = memory_filesystem::create();

    BOOST_TEST_PASSPOINT();
    auto cfile = fs->open(fileName, file_open_mode::readwrite | file_open_mode::create);
    cfile.reset();

    BOOST_TEST_CHECKPOINT("creation succeeded; trying to open the file.");
    auto ofile = fs->open(fileName, file_open_mode::read);
    ofile.reset();

    BOOST_TEST_CHECKPOINT("trying to delete the file");
    fs->remove(fileName);
    BOOST_TEST(fs->files.empty());
}

BOOST_AUTO_TEST_CASE(sync_read_write)
{
    const auto fileName = "/test_file.xx";
    const auto data = as_bytes(span("some more string data right into memory..."));
    const auto offset = memory_file::memory_holder::chunk_size - 10;
    auto fs = memory_filesystem::create();

    BOOST_TEST_PASSPOINT();
    auto cfile = fs->open(fileName, file_open_mode::readwrite | file_open_mode::create);

    BOOST_TEST_CHECKPOINT("trying to write some part of the file.");
    cfile->write(data, offset);
    BOOST_TEST(cfile->size() == offset + data.size());
    cfile.reset();

    BOOST_TEST_CHECKPOINT("creation succeeded; trying to open the file.");
    auto ofile = fs->open(fileName, file_open_mode::read);
    std::vector<std::byte> readBack(data.size());
    ofile->read(readBack, offset);
    BOOST_TEST(mismatch_distance(data, span(readBack)) == data.size());
    ofile.reset();

    BOOST_TEST_CHECKPOINT("trying to delete the file");
    fs->remove(fileName);
    BOOST_TEST(fs->files.empty());
}


BOOST_AUTO_TEST_SUITE_END()

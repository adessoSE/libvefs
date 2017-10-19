#pragma once

#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>
#include <vefs/detail/raw_archive.hpp>

namespace vefs
{

class archive
{
public:
    archive(filesystem::ptr fs, std::string_view archivePath);

private:
    std::unique_ptr<detail::raw_archive> mArchive;
};


}

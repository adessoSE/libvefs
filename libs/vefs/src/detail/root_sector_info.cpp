#include "root_sector_info.hpp"

#include <vefs/span.hpp>

#include "proto-helper.hpp"

namespace vefs ::detail
{
    void root_sector_info::pack_to(
        adesso::vefs::FileDescriptor &fd)
    {
        fd.set_startblockidx(static_cast<std::uint64_t>(root.sector));
        fd.set_startblockmac(root.mac.data(), root.mac.size());

        fd.set_filesize(maximum_extent);
        fd.set_reftreedepth(tree_depth);
    }
    auto root_sector_info::unpack_from(adesso::vefs::FileDescriptor &fd)
        -> root_sector_info
    {
        root_sector_info rsi{};
        rsi.root.sector = sector_id{fd.startblockidx()};
        copy(as_bytes(span(fd.startblockmac())), span(rsi.root.mac));

        rsi.maximum_extent = fd.filesize();
        rsi.tree_depth = fd.reftreedepth();

        return rsi;
    }
}


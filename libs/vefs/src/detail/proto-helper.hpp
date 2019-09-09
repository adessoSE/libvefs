#pragma once

#include <vefs/platform/secure_memzero.hpp>
#include <vefs/span.hpp>

#include "basic_archive_file_meta.hpp"
#include "sector_device.hpp"

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push, 3)
#pragma warning(disable : 4244)
#endif

#include "fileformat.pb.h"

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

namespace vefs
{
    template <typename T>
    inline bool parse_blob(T &out, ro_dynblob raw)
    {
        return out.ParseFromArray(raw.data(), static_cast<int>(raw.size()));
    }

    template <typename T>
    inline bool serialize_to_blob(rw_dynblob out, T &data)
    {
        return data.SerializeToArray(out.data(), static_cast<int>(out.size()));
    }

    inline void unpack(detail::basic_archive_file_meta &rawFile, adesso::vefs::FileDescriptor &fd)
    {
        copy(as_bytes(span(fd.filesecret())), as_span(rawFile.secret));
        rawFile.write_counter = crypto::counter(as_bytes(span(fd.filesecretcounter())).first<16>());
        copy(as_bytes(span(fd.startblockmac())), span(rawFile.start_block_mac));

        rawFile.id = detail::file_id{as_bytes(span(fd.fileid())).first<16>()};

        rawFile.start_block_idx = detail::sector_id{fd.startblockidx()};
        rawFile.size = fd.filesize();
        rawFile.tree_depth = fd.reftreedepth();
    }
    inline std::unique_ptr<detail::basic_archive_file_meta> unpack(adesso::vefs::FileDescriptor &fd)
    {
        auto rawFilePtr = std::make_unique<detail::basic_archive_file_meta>();
        unpack(*rawFilePtr, fd);
        return rawFilePtr;
    }
    inline void pack(adesso::vefs::FileDescriptor &fd,
                     const detail::basic_archive_file_meta &rawFile)
    {
        fd.set_filesecret(rawFile.secret.data(), rawFile.secret.size());
        auto ctr = rawFile.write_counter.load();
        fd.set_filesecretcounter(ctr.view().data(), ctr.view().size());
        fd.set_startblockmac(rawFile.start_block_mac.data(), rawFile.start_block_mac.size());

        fd.set_fileid(rawFile.id.as_uuid().data, utils::uuid::static_size());

        fd.set_startblockidx(static_cast<std::uint64_t>(rawFile.start_block_idx));
        fd.set_filesize(rawFile.size);
        fd.set_reftreedepth(rawFile.tree_depth);
    }
    inline adesso::vefs::FileDescriptor *pack(const detail::basic_archive_file_meta &rawFile)
    {
        auto *fd = new adesso::vefs::FileDescriptor;
        pack(*fd, rawFile);
        return fd;
    }

    inline void erase_secrets(adesso::vefs::FileDescriptor &fd)
    {
        if (auto secretPtr = fd.mutable_filesecret())
        {
            utils::secure_memzero(as_writable_bytes(span(*secretPtr)));
        }
    }

    inline void erase_secrets(adesso::vefs::ArchiveHeader &header)
    {
        if (auto indexPtr = header.mutable_archiveindex())
        {
            erase_secrets(*indexPtr);
        }
        if (auto freeSectorIndexPtr = header.mutable_freeblockindex())
        {
            erase_secrets(*freeSectorIndexPtr);
        }
    }

    inline void erase_secrets(adesso::vefs::StaticArchiveHeader &header)
    {
        if (auto masterSecretPtr = header.mutable_mastersecret())
        {
            utils::secure_memzero(as_writable_bytes(span(*masterSecretPtr)));
        }
        if (auto writeCtr = header.mutable_staticarchiveheaderwritecounter())
        {
            utils::secure_memzero(as_writable_bytes(span(*writeCtr)));
        }
    }
} // namespace vefs

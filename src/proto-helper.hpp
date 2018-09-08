#pragma once

#include <vefs/blob.hpp>
#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/basic_archive_file_meta.hpp>
#include <vefs/utils/secure_ops.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
#pragma warning(disable: 4146 4100)
#endif

#include "fileformat.pb.h"

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif


namespace vefs
{
    template <typename T>
    inline bool parse_blob(T &out, blob_view raw)
    {
        return out.ParseFromArray(raw.data(), static_cast<int>(raw.size()));
    }

    template <typename T>
    inline bool serialize_to_blob(blob out, T &data)
    {
        return data.SerializeToArray(out.data(), static_cast<int>(out.size()));
    }


    inline void unpack(detail::basic_archive_file_meta &rawFile, adesso::vefs::FileDescriptor &fd)
    {
        blob_view{ fd.filesecret() }.copy_to(blob{ rawFile.secret });
        rawFile.write_counter = crypto::counter(blob_view{ fd.filesecretcounter() });
        blob_view{ fd.startblockmac() }.copy_to(blob{ rawFile.start_block_mac });

        rawFile.id = detail::file_id{ blob_view{ fd.fileid() } };

        rawFile.start_block_idx = detail::sector_id{ fd.startblockidx() };
        rawFile.size = fd.filesize();
        rawFile.tree_depth = fd.reftreedepth();

    }
    inline std::unique_ptr<detail::basic_archive_file_meta> unpack(adesso::vefs::FileDescriptor &fd)
    {
        auto rawFilePtr = std::make_unique<detail::basic_archive_file_meta>();
        unpack(*rawFilePtr, fd);
        return rawFilePtr;
    }
    inline void pack(adesso::vefs::FileDescriptor &fd, const detail::basic_archive_file_meta &rawFile)
    {
        fd.set_filesecret(rawFile.secret.data(), rawFile.secret.size());
        auto ctr = rawFile.write_counter.load().value();
        fd.set_filesecretcounter(ctr.data(), ctr.size());
        fd.set_startblockmac(rawFile.start_block_mac.data(), rawFile.start_block_mac.size());

        fd.set_fileid(rawFile.id.as_uuid().data, utils::uuid::static_size());

        fd.set_startblockidx(static_cast<std::uint64_t>(rawFile.start_block_idx));
        fd.set_filesize(rawFile.size);
        fd.set_reftreedepth(rawFile.tree_depth);
    }
    inline adesso::vefs::FileDescriptor * pack(const detail::basic_archive_file_meta &rawFile)
    {
        auto *fd = new adesso::vefs::FileDescriptor;
        pack(*fd, rawFile);
        return fd;
    }

    inline void erase_secrets(adesso::vefs::FileDescriptor &fd)
    {
        if (auto secretPtr = fd.mutable_filesecret())
        {
            utils::secure_memzero(blob{ *secretPtr });
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
            utils::secure_memzero(blob{ *masterSecretPtr });
        }
        if (auto writeCtr = header.mutable_staticarchiveheaderwritecounter())
        {
            utils::secure_memzero(blob{ *writeCtr });
        }
    }
}

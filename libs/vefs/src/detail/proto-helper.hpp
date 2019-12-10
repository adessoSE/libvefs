#pragma once

#include <vefs/platform/secure_memzero.hpp>
#include <vefs/span.hpp>

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

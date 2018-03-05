#pragma once

#include <cassert>

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <type_traits>

#include <vefs/archive.hpp>
#include <vefs/detail/basic_archive_file_meta.hpp>

#include "archive_file.hpp"

namespace vefs
{
    class archive::file_lookup final
    {
        static constexpr std::uint32_t DeadBit = 1u << 31;

    public:
        static constexpr auto create = file::create;

        file_lookup(detail::basic_archive_file_meta &meta);
        file_lookup(detail::basic_archive_file_meta &meta, archive &owner, file::create_tag);

        inline detail::basic_archive_file_meta & meta_data();
        file_handle load(archive &owner);
        bool try_kill(archive &owner);

        inline void add_reference() const;
        inline void release() const;
        inline void add_ext_reference() const;
        inline void ext_release() const;


        inline static file * deref(const file_handle &handle);

    private:
        ~file_lookup();

        archive::file * create_working_set(archive &owner);
        void notify_no_external_references() const;

        mutable std::atomic<std::uint32_t> mRefs;
        mutable std::atomic<std::uint32_t> mExtRefs;
        mutable std::atomic<archive::file *> mWorkingSet;
        mutable std::shared_mutex mSync;

        detail::basic_archive_file_meta mMeta;
        std::aligned_storage_t<sizeof(archive::file), alignof(archive::file)> mWorkingSetStorage;
    };


    inline detail::basic_archive_file_meta & archive::file_lookup::meta_data()
    {
        return mMeta;
    }

    inline void archive::file_lookup::add_reference() const
    {
        mRefs.fetch_add(1, std::memory_order_release);
    }
    inline void archive::file_lookup::release() const
    {
        if (mRefs.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete this;
        }
    }
    inline void archive::file_lookup::add_ext_reference() const
    {
        mExtRefs.fetch_add(1, std::memory_order_release);
    }
    inline void archive::file_lookup::ext_release() const
    {
        if (mExtRefs.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            notify_no_external_references();
        }
    }

    inline archive::file * archive::file_lookup::deref(const file_handle &handle)
    {
        assert(handle);
        return handle.mData->mWorkingSet.load(std::memory_order_acquire);
    }
}

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
        : private file_events
    {
        friend class archive::index_file;

        static constexpr std::uint32_t DeadBit = 1u << 31;

    public:
        file_lookup(detail::basic_archive_file_meta &&meta, std::string name, int ibPos, int numBlocks);

        static auto open(detail::basic_archive_file_meta &&meta, std::string name, int ibPos, int numBlocks)
            -> result<utils::ref_ptr<file_lookup>>;
        static auto create(archive &owner, std::string name)
            -> result<std::tuple<utils::ref_ptr<file_lookup>, file_handle>>;

        inline detail::basic_archive_file_meta & meta_data();
        auto load(archive &owner)
            -> result<file_handle>;
        auto try_load()
            -> result<file_handle>;
        auto try_kill(archive &owner)
            -> result<void>;

        inline void add_reference() const;
        inline void release() const;
        inline void add_ext_reference() const;
        inline void ext_release() const;

        const std::string &name() const noexcept;


        inline static file * deref(const file_handle &handle);
        friend file * deref(const file_handle &) noexcept;

    private:
        ~file_lookup();

        auto create_working_set(archive &owner)
            -> result<archive::file *>;
        auto notify_no_external_references() const
            -> result<void>;

        virtual void on_sector_write_suggestion(sector_handle sector) override;
        virtual void on_root_sector_synced(detail::basic_archive_file_meta &rootMeta) override;
        virtual void on_sector_synced(detail::sector_id physId, ro_blob<16> mac) override;

        mutable std::atomic<std::uint32_t> mRefs;
        mutable std::atomic<std::uint32_t> mExtRefs;
        mutable std::atomic<archive::file *> mWorkingSet;
        mutable std::shared_mutex mSync;

        int mIndexBlockPosition;
        int mReservedIndexBlocks;

        detail::basic_archive_file_meta mMeta;
        utils::dirt_flag mDirtyMetaData;
        const std::string mName;

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
            // #TODO what shall be done?
            (void)notify_no_external_references();
        }
    }

    inline const std::string & archive::file_lookup::name() const noexcept
    {
        return mName;
    }

    inline archive::file * archive::file_lookup::deref(const file_handle &handle)
    {
        assert(handle);
        return handle.mData->mWorkingSet.load(std::memory_order_acquire);
    }
}

#pragma once

#include <mutex>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

#include "block_manager.hpp"
#include "sector_device.hpp"

namespace vefs::detail
{
/**
 * Thread-safe allocator for all sectors in an archive.
 *
 * Uses the \ref block_manager internally to allocate/deallocate sectors and
 * keep track of free sectors.
 */
class archive_sector_allocator final
{
    using id_range = utils::id_range<sector_id>;

public:
    enum class leak_on_failure_t
    {
    };
    static constexpr auto leak_on_failure = leak_on_failure_t{};

    archive_sector_allocator(sector_device &device,
                             file_crypto_ctx::state_type const &cryptoCtx);

    auto alloc_one() noexcept -> result<sector_id>;
    // #TBI multi sector allocation
    // auto alloc_multiple(span<sector_id> ids) noexcept
    //    -> result<void>;

    auto dealloc_one(sector_id which) noexcept -> result<void>;
    void dealloc_one(sector_id which, leak_on_failure_t) noexcept;

    auto merge_from(utils::block_manager<sector_id> &other) noexcept
            -> result<void>;
    auto merge_disjunct(utils::block_manager<sector_id> &other) noexcept
            -> result<void>;

    auto initialize_new() noexcept -> result<void>;
    auto initialize_from(root_sector_info rootInfo) noexcept -> result<void>;
    auto finalize(file_crypto_ctx const &filesystemCryptoCtx,
                  root_sector_info filesystemRoot) noexcept -> result<void>;

    void on_leak_detected() noexcept
    {
        mSectorsLeaked = true;
    }
    bool sector_leak_detected() noexcept
    {
        return mSectorsLeaked;
    }

    auto crypto_ctx() const noexcept -> file_crypto_ctx const &
    {
        return mFileCtx;
    }

private:
    auto mine_new_raw(int num) noexcept -> result<id_range>;
    auto mine_new(int num) noexcept -> result<void>;

    auto trim() noexcept -> result<void>;

    sector_device &mSectorDevice;
    utils::block_manager<sector_id> mSectorManager;
    std::mutex mAllocatorSync;
    file_crypto_ctx mFileCtx;
    sector_id mFreeBlockFileRootSector;
    std::atomic<bool> mSectorsLeaked;
};
} // namespace vefs::detail

#pragma once

#include <cstddef>

#include <array>
#include <utility>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

#include "file_crypto_ctx.hpp"
#include "reference_sector_layout.hpp"
#include "sector_device.hpp"
#include "sector_id.hpp"
#include "tree_walker.hpp"

namespace vefs::detail
{
    template <template <typename> typename TreePolicy>
    class basic_sector final : TreePolicy<basic_sector<TreePolicy>>
    {
    public:
        using policy_type = TreePolicy<basic_sector>;
        using handle_type = typename policy_type::handle_type;

        basic_sector() = delete;
        template <typename... PolicyArgs>
        inline basic_sector(
            tree_position logicalPosition, sector_id physicalPosition,
            PolicyArgs &&... policyArgs) noexcept(std::is_nothrow_constructible_v<policy_type,
                                                                                  PolicyArgs &&...>)
            : policy_type(std::forward<PolicyArgs>(policyArgs)...)
            , mLogicalPosition(logicalPosition)
            , mPhysicalPosition(physicalPosition)
        {
        }
        basic_sector(basic_sector &&) = delete;
        basic_sector(const basic_sector &) = delete;
        basic_sector &operator=(basic_sector &&) = delete;
        basic_sector &operator=(const basic_sector &) = delete;

        inline auto physical_position() const noexcept -> sector_id
        {
            return mPhysicalPosition;
        }
        inline void physical_position(sector_id newPosition) noexcept
        {
            mPhysicalPosition = newPosition;
        }
        inline auto logical_position() const noexcept -> tree_position
        {
            return mLogicalPosition;
        }

        inline static auto sync_to(sector_device &device, file_crypto_ctx &ctx,
                                   handle_type self) noexcept -> result<void>
        {
            if (!policy_type::is_dirty(self))
            {
                return success();
            }

            basic_sector &sector = *self;
            policy_type &policy = sector;
            VEFS_TRY(writePosition, policy.reallocate(sector.physical_position()));

            sector_reference updated{writePosition, {}};
            if (result<void> writerx =
                    device.write_sector(updated.mac, ctx, updated.sector, sector.mBlockData))
            {
                policy.sync_failed(writerx, writePosition);
                return std::move(writerx).as_failure();
            }

            if (const handle_type &parent = policy.parent())
            {
                basic_sector &parentSector = *parent;
                std::shared_lock parentLock{parentSector};

                const auto offset = sector.logical_position().parent_array_offset();
                reference_sector_layout parentLayout{as_span(parentSector)};
                parentLayout.write(offset, updated);

                policy_type::mark_dirty(parent);
            }
            mPhysicalPosition = updated.sector;
            policy.sync_succeeded(updated);
            policy_type::mark_clean(self);

            return success();
        }

        inline auto policy() noexcept -> policy_type &
        {
            return *this;
        }

        inline void lock()
        {
            policy_type::lock();
        }
        inline auto try_lock() -> bool
        {
            return policy_type::try_lock();
        }
        inline void unlock()
        {
            policy_type::unlock();
        }
        inline void lock_shared()
        {
            policy_type::lock_shared();
        }
        inline auto try_lock_shared() -> bool
        {
            return policy_type::try_lock_shared();
        }
        inline void unlock_shared()
        {
            policy_type::unlock_shared();
        }

        inline friend auto as_span(basic_sector &sector) noexcept
            -> rw_blob<sector_device::sector_payload_size>
        {
            return sector.mBlockData;
        }
        inline friend auto as_span(const basic_sector &sector) noexcept
            -> ro_blob<sector_device::sector_payload_size>
        {
            return sector.mBlockData;
        }

    private:
        tree_position mLogicalPosition;
        sector_id mPhysicalPosition;

        std::array<std::byte, sector_device::sector_payload_size> mBlockData;
    };
} // namespace vefs::detail

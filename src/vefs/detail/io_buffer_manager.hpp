#pragma once

#include <cstdint>

#include <memory>
#include <new>
#include <semaphore>

#include <dplx/dp/legacy/memory_buffer.hpp>
#include <dplx/predef/library.h>

#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/workaround.h>

namespace vefs
{

class io_buffer_manager
{
    using allocator = llfio::utils::page_allocator<std::byte>;
    using allocator_traits = std::allocator_traits<allocator>;
    static_assert(allocator_traits::is_always_equal::value);

    using free_buffers_semaphore_type
            = std::counting_semaphore<std::numeric_limits<std::int32_t>::max()>;

    struct control_head
    {
        free_buffers_semaphore_type free_buffers;

#if VEFS_WORKAROUND_TESTED_AT(BOOST_LIB_STD_GNU, 12, 1, 0)
#else
        constexpr
#endif
        explicit control_head(std::uint32_t numBuffers)
            : free_buffers(static_cast<std::ptrdiff_t>(numBuffers))
        {
        }
    };

public:
    static inline const std::size_t page_size = llfio::utils::page_size();

private:
    static inline constexpr std::size_t control_head_size = utils::round_up_p2(
            sizeof(control_head), alignof(std::binary_semaphore));
    static inline constexpr std::size_t control_slot_size = utils::round_up_p2(
            sizeof(std::binary_semaphore), alignof(std::binary_semaphore));
    static inline constexpr std::align_val_t control_block_alignment{
            std::max(alignof(control_head), alignof(std::binary_semaphore))};

    struct control_deleter
    {
        void operator()(std::byte *memory) const noexcept
        {
            ::operator delete(memory, control_block_alignment, std::nothrow);
        }
    };

    dplx::dp::memory_allocation<allocator> mAllocatedPages;
    std::unique_ptr<std::byte[], control_deleter> mControlBlock;
    std::uint32_t mBufferSize{};
    std::uint32_t mNumBuffers{};

public:
    ~io_buffer_manager() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<control_head>)
        {
            std::destroy_at(&head());
        }
        if constexpr (!std::is_trivially_destructible_v<std::binary_semaphore>)
        {
            for (std::uint32_t i = 0; i < mNumBuffers; ++i)
            {
                std::destroy_at(&block(i));
            }
        }
    }
    io_buffer_manager() noexcept = default;

    io_buffer_manager(io_buffer_manager &&other) noexcept
        : mAllocatedPages(std::move(other.mAllocatedPages))
        , mControlBlock(std::move(other.mControlBlock))
        , mBufferSize(std::exchange(other.mBufferSize, 0U))
        , mNumBuffers(std::exchange(other.mNumBuffers, 0U))
    {
    }
    auto operator=(io_buffer_manager &&other) noexcept -> io_buffer_manager &
    {
        mAllocatedPages = std::move(other.mAllocatedPages);
        mControlBlock = std::move(other.mControlBlock);
        mBufferSize = std::exchange(other.mBufferSize, 0U);
        mNumBuffers = std::exchange(other.mNumBuffers, 0U);

        return *this;
    }

private:
    explicit io_buffer_manager(std::uint32_t bufferSize,
                               std::uint32_t numBuffers) noexcept
        : mAllocatedPages()
        , mControlBlock()
        , mBufferSize(
                  llfio::utils::round_up_to_page_size(bufferSize, page_size))
        , mNumBuffers(numBuffers)
    {
    }

public:
    static auto create(std::uint32_t bufferSize,
                       std::uint32_t numBuffers) noexcept
            -> result<io_buffer_manager>
    {
        io_buffer_manager self(bufferSize, numBuffers);
        VEFS_TRY(self.initialize());
        return self;
    }

private:
    auto initialize() noexcept -> result<void>
    {
        VEFS_TRY(mAllocatedPages.resize(mBufferSize * mNumBuffers));
        mControlBlock.reset(static_cast<std::byte *>(::operator new(
                control_head_size + mNumBuffers * control_slot_size,
                control_block_alignment, std::nothrow)));
        if (!mControlBlock)
        {
            return errc::not_enough_memory;
        }

        auto initPtr = mControlBlock.get();
        ::new (initPtr) control_head(mNumBuffers);
        initPtr += control_head_size;

        for (auto const end = initPtr + mNumBuffers * control_slot_size;
             initPtr != end; initPtr += control_slot_size)
        {
            ::new (static_cast<void *>(initPtr)) std::binary_semaphore(1);
        }

        return oc::success();
    }

public:
    [[nodiscard]] auto allocate() const noexcept -> result<std::span<std::byte>>
    {
        if (head().free_buffers.try_acquire())
        {
            for (uint32_t i = 0U;; ++i)
            {
                if (block(i).try_acquire())
                {
                    return block_data(i);
                }
            }
        }
        else
        {
            try
            {
                allocator tmp{}; // this is valid due to always_equal
                auto const allocation
                        = allocator_traits::allocate(tmp, mBufferSize);
                return {allocation, mBufferSize};
            }
            catch (std::bad_alloc const &)
            {
                return errc::not_enough_memory;
            }
        }
    }

    void deallocate(std::span<std::byte> const allocation) const noexcept
    {
        deallocate(allocation.data());
    }
    void deallocate(std::byte *const allocation) const noexcept
    {
        if (auto const blockId = block_id_of(allocation); blockId < mNumBuffers)
        {
            block(blockId).release();
            head().free_buffers.release();
        }
        else
        {
            allocator tmp{}; // this is valid due to always_equal
            allocator_traits::deallocate(tmp, allocation, mBufferSize);
        }
    }

private:
    [[nodiscard]] auto head() const noexcept -> control_head &
    {
        return *std::launder(
                reinterpret_cast<control_head *>(mControlBlock.get()));
    }
    [[nodiscard]] auto block(std::uint32_t const which) const noexcept
            -> std::binary_semaphore &
    {
        return *std::launder(reinterpret_cast<std::binary_semaphore *>(
                mControlBlock.get() + control_head_size
                + which * control_slot_size));
    }
    [[nodiscard]] auto block_data(std::uint32_t const which) const noexcept
            -> std::span<std::byte>
    {
        return mAllocatedPages.as_span().subspan(
                static_cast<std::size_t>(which) * mBufferSize, mBufferSize);
    }
    auto block_id_of(std::byte const *const ptr) const noexcept -> std::uint32_t
    {
        auto const pages = mAllocatedPages.as_span();
        return static_cast<std::uint32_t>(
                (reinterpret_cast<std::uintptr_t>(ptr)
                 - reinterpret_cast<std::uintptr_t>(pages.data()))
                / mBufferSize);
    }
};

} // namespace vefs

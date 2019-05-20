#pragma once

#include <cstddef>

#include <algorithm>
#include <type_traits>
#include <utility>

#include <vefs/allocator/allocation.hpp>

namespace vefs::detail
{
#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
// C4584: class appearing multiple times in the inheritance hierarchy
//        which is intended in this case
#pragma warning(disable : 4584)
#endif

    template <typename Primary, typename... Fallbacks>
    class octopus_allocator
        : private Primary
        , private Fallbacks...
    {
        template <typename Current, typename... Remaining>
        constexpr auto allocate(const std::size_t size) noexcept -> allocation_result;

        // relocates an existing allocation from Owner to a different Allocator
        template <typename Owner, typename Current, typename... Remaining>
        constexpr auto relocate(const memory_allocation memblock, const std::size_t size) noexcept
            -> allocation_result;

        template <typename Current, typename... Remaining>
        constexpr auto reallocate(const memory_allocation memblock, const std::size_t size) noexcept
            -> allocation_result;

        template <typename Current, typename... Remaining>
        constexpr void deallocate(memory_allocation memblock) noexcept;

        // #MSVC_workaround fold expressions don't work within a noexcept() context
        static constexpr bool is_nothrow_default_constructible_v =
            (std::is_nothrow_default_constructible_v<Primary> && ... &&
             std::is_nothrow_default_constructible_v<Fallbacks>);

        template <typename Init1, typename... Inits>
        static constexpr bool
            is_nothrow_constructible_v = (std::is_nothrow_constructible_v<Primary, Init1 &&> &&
                                          ... &&
                                          std::is_nothrow_constructible_v<Fallbacks, Inits &&>);

    public:
        static constexpr std::size_t alignment =
            std::min({Primary::alignment, Fallbacks::alignment...});

        constexpr octopus_allocator() noexcept(is_nothrow_default_constructible_v) = default;
        template <typename Init1, typename... Inits>
        constexpr explicit octopus_allocator(
            std::in_place_t, Init1 &&init1,
            Inits &&... inits) noexcept(is_nothrow_constructible_v<Init1, Inits...>);

        constexpr auto allocate(const std::size_t size) noexcept -> allocation_result;
        constexpr auto reallocate(const memory_allocation memblock, const std::size_t size) noexcept
            -> allocation_result;
        constexpr void deallocate(memory_allocation memblock) noexcept;

        constexpr auto owns(const memory_allocation memblock) noexcept -> bool;
    };

    template <typename Primary, typename... Fallbacks>
    template <typename Init1, typename... Inits>
    constexpr octopus_allocator<Primary, Fallbacks...>::octopus_allocator(
        std::in_place_t, Init1 &&init1,
        Inits &&... inits) noexcept(is_nothrow_constructible_v<Init1, Inits...>)
        : Primary(std::forward<Init1>(init1))
        , Fallbacks(std::forward<Inits>(inits))...
    {
    }

    template <typename Primary, typename... Fallbacks>
    template <typename Current, typename... Remaining>
    constexpr auto
    octopus_allocator<Primary, Fallbacks...>::allocate(const std::size_t size) noexcept
        -> allocation_result
    {
        if constexpr (sizeof...(Remaining) > 0)
        {
            if (auto memblock = Current::allocate(size); memblock.has_value())
            {
                return std::move(memblock);
            }
            return octopus_allocator::allocate<Remaining...>(size);
        }
        else
        {
            return Current::allocate(size);
        }
    }

    template <typename Primary, typename... Fallbacks>
    template <typename Owner, typename Current, typename... Remaining>
    constexpr auto octopus_allocator<Primary, Fallbacks...>::relocate(
        const memory_allocation memblock, const std::size_t size) noexcept -> allocation_result
    {
        if (auto reloc = Current::allocate(size))
        {
            auto moveSize = std::min(memblock.size(), size);
            std::memmove(reloc.value().raw(), memblock.raw(), moveSize);

            Owner::deallocate(memblock);
            return reloc;
        }

        if constexpr (sizeof...(Remaining) > 0)
        {
            return octopus_allocator::relocate<Owner, Remaining...>(memblock, size);
        }
        else
        {
            return std::errc::not_enough_memory;
        }
    }

    template <typename Primary, typename... Fallbacks>
    template <typename Current, typename... Remaining>
    constexpr auto octopus_allocator<Primary, Fallbacks...>::reallocate(
        const memory_allocation memblock, const std::size_t size) noexcept -> allocation_result
    {
        if constexpr (sizeof...(Remaining) > 0)
        {
            if (Current::owns(memblock))
            {
                if (auto reallocated = Current::reallocate(memblock, size))
                {
                    return reallocated;
                }
                return octopus_allocator::relocate<Current, Remaining...>(memblock, size);
            }
            return octopus_allocator::reallocate<Remaining...>(memblock, size);
        }
        else
        {
            return Current::reallocate(memblock, size);
        }
    }

    template <typename Primary, typename... Fallbacks>
    template <typename Current, typename... Remaining>
    constexpr void
    octopus_allocator<Primary, Fallbacks...>::deallocate(memory_allocation memblock) noexcept
    {
        if constexpr (sizeof...(Remaining) > 0)
        {
            if (Current::owns(memblock))
            {
                Current::deallocate(memblock);
            }
            else
            {
                octopus_allocator::deallocate<Remaining...>(memblock);
            }
        }
        else
        {
            Current::deallocate(memblock);
        }
    }

    template <typename Primary, typename... Fallbacks>
    constexpr auto
    octopus_allocator<Primary, Fallbacks...>::allocate(const std::size_t size) noexcept
        -> allocation_result
    {
        return octopus_allocator::allocate<Primary, Fallbacks...>(size);
    }

    template <typename Primary, typename... Fallbacks>
    constexpr auto octopus_allocator<Primary, Fallbacks...>::reallocate(
        const memory_allocation memblock, const std::size_t size) noexcept -> allocation_result
    {
        return octopus_allocator::reallocate<Primary, Fallbacks...>(memblock, size);
    }

    template <typename Primary, typename... Fallbacks>
    constexpr void
    octopus_allocator<Primary, Fallbacks...>::deallocate(memory_allocation memblock) noexcept
    {
        octopus_allocator::deallocate<Primary, Fallbacks...>(memblock);
    }

    template <typename Primary, typename... Fallbacks>
    constexpr auto
    octopus_allocator<Primary, Fallbacks...>::owns(const memory_allocation memblock) noexcept
        -> bool
    {
        return (Primary::owns(memblock) || ... || Fallbacks::owns(memblock));
    }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif
} // namespace vefs::detail

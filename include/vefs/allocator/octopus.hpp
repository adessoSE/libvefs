#pragma once

#include <cstddef>

#include <algorithm>
#include <tuple>
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
class octopus_allocator : private std::tuple<Primary, Fallbacks...>
{
    using base_type = std::tuple<Primary, Fallbacks...>;
    static constexpr auto num_bases = std::tuple_size_v<base_type>;

    inline auto alloc_tuple() noexcept -> base_type &
    {
        return static_cast<base_type &>(*this);
    }

    template <std::size_t idx>
    inline auto allocate(const std::size_t size) noexcept -> allocation_result;

    // relocates an existing allocation from Owner to a different Allocator
    template <std::size_t ownerIdx, std::size_t idx>
    inline auto relocate(const memory_allocation memblock,
                         const std::size_t size) noexcept -> allocation_result;

    template <std::size_t idx>
    inline auto reallocate(const memory_allocation memblock,
                           const std::size_t size) noexcept
            -> allocation_result;

    template <std::size_t idx>
    inline void deallocate(memory_allocation memblock) noexcept;

    // #MSVC_workaround fold expressions don't work within a noexcept() context
    static constexpr bool is_nothrow_default_constructible_v
            = (std::is_nothrow_default_constructible_v<Primary> && ...
               && std::is_nothrow_default_constructible_v<Fallbacks>);

    template <typename Init1, typename... Inits>
    static constexpr bool is_nothrow_constructible_v
            = (std::is_nothrow_constructible_v<Primary, Init1 &&> && ...
               && std::is_nothrow_constructible_v<Fallbacks, Inits &&>);

public:
    static constexpr std::size_t alignment
            = std::min({Primary::alignment, Fallbacks::alignment...});

    constexpr octopus_allocator() noexcept(is_nothrow_default_constructible_v)
            = default;
    template <typename Init1, typename... Inits>
    constexpr explicit octopus_allocator(
            std::in_place_t,
            Init1 &&init1,
            Inits &&...inits) noexcept(is_nothrow_constructible_v<Init1,
                                                                  Inits...>);

    inline auto allocate(const std::size_t size) noexcept -> allocation_result;
    inline auto reallocate(const memory_allocation memblock,
                           const std::size_t size) noexcept
            -> allocation_result;
    inline void deallocate(memory_allocation memblock) noexcept;

    inline auto owns(const memory_allocation memblock) noexcept -> bool;
};

template <typename Primary, typename... Fallbacks>
template <typename Init1, typename... Inits>
constexpr octopus_allocator<Primary, Fallbacks...>::octopus_allocator(
        std::in_place_t,
        Init1 &&init1,
        Inits &&...inits) noexcept(is_nothrow_constructible_v<Init1, Inits...>)
    : base_type(std::forward<Init1>(init1), std::forward<Inits>(inits)...)
{
}

template <typename Primary, typename... Fallbacks>
template <std::size_t idx>
inline auto octopus_allocator<Primary, Fallbacks...>::allocate(
        const std::size_t size) noexcept -> allocation_result
{
    auto &current = std::get<idx>(alloc_tuple());
    if constexpr (idx < num_bases - 1)
    {
        if (auto memblock = current.allocate(size); memblock.has_value())
        {
            return std::move(memblock);
        }
        return octopus_allocator::allocate<idx + 1>(size);
    }
    else
    {
        return current.allocate(size);
    }
}

template <typename Primary, typename... Fallbacks>
template <std::size_t ownerIdx, std::size_t idx>
inline auto octopus_allocator<Primary, Fallbacks...>::relocate(
        const memory_allocation memblock, const std::size_t size) noexcept
        -> allocation_result
{
    auto &current = std::get<idx>(alloc_tuple());
    if (auto reloc = current.allocate(size))
    {
        auto moveSize = std::min(memblock.size(), size);
        std::memmove(reloc.value().raw(), memblock.raw(), moveSize);

        auto owner = std::get<ownerIdx>(alloc_tuple());
        owner.deallocate(memblock);
        return reloc;
    }

    if constexpr (idx < num_bases - 1)
    {
        return octopus_allocator::relocate<ownerIdx, idx + 1>(memblock, size);
    }
    else
    {
        return std::errc::not_enough_memory;
    }
}

template <typename Primary, typename... Fallbacks>
template <std::size_t idx>
inline auto octopus_allocator<Primary, Fallbacks...>::reallocate(
        const memory_allocation memblock, const std::size_t size) noexcept
        -> allocation_result
{
    auto &current = std::get<idx>(alloc_tuple());
    if constexpr (idx < num_bases - 1)
    {
        if (current.owns(memblock))
        {
            if (auto reallocated = current.reallocate(memblock, size))
            {
                return reallocated;
            }
            return octopus_allocator::relocate<idx, idx + 1>(memblock, size);
        }
        return octopus_allocator::reallocate<idx + 1>(memblock, size);
    }
    else
    {
        return current.reallocate(memblock, size);
    }
}

template <typename Primary, typename... Fallbacks>
template <std::size_t idx>
inline void octopus_allocator<Primary, Fallbacks...>::deallocate(
        memory_allocation memblock) noexcept
{
    auto &current = std::get<idx>(alloc_tuple());
    if constexpr (idx < num_bases - 1)
    {
        if (current.owns(memblock))
        {
            current.deallocate(memblock);
        }
        else
        {
            octopus_allocator::deallocate<idx + 1>(memblock);
        }
    }
    else
    {
        current.deallocate(memblock);
    }
}

template <typename Primary, typename... Fallbacks>
inline auto octopus_allocator<Primary, Fallbacks...>::allocate(
        const std::size_t size) noexcept -> allocation_result
{
    return octopus_allocator::allocate<0>(size);
}

template <typename Primary, typename... Fallbacks>
inline auto octopus_allocator<Primary, Fallbacks...>::reallocate(
        const memory_allocation memblock, const std::size_t size) noexcept
        -> allocation_result
{
    return octopus_allocator::reallocate<0>(memblock, size);
}

template <typename Primary, typename... Fallbacks>
inline void octopus_allocator<Primary, Fallbacks...>::deallocate(
        memory_allocation memblock) noexcept
{
    octopus_allocator::deallocate<0>(memblock);
}

template <typename Primary, typename... Fallbacks>
inline auto octopus_allocator<Primary, Fallbacks...>::owns(
        const memory_allocation memblock) noexcept -> bool
{
    return std::apply([memblock](auto &...allocs)
                      { return (... || allocs.owns(memblock)); },
                      alloc_tuple());
}

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif
} // namespace vefs::detail

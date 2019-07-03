#pragma once

#include <utility>

#include <vefs/utils/ref_ptr.hpp>

#include <vefs/detail/cache_page.hpp>

namespace vefs::detail
{
    /**
     * A reference counting smart pointer to a cache_page element
     */
    template <typename T>
    class cache_handle : protected utils::aliasing_ref_ptr<T, cache_page<T>>
    {
        using base_type = utils::aliasing_ref_ptr<T, cache_page<T>>;

    public:
        using base_type::base_type;
        using base_type::operator=;
        using base_type::operator bool;
        using base_type::operator*;
        using base_type::operator->;
        using base_type::get;

        /**
         * Checks whether the referenced cache_page is marked as dirty.
         */
        inline auto is_dirty() const noexcept -> bool;
        /**
         * Marks the referenced cache_page as dirty.
         */
        inline auto mark_dirty() noexcept -> bool;
        /**
         * Clears the dirty bit of the referenced cache_page.
         */
        inline auto mark_clean() noexcept -> bool;

        friend inline void swap(cache_handle &lhs, cache_handle &rhs) noexcept
        {
            swap(static_cast<base_type &>(lhs), static_cast<base_type &>(rhs));
        }
    };

    template <typename T>
    inline auto cache_handle<T>::is_dirty() const noexcept -> bool
    {
        return base_type::get_handle()->is_dirty();
    }
    template <typename T>
    inline auto cache_handle<T>::mark_dirty() noexcept -> bool
    {
        return base_type::get_handle()->mark_dirty();
    }
    template <typename T>
    inline auto cache_handle<T>::mark_clean() noexcept -> bool
    {
        return base_type::get_handle()->mark_clean();
    }
} // namespace vefs::detail

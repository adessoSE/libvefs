#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <stdexcept>
#include <type_traits>

#include <boost/throw_exception.hpp>

namespace vefs
{
    template <typename T>
    class basic_range
    {
    public:
        static_assert(std::is_pod_v<T>);

        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = std::add_lvalue_reference_t<value_type>;
        using const_reference = std::add_const_t<reference>;
        using pointer = std::add_pointer_t<value_type>;
        using const_pointer = std::add_const_t<pointer>;

        static constexpr size_type npos = std::numeric_limits<size_type>::max();


        constexpr basic_range() noexcept;
        constexpr basic_range(pointer buffer, size_type size) noexcept;
        template <typename U, std::enable_if_t<std::is_pod_v<typename U::value_type> && sizeof(typename U::value_type) == sizeof(T), int> = 0>
        explicit constexpr basic_range(U &container);
        template <typename U, std::size_t S, std::enable_if_t<std::is_pod_v<U> && sizeof(U) == sizeof(T), int> = 0>
        explicit constexpr basic_range(U (&arr)[S]);
        constexpr basic_range(const basic_range &other) noexcept = default;

        constexpr basic_range & operator=(const basic_range &other) noexcept = default;
        constexpr basic_range & operator=(std::nullptr_t) noexcept;

        constexpr operator basic_range<std::add_const_t<value_type>>()
        {
            return { mBuffer, mBufferSize };
        }

        // element access
        constexpr reference at(size_type pos);
        constexpr const_reference at(size_type pos) const;

        constexpr reference operator[](size_type pos) noexcept;
        constexpr const_reference operator[](size_type pos) const noexcept;

        constexpr reference front() noexcept;
        constexpr const_reference front() const noexcept;

        constexpr reference back() noexcept;
        constexpr const_reference back() const noexcept;

        constexpr pointer data() noexcept;
        constexpr const_pointer data() const noexcept;

        template <typename U, bool checked = false>
        inline auto & as(size_type offset = size_type{}) noexcept(!checked);

        template <typename U, bool checked = false>
        inline auto & pop_front_as() noexcept(!checked);


        //TODO: iterators


        // capacity
        constexpr bool empty() const noexcept;
        constexpr explicit operator bool() const noexcept;

        constexpr size_type size() const noexcept;

        // modifiers
        constexpr void assign(pointer buffer, size_type size) noexcept;
        constexpr void swap(basic_range &other) noexcept;

        constexpr void remove_prefix(size_type n) noexcept;
        constexpr void remove_suffix(size_type n) noexcept;

        constexpr basic_range slice(size_type pos, size_type count = npos) const noexcept;

        inline void copy_to(basic_range<std::remove_const_t<value_type>> target) const noexcept;

    protected:
        template <typename U>
        basic_range(std::false_type, U *ptr, size_type size);
        template <typename U>
        constexpr basic_range(std::true_type, U *ptr, size_type size);

        pointer mBuffer;
        size_type mBufferSize;
    };


    template< typename T >
    constexpr basic_range<T>::basic_range() noexcept
        : basic_range(nullptr, 0)
    {
    }
    template< typename T >
    constexpr basic_range<T>::basic_range(pointer buffer, size_type size) noexcept
        : mBuffer(buffer)
        , mBufferSize(size)
    {
    }

    template <typename T>
    template <typename U, std::size_t S, std::enable_if_t<std::is_pod_v<U> && sizeof(U) == sizeof(T), int>>
    constexpr basic_range<T>::basic_range(U(& arr)[S])
        : basic_range(std::is_convertible<U *, pointer>{}, arr, S)
    {
    }

    template <typename T>
    template <typename U, std::enable_if_t<std::is_pod_v<typename U::value_type> && sizeof(typename U::value_type) == sizeof(T), int>>
    constexpr basic_range<T>::basic_range(U& container)
        : basic_range(std::is_convertible<typename U::value_type *, pointer>{}, container.data(), container.size())
    {
    }

    template <typename T>
    template <typename U>
    basic_range<T>::basic_range(std::false_type, U *ptr, size_type size)
        : basic_range(reinterpret_cast<pointer>(ptr), size)
    {
    }
    template <typename T>
    template <typename U>
    constexpr basic_range<T>::basic_range(std::true_type, U *ptr, size_type size)
        : basic_range(static_cast<pointer>(ptr), size)
    {
    }

    template<typename T>
    inline constexpr basic_range<T> & basic_range<T>::operator=(std::nullptr_t) noexcept
    {
        mBuffer = nullptr;
        mBufferSize = 0;
        return *this;
    }

    template <typename T>
    constexpr auto basic_range<T>::at(size_type pos)
        -> reference
    {
        if (pos >= mBufferSize)
        {
            BOOST_THROW_EXCEPTION(std::out_of_range("basic_range<T>::at() out of range index"));
        }
        return mBuffer[pos];
    }
    template <typename T>
    constexpr auto basic_range<T>::at(size_type pos) const
        -> const_reference
    {
        if (pos >= mBufferSize)
        {
            BOOST_THROW_EXCEPTION(std::out_of_range("basic_range<T>::at() out of range index"));
        }
        return mBuffer[pos];
    }

    template <typename T>
    template <typename U, bool checked>
    inline auto & basic_range<T>::as(size_type offset) noexcept(!checked)
    {
        static_assert(std::is_pod_v<U>);
        using target_type = std::conditional_t<std::is_const_v<T>, std::add_const_t<U>, U>;

        if constexpr (checked)
        {
            if (sizeof(U) > mBufferSize)
            {
                BOOST_THROW_EXCEPTION(std::out_of_range("basic_range<T>::as<U>() over the end cast"));
            }
        }

        return *reinterpret_cast<target_type *>(mBuffer + offset);
    }

    template <typename T>
    template <typename U, bool checked>
    inline auto & basic_range<T>::pop_front_as() noexcept(!checked)
    {
        auto & result = as<U, checked>(0);
        remove_prefix(sizeof(U));
        return result;
    }

    template <typename T>
    constexpr auto basic_range<T>::operator[](size_type pos) noexcept
        -> reference
    {
        return mBuffer[pos];
    }
    template <typename T>
    constexpr auto basic_range<T>::operator[](size_type pos) const noexcept
        -> const_reference
    {
        return mBuffer[pos];
    }

    template <typename T>
    constexpr auto basic_range<T>::front() noexcept
        -> reference
    {
        return mBuffer[0];
    }
    template <typename T>
    constexpr auto basic_range<T>::front() const noexcept
        -> const_reference
    {
        return mBuffer[0];
    }

    template <typename T>
    constexpr auto basic_range<T>::back() noexcept
        -> reference
    {
        return mBuffer[mBufferSize - 1];
    }
    template <typename T>
    constexpr auto basic_range<T>::back() const noexcept
        -> const_reference
    {
        return mBuffer[mBufferSize - 1];
    }

    template< typename T >
    constexpr auto basic_range<T>::data() noexcept
        -> pointer
    {
        return mBuffer;
    }
    template< typename T >
    constexpr auto basic_range<T>::data() const noexcept
        -> const_pointer
    {
        return mBuffer;
    }

    template< typename T >
    constexpr bool basic_range<T>::empty() const noexcept
    {
        return !mBuffer || !mBufferSize;
    }

    template< typename T >
    constexpr basic_range<T>::operator bool() const noexcept
    {
        return mBuffer && mBufferSize;
    }

    template< typename T >
    constexpr auto basic_range<T>::size() const noexcept
        -> size_type
    {
        return mBufferSize * !!mBuffer;
    }

    template< typename T >
    constexpr void basic_range<T>::assign(pointer buffer, size_type size) noexcept
    {
        mBuffer = buffer;
        mBufferSize = size;
    }

    template <typename T>
    constexpr void basic_range<T>::swap(basic_range& other) noexcept
    {
        basic_range tmp = *this;
        *this = other;
        other = tmp;
    }

    template <typename T>
    constexpr void basic_range<T>::remove_prefix(size_type n) noexcept
    {
        mBuffer += n;
        mBufferSize -= std::min(n, mBufferSize);
    }

    template <typename T>
    constexpr void basic_range<T>::remove_suffix(size_type n) noexcept
    {
        mBufferSize -= std::min(n, mBufferSize);
    }

    template <typename T>
    constexpr basic_range<T> basic_range<T>::slice(size_type pos, size_type count) const noexcept
    {
        return { mBuffer + pos, std::min(count, mBufferSize - pos) };
    }

    template <typename T>
    void basic_range<T>::copy_to(basic_range<std::remove_const_t<value_type>> target) const noexcept
    {
        // calling memmove with a nullptr is UB
        if (target && *this)
        {
            std::memmove(target.data(), mBuffer, std::min(mBufferSize, target.size()));
        }
    }


    template <typename T1, typename T2, std::enable_if_t<sizeof(T1) == sizeof(T2), int> = 0>
    bool equal(basic_range<T1> left, basic_range<T2> right)
    {
        if (left && right)
        {
            return std::equal(left.data(), left.data() + left.size(),
                        right.data(), right.data() + right.size());
        }
        else
        {
            return !left && !right;
        }
    }

    template <typename T1, typename T2, std::enable_if_t<sizeof(T1) == sizeof(T2), int> = 0>
    std::size_t mismatch(basic_range<T1> left, basic_range<T2> right)
    {
        if (left && right)
        {
            auto [l, r] = std::mismatch(left.data(), left.data() + left.size(),
                            right.data(), right.data() + right.size());
            (void)r;
            return l - left.data();
        }
        else
        {
            return 0;
        }
    }


    template <typename T>
    constexpr void swap(basic_range<T> &lhs, basic_range<T> &rhs) noexcept
    {
        lhs.swap(rhs);
    }


    using blob = basic_range<std::byte>;
    using blob_view = basic_range<const std::byte>;

    inline void fill_blob(blob target, std::byte value = std::byte{})
    {
        // calling memset with a nullptr is UB
        if (target)
        {
            std::memset(target.data(), std::to_integer<int>(value), target.size());
        }
    }

    template <typename T, std::enable_if_t<std::is_pod_v<T>, int> = 0>
    constexpr blob as_blob(T &obj)
    {
        return blob{ reinterpret_cast<blob::pointer>(&obj), sizeof(T) };
    }
    template <typename T, std::enable_if_t<std::is_pod_v<T>, int> = 0>
    constexpr blob_view as_blob_view(T &obj)
    {
        return blob_view{ reinterpret_cast<blob_view::pointer>(&obj), sizeof(T) };
    }

    inline namespace blob_literals
    {
        inline blob_view operator""_bv(const char *str, std::size_t strSize)
        {
            return blob_view{ reinterpret_cast<blob_view::pointer>(str), strSize };
        }
    }
}
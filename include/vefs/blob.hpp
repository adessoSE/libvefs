#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <stdexcept>
#include <type_traits>

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

        void copy_to(basic_range<std::remove_const_t<value_type>> target) const noexcept;

    protected:
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
        : basic_range(reinterpret_cast<pointer>(arr), S)
    {
    }

    template <typename T>
    template <typename U, std::enable_if_t<std::is_pod_v<typename U::value_type> && sizeof(typename U::value_type) == sizeof(T), int>>
    constexpr basic_range<T>::basic_range(U& container)
        : basic_range(reinterpret_cast<pointer>(container.data()), container.size())
    {
    }

    template <typename T>
    constexpr auto basic_range<T>::at(size_type pos)
        -> reference
    {
        if (pos >= mBufferSize)
        {
            throw std::out_of_range("basic_range<T>::at() out of range index");
        }
        return mBuffer[pos];
    }
    template <typename T>
    constexpr auto basic_range<T>::at(size_type pos) const
        -> const_reference
    {
        if (pos >= mBufferSize)
        {
            throw std::out_of_range("basic_range<T>::at() out of range index");
        }
        return mBuffer[pos];
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
        mBufferSize -= n;
    }

    template <typename T>
    constexpr void basic_range<T>::remove_suffix(size_type n) noexcept
    {
        mBufferSize -= n;
    }

    template <typename T>
    constexpr basic_range<T> basic_range<T>::slice(size_type pos, size_type count) const noexcept
    {
        return { mBuffer + pos, count };
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
            return left.size() == right.size()
                && std::equal(left.data(), left.data() + left.size(),
                    right.data() + right.size());
        }
        else
        {
            return !left && !right;
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
        return blob_view{ &obj, sizeof(T) };
    }

    inline blob_view operator""_bv(const char *str, std::size_t strSize)
    {
        return blob_view{ reinterpret_cast<blob_view::pointer>(str), strSize };
    }
}

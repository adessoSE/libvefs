#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace vefs
{
    constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

    template <typename T, std::size_t Extent = dynamic_extent>
    class span;

    namespace detail
    {
        template <typename T>
        struct is_span_impl : std::false_type
        {
        };
        template <typename T, std::size_t Extent>
        struct is_span_impl<span<T, Extent>> : std::true_type
        {
        };
        template <typename T>
        struct is_span : is_span_impl<std::remove_cv_t<T>>
        {
        };

        template <typename T>
        struct is_std_array_impl : std::false_type
        {
        };
        template <typename T, std::size_t N>
        struct is_std_array_impl<std::array<T, N>> : std::true_type
        {
        };
        template <typename T>
        struct is_std_array : is_std_array_impl<std::remove_cv_t<T>>
        {
        };

        template <typename From, typename To>
        inline constexpr bool is_span_compatible_storage_v =
            std::is_convertible_v<std::remove_pointer_t<From> (*)[], To (*)[]>;

        template <typename C, typename T, typename = void>
        struct is_span_compatible_container : std::false_type
        {
        };

        // clang-format off
        template <typename C, typename T>
        struct is_span_compatible_container<C, T, std::void_t<
                std::enable_if_t<!is_span<C>::value>,
                std::enable_if_t<!std::is_array_v<C>>,
                std::enable_if_t<!is_std_array<C>::value>,
                decltype(std::data(std::declval<C &>())),
                decltype(std::size(std::declval<C &>())),
                std::enable_if_t<is_span_compatible_storage_v<decltype(std::data(std::declval<C &>())), T>>
            >>
            : std::true_type
        {
        };
        // clang-format on

        template <typename C, typename T>
        inline constexpr bool is_span_compatible_container_v =
            is_span_compatible_container<C, T>::value;

        template <typename T, std::size_t Extent>
        class compressed_span_storage
        {
        public:
            constexpr auto data() const noexcept -> T *;
            constexpr auto size() const noexcept -> std::size_t;

        protected:
            constexpr compressed_span_storage(typename T *ptr, typename std::size_t count) noexcept;

        private:
            T *mData;
        };

        template <typename T, std::size_t Extent>
        constexpr compressed_span_storage<T, Extent>::compressed_span_storage(
            T *ptr, [[maybe_unused]] std::size_t count) noexcept
            : mData{ptr}
        {
            assert(ptr != nullptr);
            assert(count == Extent);
        }

        template <typename T, std::size_t Extent>
        constexpr auto compressed_span_storage<T, Extent>::data() const noexcept -> T *
        {
            return mData;
        }

        template <typename T, std::size_t Extent>
        constexpr auto compressed_span_storage<T, Extent>::size() const noexcept -> std::size_t
        {
            return Extent;
        }

        template <typename T>
        class compressed_span_storage<T, 0>
        {
        public:
            constexpr auto data() const noexcept -> T *;
            constexpr auto size() const noexcept -> std::size_t;

        protected:
            constexpr compressed_span_storage(typename T *ptr, typename std::size_t count) noexcept;
        };

        template <typename T>
        constexpr compressed_span_storage<T, 0>::compressed_span_storage(
            [[maybe_unused]] T *ptr, [[maybe_unused]] std::size_t count) noexcept
        {
            assert(ptr == nullptr);
            assert(count == 0);
        }

        template <typename T>
        constexpr auto compressed_span_storage<T, 0>::data() const noexcept -> T *
        {
            return nullptr;
        }

        template <typename T>
        constexpr auto compressed_span_storage<T, 0>::size() const noexcept -> std::size_t
        {
            return 0;
        }

        template <typename T>
        class compressed_span_storage<T, dynamic_extent>
        {
        public:
            constexpr auto data() const noexcept -> T *;
            constexpr auto size() const noexcept -> std::size_t;

        protected:
            constexpr compressed_span_storage(T *ptr, std::size_t count) noexcept;

        private:
            T *mData;
            std::size_t mSize;
        };

        template <typename T>
        constexpr compressed_span_storage<T, dynamic_extent>::compressed_span_storage(
            T *ptr, std::size_t count) noexcept
            : mData{ptr}
            , mSize{count}
        {
        }

        template <typename T>
        constexpr auto compressed_span_storage<T, dynamic_extent>::data() const noexcept -> T *
        {
            return mData;
        }

        template <typename T>
        constexpr auto compressed_span_storage<T, dynamic_extent>::size() const noexcept
            -> std::size_t
        {
            return mSize;
        }

    } // namespace detail

    template <typename T, std::size_t Extent>
    class span : private detail::compressed_span_storage<T, Extent>
    {
        using storage = detail::compressed_span_storage<T, Extent>;

    public:
        using element_type = T;
        using value_type = std::remove_cv_t<T>;

        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        using reference = T &;
        using pointer = T *;

        using iterator = pointer;
        using const_iterator = const pointer;

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        static constexpr size_type extent = Extent;

        static_assert(extent == dynamic_extent || extent < 0xFF'FFFF'FFFF,
                      "sorry, but I think you made a mistake in the span size computation");

    public:
        constexpr span() noexcept;
        constexpr span(std::nullptr_t) noexcept;
        constexpr span(pointer ptr, size_type count) noexcept;
        constexpr span(pointer rangeBegin, pointer rangeEnd) noexcept;

        template <std::size_t N>
        constexpr span(element_type (&arr)[N]) noexcept
            : span{std::data(arr), N}
        {
            static_assert(extent == dynamic_extent || N == extent);
        }
        template <std::size_t N>
        constexpr span(std::array<value_type, N> &arr) noexcept
            : span{std::data(arr), N}
        {
            static_assert(extent == dynamic_extent || N == extent);
        }
        template <std::size_t N>
        constexpr span(const std::array<value_type, N> &arr) noexcept
            : span{std::data(arr), N}
        {
            static_assert(extent == dynamic_extent || N == extent);
        }

        template <typename Container>
        constexpr span(
            Container &cont,
            std::enable_if_t<detail::is_span_compatible_container_v<Container, element_type>,
                             std::nullptr_t> = nullptr) noexcept(noexcept(std::data(cont)) ||
                                                                 noexcept(std::size(cont)))
            : span{std::data(cont), std::size(cont)}
        {
        }
        template <typename Container>
        constexpr span(
            const Container &cont,
            std::enable_if_t<detail::is_span_compatible_container_v<const Container, element_type>,
                             std::nullptr_t> = nullptr) noexcept(noexcept(std::data(cont)) ||
                                                                 noexcept(std::size(cont)))
            : span{std::data(cont), std::size(cont)}
        {
        }

        template <typename U, size_type N>
        constexpr span(const span<U, N> &other,
                       std::enable_if_t<detail::is_span_compatible_storage_v<U *, element_type>,
                                        std::nullptr_t> = nullptr) noexcept
            : span{std::data(other), std::size(other)}
        {
            static_assert(extent == dynamic_extent || N == extent);
        }
        constexpr span(const span &other) noexcept = default;

        constexpr span &operator=(const span &other) noexcept = default;

        constexpr auto begin() noexcept -> iterator;
        constexpr auto begin() const noexcept -> const_iterator;
        constexpr auto cbegin() const noexcept -> const_iterator;
        constexpr auto end() noexcept -> iterator;
        constexpr auto end() const noexcept -> const_iterator;
        constexpr auto cend() const noexcept -> const_iterator;

        constexpr auto rbegin() noexcept -> reverse_iterator;
        constexpr auto rbegin() const noexcept -> const_reverse_iterator;
        constexpr auto rcbegin() const noexcept -> const_reverse_iterator;
        constexpr auto rend() noexcept -> reverse_iterator;
        constexpr auto rend() const noexcept -> const_reverse_iterator;
        constexpr auto rcend() const noexcept -> const_reverse_iterator;

        // element access
        constexpr auto operator[](size_type pos) const noexcept -> reference;
        constexpr auto operator()(size_type pos) const noexcept -> reference;

        constexpr auto front() const noexcept -> reference;
        constexpr auto back() const noexcept -> reference;

        // constexpr auto data() const noexcept -> pointer;
        using storage::data;

        template <std::size_t Count>
        constexpr auto first() const noexcept -> span<element_type, Count>;
        constexpr auto first(size_type count) const -> span<element_type, dynamic_extent>;

        template <std::size_t Count>
        constexpr auto last() const noexcept -> span<element_type, Count>;
        constexpr auto last(size_type count) const -> span<element_type, dynamic_extent>;

        template <std::size_t Offset, std::size_t Count = dynamic_extent>
        constexpr auto subspan() const noexcept;
        constexpr auto subspan(size_type offset, size_type count = dynamic_extent) const
            -> span<element_type, dynamic_extent>;

        // capacity
        constexpr auto empty() const noexcept -> bool;
        constexpr explicit operator bool() const noexcept;

        // constexpr size_type size() const noexcept;
        using storage::size;
        constexpr size_type size_bytes() const noexcept;
    };

    template <class T, std::size_t N>
    span(T (&)[N])->span<T, N>;

    template <class T, std::size_t N>
    span(std::array<T, N> &)->span<T, N>;

    template <class T, std::size_t N>
    span(const std::array<T, N> &)->span<const T, N>;

    template <class Container>
    span(Container &)->span<typename Container::value_type>;

    template <class Container>
    span(const Container &)->span<const typename Container::value_type>;

    template <class CharT, class Traits>
    span(std::basic_string_view<CharT, Traits> &)
        ->span<const typename std::basic_string_view<CharT, Traits>::value_type>;

    template <typename T, std::size_t Extent>
    inline constexpr span<T, Extent>::span() noexcept
        : storage{nullptr, 0}
    {
        static_assert(extent == dynamic_extent || extent == 0,
                      "Only dynamic and zero sized spans are default constructible!");
    }
    template <typename T, std::size_t Extent>
    inline constexpr span<T, Extent>::span(std::nullptr_t) noexcept
        : storage{nullptr, 0}
    {
        static_assert(
            extent == dynamic_extent || extent == 0,
            "Only dynamic and zero sized spans are constructible from a nullptr literal!");
    }
    template <typename T, std::size_t Extent>
    inline constexpr span<T, Extent>::span(pointer ptr, size_type size) noexcept
        : storage{size ? ptr : nullptr, size}
    {
    }
    template <typename T, std::size_t Extent>
    inline constexpr span<T, Extent>::span(pointer rangeBegin, pointer rangeEnd) noexcept
        : span{rangeBegin, static_cast<size_type>(rangeEnd - rangeBegin)}
    {
        assert(rangeBegin <= rangeEnd);
    }

    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::begin() noexcept -> iterator
    {
        return iterator(data());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::begin() const noexcept -> const_iterator
    {
        return const_iterator(data());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::cbegin() const noexcept -> const_iterator
    {
        return const_iterator(data());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::end() noexcept -> iterator
    {
        return iterator(data() + size());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::end() const noexcept -> const_iterator
    {
        return const_iterator(data() + size());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::cend() const noexcept -> const_iterator
    {
        return const_iterator(data() + size());
    }

    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::rbegin() noexcept -> reverse_iterator
    {
        return reverse_iterator(data() + size());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::rbegin() const noexcept -> const_reverse_iterator
    {
        return const_reverse_iterator(data() + size());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::rcbegin() const noexcept -> const_reverse_iterator
    {
        return const_reverse_iterator(data() + size());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::rend() noexcept -> reverse_iterator
    {
        return reverse_iterator(data());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::rend() const noexcept -> const_reverse_iterator
    {
        return const_reverse_iterator(data());
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::rcend() const noexcept -> const_reverse_iterator
    {
        return const_reverse_iterator(data());
    }

    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::operator[](size_type pos) const noexcept -> reference
    {
        return data()[pos];
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::operator()(size_type pos) const noexcept -> reference
    {
        return data()[pos];
    }

    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::front() const noexcept -> reference
    {
        static_assert(/*extent == dynamic_extent ||*/ extent > 0);
        if constexpr (extent == dynamic_extent)
        {
            assert(size() > 0);
        }
        return data()[0];
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::back() const noexcept -> reference
    {
        static_assert(/*extent == dynamic_extent ||*/ extent > 0);
        if constexpr (extent == dynamic_extent)
        {
            assert(size() > 0);
        }
        return data()[size() - 1];
    }

    template <typename T, std::size_t Extent>
    template <std::size_t Count>
    inline constexpr auto span<T, Extent>::first() const noexcept -> span<element_type, Count>
    {
        static_assert(/*extent == dynamic_extent ||*/ extent >= Count);
        if constexpr (extent == dynamic_extent)
        {
            assert(size() >= Count);
        }
        return span<element_type, Count>(data(), Count);
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::first(size_type count) const
        -> span<element_type, dynamic_extent>
    {
        assert(/*extent == dynamic_extent ||*/ extent >= count);
        return span(data(), count);
    }

    template <typename T, std::size_t Extent>
    template <std::size_t Count>
    inline constexpr auto span<T, Extent>::last() const noexcept -> span<element_type, Count>
    {
        static_assert(/*extent == dynamic_extent ||*/ extent >= Count);
        if constexpr (extent == dynamic_extent)
        {
            assert(size() >= Count);
        }
        return span<element_type, Count>(data() + size() - Count, Count);
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::last(size_type count) const
        -> span<element_type, dynamic_extent>
    {
        assert(extent >= count);
        return span(data() + size() - count, count);
    }

    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::empty() const noexcept -> bool
    {
        return size() == 0;
    }

    template <typename T, std::size_t Extent>
    inline constexpr span<T, Extent>::operator bool() const noexcept
    {
        return size() != 0;
    }

    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::size_bytes() const noexcept -> size_type
    {
        return size() * sizeof(element_type);
    }

    template <typename T, std::size_t Extent>
    template <std::size_t Offset, std::size_t Count>
    inline constexpr auto span<T, Extent>::subspan() const noexcept
    {
        static_assert(extent == dynamic_extent || Offset <= extent);

        if constexpr (Count == dynamic_extent)
        {
            if constexpr (extent == dynamic_extent)
            {
                assert(Offset + Count <= size());
                return span<element_type, dynamic_extent>(data() + Offset, size() - Offset);
            }
            else
            {
                return span<element_type, extent - Offset>(data() + Offset, extent - Offset);
            }
        }
        else
        {
            static_assert(Offset + Count <= extent);
            if constexpr (extent == dynamic_extent)
            {
                assert(Offset + Count <= size());
            }
            return span<element_type, Count>(data() + Offset, Count);
        }
    }
    template <typename T, std::size_t Extent>
    inline constexpr auto span<T, Extent>::subspan(size_type offset, size_type count) const
        -> span<element_type, dynamic_extent>
    {
        assert(offset <= size());
        assert(count == dynamic_extent || offset + count <= size());
        const auto subsize = count != dynamic_extent ? count : size() - offset;
        return span<element_type, dynamic_extent>(data() + offset, subsize);
    }

    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto equal(span<T, X> left, span<U, Y> right) -> bool
    {
        if (left && right)
        {
            return std::equal(left.cbegin(), left.cend(), right.cbegin(), right.cend());
        }
        else
        {
            return !left && !right;
        }
    }

    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto operator<(span<T, X> l, span<U, Y> r) -> bool
    {
        return std::lexicographical_compare(l.cbegin(), l.cend(), r.cbegin(), r.cend());
    }
    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto operator<=(span<T, X> l, span<U, Y> r) -> bool
    {
        return !(r < l);
    }
    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto operator>(span<T, X> l, span<U, Y> r) -> bool
    {
        return (r < l);
    }
    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto operator>=(span<T, X> l, span<U, Y> r) -> bool
    {
        return !(l < r);
    }

    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto mismatch(span<T, X> left, span<U, Y> right)
    {
        return std::mismatch(left.cbegin(), left.cend(), right.cbegin(), right.cend());
    }

    template <class T, std::size_t X, class U, std::size_t Y>
    inline constexpr auto mismatch_distance(span<T, X> left, span<U, Y> right) -> std::size_t
    {
        if (left && right)
        {
            auto [l, r] = mismatch(left, right);
            (void)r;
            return std::distance(left.cbegin(), l);
        }
        else
        {
            return 0;
        }
    }

    template <class T, std::size_t X, class U, std::size_t Y>
    inline auto copy(span<T, X> source, span<U, Y> dest)
    {
        assert(dest.data() < source.data() || source.data() + source.size() <= dest.data());
        if constexpr (X == dynamic_extent || Y == dynamic_extent)
        {
            const auto n = std::min(source.size(), dest.size());
            std::copy_n(source.cbegin(), n, dest.begin());
            return dest.subspan(n);
        }
        else
        {
            constexpr auto N = std::min(X, Y);
            std::copy_n(source.cbegin(), N, dest.begin());
            return dest.subspan<N>();
        }
    }

    template <typename T, std::size_t Extent>
    inline constexpr void swap(span<T, Extent> &lhs, span<T, Extent> &rhs) noexcept
    {
        auto tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }

    template <std::size_t Extent>
    using rw_blob = span<std::byte, Extent>;
    using rw_dynblob = rw_blob<dynamic_extent>;

    template <std::size_t Extent>
    using ro_blob = span<const std::byte, Extent>;
    using ro_dynblob = ro_blob<dynamic_extent>;

    template <typename T, std::size_t N>
    inline auto as_bytes(span<T, N> s)
        -> ro_blob<(N == dynamic_extent ? dynamic_extent : sizeof(T) * N)>
    {
        return {reinterpret_cast<const std::byte *>(s.data()), s.size_bytes()};
    }
    template <typename T, std::size_t N>
    inline auto as_writable_bytes(span<T, N> s)
        -> rw_blob<(N == dynamic_extent ? dynamic_extent : sizeof(T) * N)>
    {
        static_assert(!std::is_const_v<T>, "Obtaining a writable span requires a non const object");
        return {reinterpret_cast<std::byte *>(s.data()), s.size_bytes()};
    }

    template <std::size_t Extent>
    inline void fill_blob(rw_blob<Extent> target, std::byte value = std::byte{})
    {
        // calling memset with a nullptr is UB
        if (target)
        {
            std::memset(target.data(), std::to_integer<int>(value), target.size());
        }
    }

    template <typename T>
    inline auto rw_blob_cast(T &obj) -> rw_blob<sizeof(obj)>
    {
        static_assert(!std::is_const_v<T>, "Obtaining a writable span requires a non const object");
        return {reinterpret_cast<std::byte *>(&obj), sizeof(obj)};
    }
    template <typename T>
    inline auto ro_blob_cast(const T &obj) -> ro_blob<sizeof(T)>
    {
        return {reinterpret_cast<const std::byte *>(&obj), sizeof(obj)};
    }
} // namespace vefs

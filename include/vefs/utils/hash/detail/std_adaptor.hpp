#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <string_view>
#include <type_traits>

#include <vefs/blob.hpp>
#include <vefs/utils/uuid.hpp>
#include <vefs/utils/hash/algorithm_tag.hpp>

namespace vefs::utils
{
    template <typename T, typename Impl, typename H,
        std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>, int> = 0>
    inline void compute_hash(const T &obj, H &h, hash::algorithm_tag<Impl>)
    {
        auto ptr = reinterpret_cast<const std::byte *>(&obj);
        Impl::compute(blob_view{ ptr, sizeof(T) }, h);
    }

    template <typename T, typename Impl,
        std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>, int> = 0>
    inline void compute_hash(const T &obj, Impl &state)
    {
        auto ptr = reinterpret_cast<const std::byte *>(&obj);
        state.update(blob_view{ ptr, sizeof(T) });
    }


    template <typename T, typename Impl, typename H>
    inline void compute_hash(const std::shared_ptr<T> &obj, H &h, hash::algorithm_tag<Impl>)
    {
        compute_hash(obj.get(), h, hash::algorithm_tag<Impl>{});
    }

    template <typename T, typename Impl>
    inline void compute_hash(const std::shared_ptr<T> &obj, Impl &state)
    {
        compute_hash(obj.get(), state);
    }


    template <typename T, typename Impl, typename H>
    inline void compute_hash(const std::unique_ptr<T> &obj, H &h, hash::algorithm_tag<Impl>)
    {
        compute_hash(obj.get(), h, hash::algorithm_tag<Impl>{});
    }

    template <typename T, typename Impl>
    inline void compute_hash(const std::unique_ptr<T> &obj, Impl &state)
    {
        compute_hash(obj.get(), state);
    }


    template <typename T, typename Impl, typename H>
    inline void compute_hash(const std::basic_string_view<T> &obj, H &h, hash::algorithm_tag<Impl>)
    {
        auto ptr = reinterpret_cast<const std::byte *>(obj.data());
        auto size = obj.size() * sizeof(T);
        Impl::compute(blob_view{ ptr, size }, h);
    }

    template <typename T, typename Impl>
    inline void compute_hash(const std::basic_string_view<T> &obj, Impl &state)
    {
        auto ptr = reinterpret_cast<const std::byte *>(obj.data());
        auto size = obj.size() * sizeof(T);
        state.update(blob_view{ ptr, size });
    }


    template <typename Impl, typename H>
    inline void compute_hash(const vefs::utils::uuid &obj, H &h, hash::algorithm_tag<Impl>)
    {
        auto ptr = reinterpret_cast<const std::byte *>(obj.begin());
        Impl::compute(blob_view{ ptr, obj.static_size() }, h);
    }

    template <typename Impl>
    inline void compute_hash(const vefs::utils::uuid &obj, Impl &state)
    {
        auto ptr = reinterpret_cast<const std::byte *>(obj.begin());
        state.update(blob_view{ ptr, obj.static_size() });
    }
}

namespace vefs::utils::hash::detail
{
    template <typename T, typename Impl, typename = void>
    struct has_stateless_compute_t
        : std::false_type {};

    template <typename T, typename Impl>
    struct has_stateless_compute_t<T, Impl,
        std::void_t<decltype(compute_hash(std::declval<const T &>(), std::declval<std::size_t &>(),
                                          std::declval<algorithm_tag<Impl>>()))>>
        : std::true_type {};

    template <typename T, typename Impl>
    constexpr bool has_stateless_compute = has_stateless_compute_t<T, Impl>::value;


    template <typename T, typename Impl>
    struct std_adaptor;

    template <typename Impl>
    struct std_adaptor<void, Impl>
    {
        template <typename T>
        inline std::size_t operator()(const T &obj)
        {
            std::size_t h = 0;
            if constexpr (has_stateless_compute<T, Impl>)
            {
                compute_hash(obj, h, algorithm_tag<Impl>{});
            }
            else
            {
                Impl state{};
                state.init();
                compute_hash(obj, state);

                state.final(h);
            }
            return h;
        }
    };

    template <typename T, typename Impl>
    struct std_adaptor : private std_adaptor<void, Impl>
    {
        inline std::size_t operator()(const T &obj)
        {
            return std_adaptor<void, Impl>::operator()(obj);
        }
    };
}

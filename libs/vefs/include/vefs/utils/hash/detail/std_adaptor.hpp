#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <string_view>
#include <type_traits>

#include <vefs/blob.hpp>
#include <vefs/utils/hash/algorithm_tag.hpp>
#include <vefs/utils/uuid.hpp>

namespace vefs::utils
{
    template <typename T, typename Impl, typename H,
              std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>, int> = 0>
    inline void compute_hash(const T &obj, H &h, hash::algorithm_tag<Impl>)
    {
        Impl::compute(ro_blob_cast(obj), h);
    }

    template <typename T, typename Impl,
              std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>, int> = 0>
    inline void compute_hash(const T &obj, Impl &state)
    {
        state.update(ro_blob_cast(obj));
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
        compute_hash(*obj, h, hash::algorithm_tag<Impl>{});
    }

    template <typename T, typename Impl>
    inline void compute_hash(const std::unique_ptr<T> &obj, Impl &state)
    {
        compute_hash(*obj, state);
    }

    template <typename T, typename Impl, typename H>
    inline void compute_hash(std::basic_string_view<T> obj, H &h, hash::algorithm_tag<Impl>)
    {
        Impl::compute(as_bytes(span(obj)), h);
    }

    template <typename T, typename Impl>
    inline void compute_hash(std::basic_string_view<T> obj, Impl &state)
    {
        state.update(as_bytes(span(obj)));
    }

    template <typename Impl, typename H>
    inline void compute_hash(const vefs::utils::uuid &obj, H &h, hash::algorithm_tag<Impl>)
    {
        Impl::compute(as_bytes(span(obj.data)), h);
    }

    template <typename Impl>
    inline void compute_hash(const vefs::utils::uuid &obj, Impl &state)
    {
        state.update(as_bytes(span(obj.data)));
    }
} // namespace vefs::utils

namespace vefs::utils::hash::detail
{
    template <typename T, typename Impl, typename = void>
    struct has_stateless_compute_t : std::false_type
    {
    };

    template <typename T, typename Impl>
    struct has_stateless_compute_t<
        T, Impl,
        std::void_t<decltype(compute_hash(std::declval<const T &>(), std::declval<std::size_t &>(),
                                          std::declval<algorithm_tag<Impl>>()))>> : std::true_type
    {
    };

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
} // namespace vefs::utils::hash::detail

#pragma once

#include <cstddef>

#include <array>
#include <memory>
#include <type_traits>

#include <vefs/utils/misc.hpp>

namespace vefs::utils
{
    template <typename T,
              std::size_t Alignment = alignof(utils::remove_cvref_t<T>)>
    class object_storage
    {
    public:
        using value_type = utils::remove_cvref_t<T>;
        using reference_type = value_type &;
        using pointer_type = value_type *;

        static constexpr std::size_t alignment = Alignment;
        static constexpr std::size_t size = sizeof(value_type);

        object_storage() noexcept = default;
        object_storage(const object_storage &) = delete;
        object_storage &operator=(const object_storage &) = delete;

        template <typename... Args>
        auto construct(Args &&... args) noexcept(
            std::is_nothrow_constructible_v<value_type, Args...>)
            -> reference_type
        {
            return *new (static_cast<void *>(mStorage.data()))
                value_type(std::forward<Args>(args)...);
        }
        void destroy() noexcept
        {
            if constexpr (!std::is_trivially_constructible_v<value_type>)
            {
                std::destroy_at(pointer());
            }
        }

        auto value() noexcept -> reference_type
        {
            return *pointer();
        }
        auto value() const noexcept -> const value_type &
        {
            return *pointer();
        }

        auto pointer() noexcept -> pointer_type
        {
            return std::launder(reinterpret_cast<pointer_type>(mStorage.data()));
        }
        auto pointer() const noexcept -> const value_type *
        {
            return std::launder(
                reinterpret_cast<const value_type *>(mStorage.data()));
        }

    private:
        alignas(alignment) std::array<std::byte, size> mStorage;
    };
} // namespace vefs::utils

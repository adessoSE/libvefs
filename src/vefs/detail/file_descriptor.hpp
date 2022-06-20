#pragma once

#include <memory>
#include <string>

#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>

#include <dplx/dp/object_def.hpp>

#include <vefs/platform/secure_memzero.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/uuid.hpp>

#include "file_crypto_ctx.hpp"
#include "root_sector_info.hpp"
#include "sector_id.hpp"

namespace vefs::detail
{
struct file_descriptor
{
    uuid fileId;

    std::array<std::byte, 32> secret;
    crypto::counter secretCounter;
    root_sector_info data;

    std::u8string filePath;

    // ISO 8601 encoded date time string
    std::u8string modificationTime;

    ~file_descriptor() noexcept
    {
        utils::secure_data_erase(secret);
        utils::secure_data_erase(fileId);
        utils::secure_data_erase(data);
    }

    file_descriptor(file_descriptor &&) noexcept = default;
    auto operator=(file_descriptor &&) noexcept -> file_descriptor & = default;

    file_descriptor() noexcept = default;
    file_descriptor(uuid id,
                    file_crypto_ctx const &ctx,
                    root_sector_info root) noexcept
        : fileId(id)
        , data(root)
        , filePath()
        , modificationTime()
    {
        auto ctxState = ctx.state();
        vefs::copy(as_span(ctxState.secret), std::span(secret));
        secretCounter = ctxState.counter;
    }

#if BOOST_PREDEF_TESTED_AT(BOOST_COMP_MSVC, 19, 29, 30037)
    struct root_sector_accessor
        : dplx::dp::member_accessor_base<file_descriptor, sector_id>
    {
        auto operator()(auto &self) const noexcept
        {
            return &self.data.root.sector;
        }
    };
    struct root_mac_accessor
        : dplx::dp::member_accessor_base<file_descriptor,
                                         std::array<std::byte, 16>>
    {
        auto operator()(auto &self) const noexcept
        {
            return &self.data.root.mac;
        }
    };
    struct max_extent_accessor
        : dplx::dp::member_accessor_base<file_descriptor, std::uint64_t>
    {
        auto operator()(auto &self) const noexcept
        {
            return &self.data.maximum_extent;
        }
    };
    struct tree_depth_accessor
        : dplx::dp::member_accessor_base<file_descriptor, int>
    {
        auto operator()(auto &self) const noexcept
        {
            return &self.data.tree_depth;
        }
    };
#endif

    static constexpr dplx::dp::object_def<
            dplx::dp::property_def<1U, &file_descriptor::fileId>{},
            dplx::dp::property_def<2U, &file_descriptor::filePath>{},
            dplx::dp::property_def<3U, &file_descriptor::secret>{},
            dplx::dp::property_def<4U, &file_descriptor::secretCounter>{},
#if BOOST_PREDEF_TESTED_AT(BOOST_COMP_MSVC, 19, 29, 30037)
            dplx::dp::property_fun<5U, root_sector_accessor>{},
            dplx::dp::property_fun<6U, root_mac_accessor>{},
            dplx::dp::property_fun<7U, max_extent_accessor>{},
            dplx::dp::property_fun<8U, tree_depth_accessor>{},
#else
            dplx::dp::property_def<5U,
                                   &file_descriptor::data,
                                   &root_sector_info::root,
                                   &sector_reference::sector>{},
            dplx::dp::property_def<6U,
                                   &file_descriptor::data,
                                   &root_sector_info::root,
                                   &sector_reference::mac>{},
            dplx::dp::property_def<7U,
                                   &file_descriptor::data,
                                   &root_sector_info::maximum_extent>{},
            dplx::dp::property_def<8U,
                                   &file_descriptor::data,
                                   &root_sector_info::tree_depth>{},
#endif
            dplx::dp::property_def<9U, &file_descriptor::modificationTime>{}>
            layout_descriptor{};
};

} // namespace vefs::detail

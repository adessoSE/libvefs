#pragma once

#include <cstddef>
#include <cstdint>

#include <vefs/blob.hpp>
#include <vefs/utils/hash/algorithm_tag.hpp>
#include <vefs/utils/hash/detail/SpookyV2_impl.hpp>

namespace vefs::utils::hash::detail
{
    class spooky
    {
    public:
        inline static void compute(vefs::blob_view data, std::uint32_t &hash);
        inline static void compute(vefs::blob_view data, std::uint64_t &hash);

        inline void init();
        inline void update(vefs::blob_view data);

        inline void final(std::uint32_t &hash);
        inline void final(std::uint64_t &hash);

    private:
        impl::SpookyHash mState;
    };

    inline void spooky::compute(vefs::blob_view data, std::uint32_t &hash)
    {
        hash = impl::SpookyHash::Hash32(data.data(), data.size(), 0);
    }
    inline void spooky::compute(vefs::blob_view data, std::uint64_t &hash)
    {
        hash = impl::SpookyHash::Hash64(data.data(), data.size(), 0);
    }

    inline void spooky::init()
    {
        mState.Init(0, 0);
    }

    inline void spooky::update(vefs::blob_view data)
    {
        mState.Update(data.data(), data.size());
    }

    inline void spooky::final(std::uint32_t &hash)
    {
        std::uint64_t h1, h2;
        mState.Final(&h1, &h2);
        hash = static_cast<std::uint32_t>(h1);
    }
    inline void spooky::final(std::uint64_t &hash)
    {
        std::uint64_t h2;
        mState.Final(&hash, &h2);
    }
}

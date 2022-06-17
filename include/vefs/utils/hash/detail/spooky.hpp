#pragma once

#include <cstddef>
#include <cstdint>

#include <vefs/hash/detail/spooky_v2_impl.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/hash/algorithm_tag.hpp>

namespace vefs::utils::hash::detail
{
class spooky
{
public:
    inline static void compute(vefs::ro_dynblob data, std::uint32_t &hash);
    inline static void compute(vefs::ro_dynblob data, std::uint64_t &hash);

    inline void init();
    inline void update(vefs::ro_dynblob data);

    inline void final(std::uint32_t &hash);
    inline void final(std::uint64_t &hash);

private:
    external::SpookyHash mState;
};

inline void spooky::compute(vefs::ro_dynblob data, std::uint32_t &hash)
{
    hash = external::SpookyHash::Hash32(data.data(), data.size(), 0);
}
inline void spooky::compute(vefs::ro_dynblob data, std::uint64_t &hash)
{
    hash = external::SpookyHash::Hash64(data.data(), data.size(), 0);
}

inline void spooky::init()
{
    mState.Init(0, 0);
}

inline void spooky::update(vefs::ro_dynblob data)
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
} // namespace vefs::utils::hash::detail

#pragma once

#include <boost/container/small_vector.hpp>

#include <vefs/disappointment.hpp>

#include "sector_id.hpp"

namespace vefs::detail
{
/**
 * Allocates sectors from a pool of sectors assigned to the allocator at the
 * beginning of its lifecycle.
 */
class preallocated_tree_allocator
{
public:
    using sector_id_container = boost::container::small_vector<sector_id, 128>;

    class sector_allocator;

    enum class leak_on_failure_t
    {
    };
    static constexpr auto leak_on_failure = leak_on_failure_t{};

    explicit preallocated_tree_allocator(sector_id_container &ids) noexcept;

    auto reallocate(sector_allocator &part) noexcept -> result<sector_id>;

    auto dealloc_one(sector_id const which) noexcept -> result<void>;
    void dealloc_one(sector_id const which, leak_on_failure_t) noexcept;

    auto on_commit() noexcept -> result<void>;

    void on_leak_detected() noexcept;
    auto leaked() -> bool;
    void reset_leak_flag();

private:
    sector_id_container &mIds;
    bool mLeaked;
};

class preallocated_tree_allocator::sector_allocator
{
    friend class preallocated_tree_allocator;

public:
    explicit sector_allocator(preallocated_tree_allocator &,
                              sector_id current) noexcept;

private:
    sector_id mCurrent;
};
} // namespace vefs::detail

#include "precompiled.hpp"
#include <vefs/archive.hpp>

#include <cstdint>
#include <cassert>

#include <stack>
#include <tuple>
#include <array>
#include <chrono>
#include <sstream>
#include <string_view>


#include <boost/dynamic_bitset.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/iterator/iterator_facade.hpp>

#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/tree_walker.hpp>
#include <vefs/detail/thread_pool.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/bitset_overlay.hpp>

#include "archive_file.hpp"
#include "archive_file_lookup.hpp"
#include "archive_index_file.hpp"
#include "archive_free_block_list_file.hpp"
#include "proto-helper.hpp"

using namespace std::string_view_literals;

using namespace vefs::detail;

namespace vefs
{
    archive::file * deref(const archive::file_handle &handle) noexcept
    {
        assert(handle);
        return handle.mData->mWorkingSet.load(std::memory_order_acquire);
    }

    archive::archive()
        : mWorkTracker{ &thread_pool::shared() }
    {
    }

    archive::archive(std::unique_ptr<detail::raw_archive> primitives)
        : mWorkTracker{ &thread_pool::shared() }
        , mArchive{ std::move(primitives) }
    {
    }

    auto archive::open(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider * cryptoProvider, ro_blob<32> userPRK, file_open_mode_bitset openMode)
        -> result<std::unique_ptr<archive>>
    {
        BOOST_OUTCOME_TRY(primitives,
            raw_archive::open(fs, archivePath, cryptoProvider, userPRK, openMode));

        std::unique_ptr<archive> arc{ new archive(std::move(primitives)) };

        const auto createNew = openMode % file_open_mode::create;
        if (createNew)
        {
            if (auto fblrx = free_block_list_file::create_new(*arc))
            {
                arc->mFreeBlockIndexFile = std::move(fblrx).assume_value();
            }
            else
            {
                fblrx.assume_error() << ed::archive_file{ "[free-block-list]" };
                return std::move(fblrx).as_failure();
            }
            if (auto aixrx = index_file::create_new(*arc))
            {
                arc->mArchiveIndexFile = std::move(aixrx).assume_value();
            }
            else
            {
                aixrx.assume_error() << ed::archive_file{ "[archive-index]" };
                return std::move(aixrx).as_failure();
            }
        }
        else
        {
            if (auto fblrx = free_block_list_file::open(*arc))
            {
                arc->mFreeBlockIndexFile = std::move(fblrx).assume_value();
            }
            else
            {
                fblrx.assume_error() << ed::archive_file{ "[free-block-list]" };
                return std::move(fblrx).as_failure();
            }
            if (auto aixrx = index_file::open(*arc))
            {
                arc->mArchiveIndexFile = std::move(aixrx).assume_value();
            }
            else
            {
                aixrx.assume_error() << ed::archive_file{ "[archive-index]" };
                return std::move(aixrx).as_failure();
            }
        }
        return std::move(arc);
    }

    archive::~archive()
    {
        (void)sync();

        mArchiveIndexFile->dispose();
        mArchiveIndexFile = nullptr;
        mFreeBlockIndexFile->dispose();
        mFreeBlockIndexFile = nullptr;
        mWorkTracker.wait();
    }

    auto archive::sync()
        -> result<void>
    {
        BOOST_OUTCOME_TRY(changes, mArchiveIndexFile->sync(true));
        if (!changes)
        {
            return outcome::success();
        }

        VEFS_TRY_INJECT(mFreeBlockIndexFile->sync(), ed::archive_file{ "[free-block-list]" });
        VEFS_TRY_INJECT(mArchive->update_header(), ed::archive_file{ "[archive-index]" });
        return mArchive->sync();
    }

    void archive::sync_async(std::function<void(op_outcome<void>)> cb)
    {
        ops_pool().execute([this, cb = std::move(cb)]()
        {
            auto einfo = collect_disappointment([&]()
            {
                return sync();
            });
            cb(std::move(einfo));
        });
    }

    auto archive::open(const std::string_view filePath, const file_open_mode_bitset mode)
        -> result<archive::file_handle>
    {
        return mArchiveIndexFile->open(filePath, mode);
    }

    auto archive::query(const std::string_view filePath)
        -> result<file_query_result>
    {
        return mArchiveIndexFile->query(filePath);
    }

    auto archive::erase(std::string_view filePath)
        -> result<void>
    {
        return mArchiveIndexFile->erase(filePath);
    }

    auto archive::read(file_handle handle, rw_dynblob buffer, std::uint64_t readFilePos)
        -> result<void>
    {
        if (!buffer)
        {
            return outcome::success();
        }
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }
        auto readrx = f->read(buffer, readFilePos);
        if (readrx.has_error())
        {
            readrx.assume_error()
                << ed::archive_file_read_area{ ed::file_span{ readFilePos, readFilePos + buffer.size() } }
            << ed::archive_file{ handle.value()->name() };
        }
        return std::move(readrx);
    }

    auto archive::write(file_handle handle, ro_dynblob data, std::uint64_t writeFilePos)
        -> result<void>
    {
        if (!data)
        {
            return outcome::success();
        }
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        auto writerx = f->write(data, writeFilePos);
        if (writerx.has_error())
        {
            writerx.assume_error()
                << ed::archive_file_write_area{ ed::file_span{ writeFilePos, writeFilePos + data.size() } }
                << ed::archive_file{ handle.value()->name() };
        }
        return std::move(writerx);
    }

    auto archive::resize(file_handle handle, std::uint64_t size)
        -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        auto resizerx = f->resize(size);
        if (resizerx.has_error())
        {
            resizerx.assume_error()
                << ed::archive_file{ handle.value()->name() };
        }
        return std::move(resizerx);
    }

    auto archive::size_of(file_handle handle)
        -> result<std::uint64_t>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        return f->size();
    }

    auto archive::sync(file_handle handle)
        -> result<void>
    {
        if (!handle)
        {
            return errc::invalid_argument;
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            return errc::invalid_argument;
        }

        auto syncrx = f->sync();
        if (syncrx.has_error())
        {
            syncrx.assume_error()
                << ed::archive_file{ handle.value()->name() };
        }
        return std::move(syncrx);
    }

    void archive::erase_async(std::string filePath, std::function<void(op_outcome<void>)> cb)
    {
        ops_pool().execute([this, filePath = std::move(filePath), cb = std::move(cb)]()
        {
            auto rx = collect_disappointment([&]()
            {
                return erase(filePath);
            });
            cb(std::move(rx));
        });
    }

    void archive::read_async(file_handle handle, rw_dynblob buffer, std::uint64_t readFilePos, std::function<void(op_outcome<void>)> cb)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        ops_pool().execute([this, h = std::move(handle), buffer, readFilePos, cb = std::move(cb)]()
        {
            auto rx = collect_disappointment([&]()
            {
                return read(std::move(h), buffer, readFilePos);
            });
            cb(std::move(rx));
        });
    }

    void archive::write_async(file_handle handle, ro_dynblob data, std::uint64_t writeFilePos, std::function<void(op_outcome<void>)> cb)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        ops_pool().execute([this, h = std::move(handle), data, writeFilePos, cb = std::move(cb)]()
        {
            auto rx = collect_disappointment([&]()
            {
                return write(std::move(h), data, writeFilePos);
            });
            cb(std::move(rx));
        });
    }

    void archive::resize_async(file_handle handle, std::uint64_t size, std::function<void(op_outcome<void>)> cb)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        ops_pool().execute([this, h = std::move(handle), size, cb = std::move(cb)]()
        {
            auto rx = collect_disappointment([&]()
            {
                return resize(std::move(h), size);
            });
            cb(std::move(rx));
        });
    }

    void archive::size_of_async(file_handle handle, std::function<void(op_outcome<std::uint64_t>)> cb)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        ops_pool().execute([this, h = std::move(handle), cb = std::move(cb)]()
        {
            auto rx = collect_disappointment([&]()
            {
                return size_of(std::move(h));
            });
            cb(std::move(rx));
        });
    }

    void archive::sync_async(file_handle handle, std::function<void(op_outcome<void>)> cb)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        ops_pool().execute([this, h = std::move(handle), cb = std::move(cb)]()
        {
            auto rx = collect_disappointment([&]()
            {
                return sync(std::move(h));
            });
            cb(std::move(rx));
        });
    }
}

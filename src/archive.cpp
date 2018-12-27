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

    auto archive::open(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider * cryptoProvider, blob_view userPRK, file_open_mode_bitset openMode)
        -> result<std::unique_ptr<archive>>
    {
        OUTCOME_TRY(primitives,
            raw_archive::open(fs, archivePath, cryptoProvider, userPRK, openMode));


    }

    archive::archive(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider *cryptoProvider, blob_view userPRK)
        : archive()
    {
        auto archiveFile = fs->open(archivePath, file_open_mode::readwrite);

        mArchive = std::make_unique<detail::raw_archive>(std::move(archiveFile), cryptoProvider, userPRK);

        try
        {
            mFreeBlockIndexFile = free_block_list_file::create(*this,
                mArchive->free_sector_index_file());
        }
        catch (const boost::exception &exc)
        {
            exc << errinfo_archive_file{ "[free-sector-index]" };
            throw;
        }
        try
        {
            mArchiveIndexFile = index_file::open(*this, mArchive->index_file());
        }
        catch (const boost::exception &exc)
        {
            exc << errinfo_archive_file{ "[archive-index]" };
            throw;
        }
    }
    archive::archive(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider * cryptoProvider, blob_view userPRK, create_tag)
        : archive()
    {
        auto archiveFile = fs->open(archivePath, file_open_mode::readwrite | file_open_mode::create | file_open_mode::truncate);

        mArchive = std::make_unique<detail::raw_archive>(archiveFile, cryptoProvider,
            userPRK, raw_archive::create_tag{});

        mFreeBlockIndexFile = free_block_list_file::create(*this,
            mArchive->free_sector_index_file(), create);
        mArchiveIndexFile = index_file::create_new(*this, mArchive->index_file(), create);
    }

    archive::~archive()
    {
        sync();

        mArchiveIndexFile->dispose();
        mArchiveIndexFile = nullptr;
        mFreeBlockIndexFile->dispose();
        mFreeBlockIndexFile = nullptr;
        mWorkTracker.wait();
    }

    void archive::sync()
    {
        try
        {
            if (!mArchiveIndexFile->sync(true))
            {
                // no changes detected
                return;
            }
        }
        catch (const boost::exception &exc)
        {
            exc << errinfo_archive_file{ "[archive-index]" };
        }
        try
        {
            mFreeBlockIndexFile->sync();
        }
        catch (const boost::exception &exc)
        {
            exc << errinfo_archive_file{ "[free-sector-index]" };
        }
        mArchive->update_header();
        mArchive->sync();
    }

    void archive::sync_async(std::function<void(utils::async_error_info)> cb)
    {
        ops_pool().execute([this, cb = std::move(cb)]()
        {
            auto einfo = utils::async_error_context([&]()
            {
                sync();
            });
            cb(std::move(einfo));
        });
    }

    archive::file_handle archive::open(const std::string_view filePath,
        const file_open_mode_bitset mode)
    {
        return mArchiveIndexFile->open(filePath, mode);
    }

    std::optional<file_query_result> archive::query(const std::string_view filePath)
    {
        return mArchiveIndexFile->query(filePath);
    }

    void archive::erase(std::string_view filePath)
    {
        mArchiveIndexFile->erase(filePath);
    }

    void archive::read(file_handle handle, blob buffer, std::uint64_t readFilePos)
    {
        if (!buffer)
        {
            return;
        }
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle doesn't belong this archive" }
            );
        }
        f->read(buffer, readFilePos);
    }

    void archive::write(file_handle handle, blob_view data, std::uint64_t writeFilePos)
    {
        if (!data)
        {
            return;
        }
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle doesn't belong this archive" }
            );
        }

        f->write(data, writeFilePos);
    }

    void archive::resize(file_handle handle, std::uint64_t size)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle doesn't belong this archive" }
            );
        }

        f->resize(size);
    }

    std::uint64_t archive::size_of(file_handle handle)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle doesn't belong this archive" }
            );
        }

        return f->size();
    }

    void archive::sync(file_handle handle)
    {
        if (!handle)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle isn't valid" }
            );
        }
        auto f = file_lookup::deref(handle);
        if (&f->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "handle" }
                << errinfo_param_misuse_description{ "the handle doesn't belong this archive" }
            );
        }

        f->sync();
    }

    void archive::erase_async(std::string filePath, std::function<void(utils::async_error_info)> cb)
    {
        ops_pool().execute([this, filePath = std::move(filePath), cb = std::move(cb)]()
        {
            auto einfo = utils::async_error_context([&]()
            {
                erase(filePath);
            });
            cb(std::move(einfo));
        });
    }

    void archive::read_async(file_handle handle, blob buffer, std::uint64_t readFilePos, std::function<void(utils::async_error_info)> cb)
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
            auto einfo = utils::async_error_context([&]()
            {
                read(std::move(h), buffer, readFilePos);
            });
            cb(std::move(einfo));
        });
    }

    void archive::write_async(file_handle handle, blob_view data, std::uint64_t writeFilePos, std::function<void(utils::async_error_info)> cb)
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
            auto einfo = utils::async_error_context([&]()
            {
                write(std::move(h), data, writeFilePos);
            });
            cb(std::move(einfo));
        });
    }

    void archive::resize_async(file_handle handle, std::uint64_t size, std::function<void(utils::async_error_info)> cb)
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
            auto einfo = utils::async_error_context([&]()
            {
                resize(std::move(h), size);
            });
            cb(std::move(einfo));
        });
    }

    void archive::size_of_async(file_handle handle, std::function<void(std::uint64_t, utils::async_error_info)> cb)
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
            auto size = std::numeric_limits<std::uint64_t>::max();
            auto einfo = utils::async_error_context([&]()
            {
                size = size_of(std::move(h));
            });
            cb(size, std::move(einfo));
        });
    }

    void archive::sync_async(file_handle handle, std::function<void(utils::async_error_info)> cb)
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
            auto einfo = utils::async_error_context([&]()
            {
                sync(std::move(h));
            });
            cb(std::move(einfo));
        });
    }
    /*
    void archive::read_archive_index()
    {
        using alloc_bitset_t = utils::bitset_overlay;

        tree_position it{ 0 };

        adesso::vefs::FileDescriptor descriptor;
        file *indexFile = mArchiveIndexFile.get();

        for (std::uint64_t consumed = 0;
            consumed < indexFile->size();
            consumed += detail::raw_archive::sector_payload_size)
        {
            auto sector = indexFile->access(it);
            it.position(it.position() + 1);

            auto sectorBlob = sector->data();
            auto allocMapBlob = sectorBlob.slice(0, 64);
            sectorBlob.remove_prefix(64);
            sectorBlob.remove_suffix(32); // remove padding

            alloc_bitset_t allocMap{ allocMapBlob };

            constexpr std::size_t block_size = 64;
            constexpr auto blocks_per_sector = 510;
            for (auto i = 0; i < blocks_per_sector; )
            {
                if (!allocMap[i])
                {
                    ++i;
                    sectorBlob.remove_prefix(block_size);
                    continue;
                }

                const auto descriptorLength = sectorBlob.as<std::uint16_t>();
                const auto numBlocks = utils::div_ceil(descriptorLength, block_size);
                if (numBlocks + i >= blocks_per_sector)
                {
                    BOOST_THROW_EXCEPTION(archive_corrupted{}
                        << errinfo_code{ archive_error_code::corrupt_index_entry }
                    );
                }
                for (const auto j = i; i - j < numBlocks; ++i)
                {
                    if (!allocMap[i])
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{}
                            << errinfo_code{ archive_error_code::corrupt_index_entry }
                        );
                    }
                }

                auto currentFile = std::make_unique<detail::basic_archive_file_meta>();
                {
                    if (!parse_blob(descriptor, sectorBlob.slice(2, descriptorLength)))
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{}
                            << errinfo_code{ archive_error_code::corrupt_index_entry }
                        );
                    }
                    VEFS_SCOPE_EXIT{ erase_secrets(descriptor); };

                    unpack(*currentFile, descriptor);
                }

                auto currentId = currentFile->id;
                mIndex.insert_or_assign(descriptor.filepath(), currentId);
                mFileHandles.insert(currentId,
                    utils::make_ref_counted<file_lookup>(*currentFile));

                sectorBlob.remove_prefix(
                    utils::div_ceil(sizeof(std::uint16_t) + descriptorLength, block_size));
            }
        }
    }

    void archive::write_archive_index()
    {
        using alloc_bitset_t = utils::bitset_overlay;

        auto lockedindex = mIndex.lock_table();

        adesso::vefs::FileDescriptor descriptor;
        file *indexFile = mArchiveIndexFile.get();

        auto dataMem = std::make_unique<std::array<std::byte, raw_archive::sector_payload_size>>();
        blob data{ *dataMem };
        utils::secure_memzero(data);
        alloc_bitset_t allocMap{ data.slice(0, 64) };
        data.remove_prefix(64);

        auto i = 0;
        constexpr auto block_size = 64;
        constexpr auto blocks_per_sector = 510;
        std::uint64_t fileOffset = 0;
        bool dirty = true;

        for (auto it = lockedindex.begin(), end = lockedindex.end(); it != end;)
        {
            detail::basic_archive_file_meta *file;
            mFileHandles.find_fn(std::get<1>(*it), [&file](const file_lookup_ptr &fl)
            {
                file = &fl->meta_data();
            });
            pack(descriptor, *file);
            descriptor.set_filepath(std::get<0>(*it));

            auto size = descriptor.ByteSizeLong();
            auto neededBlocks = utils::div_ceil(size + 2, block_size);

            if (neededBlocks <= blocks_per_sector - i)
            {
                for (auto limit = i + neededBlocks; i < limit; ++i)
                {
                    allocMap.set(i);
                }

                data.pop_front_as<std::uint16_t>() = static_cast<std::uint16_t>(size);

                serialize_to_blob(data, descriptor);
                data.remove_prefix((neededBlocks * block_size) - 2);

                ++it;
                dirty = true;
            }
            else
            {
                data = nullptr;
            }

            if (!data)
            {
                data = blob{ *dataMem };

                indexFile->write(data, fileOffset);
                fileOffset += raw_archive::sector_payload_size;
                dirty = false;
                i = 0;

                utils::secure_memzero(data);
                data.remove_prefix(64);
            }
        }

        if (dirty)
        {
            data = blob{ *dataMem };

            indexFile->write(data, fileOffset);
            fileOffset += raw_archive::sector_payload_size;
        }

        indexFile->resize(fileOffset);
    }
    */
}

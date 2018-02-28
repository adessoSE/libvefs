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

#include <vefs/utils/misc.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/detail/tree_walker.hpp>

#include "archive_file.hpp"
#include "proto-helper.hpp"

using namespace std::string_view_literals;

using namespace vefs::detail;

namespace vefs
{
    namespace
    {
#pragma pack(push, 1)

        struct RawFreeSectorRange
        {
            detail::sector_id start_sector;
            std::uint64_t num_sectors;
        };
        static_assert(sizeof(RawFreeSectorRange) == 16);

#pragma pack(pop)
    }

    archive::archive()
        : mFreeSectorPoolMutex{}
    {
    }

    archive::archive(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider *cryptoProvider, blob_view userPRK)
        : archive()
    {
        auto archiveFile = fs->open(archivePath, file_open_mode::readwrite);

        mArchive = std::make_unique<detail::raw_archive>(std::move(archiveFile), cryptoProvider, userPRK);

        mArchiveIndexFile = std::make_shared<archive::file>(*this, mArchive->index_file());
        mFreeBlockIndexFile = std::make_shared<archive::file>(*this, mArchive->free_sector_index_file());
        read_archive_index();
        read_free_sector_index();
    }
    archive::archive(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider * cryptoProvider, blob_view userPRK, create_tag)
        : archive()
    {
        auto archiveFile = fs->open(archivePath, file_open_mode::readwrite | file_open_mode::create | file_open_mode::truncate);

        mArchive = std::make_unique<detail::raw_archive>(archiveFile, cryptoProvider, userPRK, create);

        mArchiveIndexFile = std::make_shared<archive::file>(
            *this, mArchive->index_file(), create
        );
        mFreeBlockIndexFile = std::make_shared<archive::file>(
            *this, mArchive->free_sector_index_file(), create
        );
    }

    archive::~archive()
    {
        sync();
        mOpsPool.reset();
    }

    void archive::sync()
    {
        for (auto &f : mFileHandles.lock_table())
        {
            if (auto &fh = f.second.handle)
            {
                fh->sync();
            }
        }

        write_archive_index();
        mArchiveIndexFile->sync();
        write_free_sector_index();
        mFreeBlockIndexFile->sync();

        mArchive->update_header();
        mArchive->sync();
    }

    archive::file_handle archive::open(std::string_view filePath,
        file_open_mode_bitset mode)
    {
        file_id id;
        file_handle result;

        auto acquire_fn = [this, &result](file_lookup &f)
        {
            result = f.to_handle(*this);
        };

        if (mIndex.find_fn(filePath, [&id](const detail::file_id &elem) { id = elem; }))
        {
            if (mFileHandles.update_fn(id, acquire_fn))
            {
                return result;
            }
        }
        if (mode % file_open_mode::create)
        {
            auto file = mArchive->create_file();
            id = file->id;

            if (!mFileHandles.insert(id, file_lookup{ std::move(file) }))
            {
                BOOST_THROW_EXCEPTION(logic_error{});
            }

            mIndex.upsert(filePath, [&](file_id &rid)
            {
                // rollback, someone was faster
                mFileHandles.erase(id);
                id = rid;
            }, id);

            mFileHandles.update_fn(id, [this, &result](file_lookup &f)
            {
                f.handle = result = std::make_shared<archive::file>(*this, *f.persistent, create);
            });

            return result;
        }

        // #TODO refine open failure exception
        BOOST_THROW_EXCEPTION(exception{});
    }

    void archive::close(file_handle &handle)
    {
        if (!handle || &handle->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        handle->sync();
        auto id = handle->mData.id;
        handle = nullptr;
        mFileHandles.update_fn(id, [](file_lookup &l)
        {
            file_handle::weak_type rst{ l.handle };
            l.handle = nullptr;
            l.handle = rst.lock();
        });
    }

    void archive::erase(std::string_view filePath)
    {
        file_id fid;
        if (!mIndex.erase_fn(filePath, [&fid](const detail::file_id &elem) { fid = elem; return true; }))
        {
            BOOST_THROW_EXCEPTION(std::out_of_range{"filePath wasn't found in the archive index"});
        }

        file_handle handle;
        mFileHandles.update_fn(fid, [this, &handle](file_lookup &l)
        {
            handle = l.to_handle(*this);
            l.persistent->valid = false;
            l.handle = nullptr;
        });
    }

    void archive::read(file_handle file, blob buffer, std::uint64_t readFilePos)
    {
        if (!buffer)
        {
            return;
        }
        if (!file || &file->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        file->read(buffer, readFilePos);
    }

    void archive::write(file_handle file, blob_view data, std::uint64_t writeFilePos)
    {
        if (!data)
        {
            return;
        }
        if (!file || &file->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        file->write(data, writeFilePos);
    }

    void archive::resize(file_handle file, std::uint64_t size)
    {
        if (!file || &file->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        file->resize(size);
    }

    std::uint64_t archive::size_of(file_handle file)
    {
        if (!file || &file->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        return file->size();
    }

    void archive::sync(file_handle file)
    {
        if (!file || &file->owner_ref() != this)
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        file->sync();
    }

    void archive::read_archive_index()
    {
        using alloc_bitset_t = utils::bitset_overlay;

        mArchiveIndexFile;

        tree_position it{ 0 };

        adesso::vefs::FileDescriptor descriptor;

        for (std::uint64_t consumed = 0; consumed < mArchiveIndexFile->size();
            consumed += detail::raw_archive::sector_payload_size)
        {
            auto sector = mArchiveIndexFile->access(it);
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
                    BOOST_THROW_EXCEPTION(archive_corrupted{});
                }
                for (const auto j = i; i - j < numBlocks; ++i)
                {
                    if (!allocMap[i])
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{});
                    }
                }

                auto currentFile = std::make_unique<detail::basic_archive_file_meta>();
                {
                    if (!parse_blob(descriptor, sectorBlob.slice(2, descriptorLength)))
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{});
                    }
                    VEFS_SCOPE_EXIT{ erase_secrets(descriptor); };

                    unpack(*currentFile, descriptor);
                }

                auto currentId = currentFile->id;
                mIndex.insert_or_assign(descriptor.filepath(), currentId);
                mFileHandles.insert(currentId, file_lookup{ std::move(currentFile) });

                sectorBlob.remove_prefix(utils::div_ceil(sizeof(std::uint16_t) + descriptorLength, block_size));
            }
        }
    }

    void archive::write_archive_index()
    {
        using alloc_bitset_t = utils::bitset_overlay;

        auto lockedindex = mIndex.lock_table();

        adesso::vefs::FileDescriptor descriptor;

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
            mFileHandles.find_fn(std::get<1>(*it), [&file](const file_lookup &fl)
            {
                file = fl.persistent.get();
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

                mArchiveIndexFile->write(data, fileOffset);
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

            mArchiveIndexFile->write(data, fileOffset);
            fileOffset += raw_archive::sector_payload_size;
        }

        mArchiveIndexFile->resize(fileOffset);
    }

    void archive::read_free_sector_index()
    {
        const auto &free_sector_index = mArchive->free_sector_index_file();
        if (free_sector_index.size % sizeof(RawFreeSectorRange) != 0)
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        std::lock_guard<std::mutex> freeSectorLock{ mFreeSectorPoolMutex };
        tree_position it{ 0 };

        for (std::uint64_t consumed = 0; consumed < free_sector_index.size; )
        {
            auto sector = mFreeBlockIndexFile->access(it);
            it.position(it.position() + 1);

            auto sectorBlob = sector->data_view();
            if (free_sector_index.size - consumed < detail::raw_archive::sector_payload_size)
            {
                sectorBlob.slice(0, static_cast<std::size_t>(free_sector_index.size - consumed));
            }
            consumed += sectorBlob.size();

            while (sectorBlob)
            {
                auto &freeSectorRange = sectorBlob.pop_front_as<RawFreeSectorRange>();
                if (freeSectorRange.start_sector == sector_id::master)
                {
                    continue;
                }

                auto sectorId = static_cast<std::uint64_t>(freeSectorRange.start_sector);

                auto offset = freeSectorRange.num_sectors - 1;
                auto lastSector = detail::sector_id{ sectorId + offset };

                mFreeSectorPool.emplace_hint(mFreeSectorPool.cend(), lastSector, offset);
            }
        }
    }

    void archive::write_free_sector_index()
    {
        RawFreeSectorRange entry = {};
        const blob_view entryView = as_blob_view(entry);
        std::uint64_t writePos = 0;

        std::unique_lock<std::mutex> freeSectorLock{ mFreeSectorPoolMutex };

        // #TODO there must be a better way to reliably flush the free block index

        do
        {
            while (mFreeBlockIndexFile->size() / sizeof(RawFreeSectorRange) > mFreeSectorPool.size() + 2)
            {
                freeSectorLock.unlock();
                {
                    std::unique_lock<std::shared_mutex> shrinkLock{
                        mFreeBlockIndexFile->mData.shrink_mutex
                    };
                    mFreeBlockIndexFile->shrink_file(
                        (mFreeSectorPool.size() + 2) * sizeof(RawFreeSectorRange)
                    );
                }
                freeSectorLock.lock();
            }

            if (mFreeBlockIndexFile->size() / sizeof(RawFreeSectorRange) < mFreeSectorPool.size())
            {
                freeSectorLock.unlock();
                {
                    std::shared_lock<std::shared_mutex> shrinkLock{
                        mFreeBlockIndexFile->mData.shrink_mutex
                    };
                    mFreeBlockIndexFile->grow_file(
                        mFreeSectorPool.size() * sizeof(RawFreeSectorRange)
                    );
                }
                freeSectorLock.lock();
            }
            else
            {
                break;
            }
        } while (true);

        for (auto [lastId, numPrev] : mFreeSectorPool)
        {
            entry.start_sector = sector_id{ static_cast<uint64_t>(lastId) - numPrev };
            entry.num_sectors = numPrev + 1;

            mFreeBlockIndexFile->write(entryView, writePos);
            writePos += entryView.size();
        }

        entry = {};
        while (writePos < mFreeBlockIndexFile->size())
        {
            mFreeBlockIndexFile->write(entryView, writePos);
            writePos += entryView.size();
        }
    }

    std::map<detail::sector_id, std::uint64_t>::iterator archive::grow_archive_impl(unsigned int num)
    {
        if (!num)
        {
            return {};
        }
        // assuming mFreeSectorPoolMutex is held
        num -= 1;
        auto newLastSector = sector_id{ mArchive->size() + num };
        mArchive->resize(static_cast<std::uint64_t>(newLastSector) + 1);

        return mFreeSectorPool.emplace_hint(mFreeSectorPool.cend(), newLastSector, num);
    }

    std::vector<detail::sector_id> archive::alloc_sectors(unsigned int num)
    {
        std::vector<detail::sector_id> allocated;
        allocated.reserve(num);

        std::lock_guard<std::mutex> allocLock{ mFreeSectorPoolMutex };

        VEFS_ERROR_EXIT{
            try
            {
                dealloc_sectors_impl(std::move(allocated));
            }
            catch (...)
            {
            }
        };

        auto freeSectorIt = mFreeSectorPool.begin();
        auto freeSectorRangeEnd = mFreeSectorPool.end();
        while (num)
        {
            if (freeSectorIt == freeSectorRangeEnd)
            {
                freeSectorIt = grow_archive_impl(std::min(4u, num));
            }
            auto[lastIdx, offset] = *freeSectorIt;

            auto i = 0ull;
            for (; num && i <= offset; ++i)
            {
                allocated.push_back(sector_id{ static_cast<std::uint64_t>(lastIdx) - offset + i });
                --num;
            }

            if (i > offset)
            {
                mFreeSectorPool.erase(freeSectorIt++);
            }
            else
            {
                offset -= i;
            }
        }

        return allocated;
    }

    void archive::dealloc_sectors(std::vector<detail::sector_id> sectors)
    {
        if (sectors.empty())
        {
            return;
        }

        std::sort(sectors.begin(), sectors.end());
        std::unique(sectors.begin(), sectors.end());

        if (sectors.front() == sector_id::master)
        {
            sectors.erase(sectors.cbegin());
            if (sectors.empty())
            {
                return;
            }
        }

        std::lock_guard<std::mutex> allocLock{ mFreeSectorPoolMutex };

        dealloc_sectors_impl(std::move(sectors));
    }
    void archive::dealloc_sectors_impl(std::vector<detail::sector_id> sectors)
    {
        auto current = sectors.front();
        auto offset = 0ull;
        for (auto it = ++sectors.cbegin(), end = sectors.cend(); it != end; ++it)
        {
            const auto next = *it;
            if (static_cast<std::uint64_t>(next) - static_cast<std::uint64_t>(current) == 1)
            {
                ++offset;
            }
            else
            {
                mFreeSectorPool.emplace(current, offset);
                offset = 0;
            }
            current = next;
        }
        mFreeSectorPool.emplace(current, offset);
    }
}

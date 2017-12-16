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

#include "proto-helper.hpp"

using namespace std::string_view_literals;

using namespace vefs::detail;

namespace vefs
{
    namespace
    {
        constexpr auto index_id = "[index]"sv;
        constexpr auto free_blocks_id = "[free-blocks]"sv;


#pragma pack(push, 1)

        struct RawSectorReference
        {
            detail::sector_id reference;
            std::array<std::byte, 8> VEFS_ANONYMOUS_VAR(_padding);
            std::array<std::byte, 16> mac;
        };
        static_assert(sizeof(RawSectorReference) == 32);

        struct RawFreeSectorRange
        {
            detail::sector_id start_sector;
            std::uint64_t num_sectors;
        };
        static_assert(sizeof(RawFreeSectorRange) == 16);

#pragma pack(pop)

        static_assert(file_sector_id::references_per_sector == raw_archive::sector_payload_size / sizeof(RawSectorReference));

    }

    inline std::optional<archive::file_sector_handle> archive::access_impl(const raw_archive_file &file,
        const file_sector_id &cacheId)
    {
        if (auto sector = mBlockPool->try_access(cacheId))
        {
            return sector;
        }

        constexpr auto references_per_sector = raw_archive::sector_payload_size / sizeof(RawSectorReference);

        std::uint64_t fileSize;
        std::uint8_t treeDepth;
        sector_id physId;
        decltype(file.start_block_mac) mac;
        {
            std::shared_lock<std::shared_mutex> fileLock{ file.integrity_mutex };
            treeDepth = static_cast<std::uint8_t>(file.tree_depth);
            physId = file.start_block_idx;
            mac = file.start_block_mac;
            fileSize = file.size;
        }
        if (treeDepth < cacheId.layer() || !cacheId.is_allocated(fileSize))
        {
            return std::make_optional<file_sector_handle>();
        }

        tree_path path{ treeDepth, cacheId.position(), cacheId.layer() };
        file_sector_id logId{ file.id, path.layer_position(treeDepth) };
        std::shared_ptr<file_sector> parentSector;


        for (;;)
        {
            std::shared_ptr<file_sector> sector;
            try
            {
                if (static_cast<uint64_t>(physId) >= mArchive->size())
                {
                    BOOST_THROW_EXCEPTION(sector_reference_out_of_range{}
                        << errinfo_sector_idx{ physId });
                }
                auto entry = mBlockPool->access(logId, *mArchive, file, logId, physId, blob_view{ mac });
                sector = std::move(std::get<1>(entry));
            }
            catch (const boost::exception &)
            {
                std::shared_lock<std::shared_mutex> fileLock{ file.integrity_mutex };
                // if the file tree shrinks during an access operation it may happen
                // that one of the intermediate nodes dies :(
                // however this is detectable and recoverable if the cut off part
                // doesn't contain the block we want to access
                auto nTreeDepth = static_cast<std::uint8_t>(file.tree_depth);
                if (nTreeDepth < cacheId.layer())
                {
                    return std::make_optional<file_sector_handle>();
                }
                if (treeDepth > nTreeDepth)
                {
                    return std::nullopt;
                }

                // it can happen that the target sector is written to disk and and removed from the
                // cache after we obtained its mac in the last iteration, if we detect such case
                // we just reread the mac and (the maybe updated sector idx) and try again with them
                if (parentSector)
                {
                    fileLock.unlock();
                    // stabelize memory representation
                    std::lock_guard<std::mutex> parentLock{ parentSector->write_mutex() };

                    const auto ref = &sector->data_view().as<RawSectorReference>()
                        + path.position(logId.layer());
                    if (ref->reference != sector_id::master)
                    {
                        return std::make_optional<file_sector_handle>();
                    }
                    if (!equal(blob_view{ mac }, blob_view{ ref->mac }))
                    {
                        physId = ref->reference;
                        mac = ref->mac;
                        continue;
                    }
                }
                else if (!equal(blob_view{ mac }, blob_view{ file.start_block_mac }))
                {
                    return std::nullopt;
                }
                //TODO: add additional information like failed file_sector_id
                throw;
            }

            if (logId.layer() == cacheId.layer())
            {
                return sector;
            }

            logId.layer(logId.layer() - 1);
            logId.position(path.position(logId.layer()));

            const auto ref = &sector->data_view().as<RawSectorReference>()
                + path.offset(logId.layer());

            physId = ref->reference;
            mac = ref->mac;
            // this will happen if we read past the end of the file
            if (physId == sector_id::master)
            {
                return std::make_optional<file_sector_handle>();
            }

            parentSector = std::move(sector);
        }
    }

    std::shared_ptr<file_sector> archive::safe_acquire(const raw_archive_file & file, const file_sector_id & logicalId, sector_id physId, blob_view mac)
    {
        auto result = mBlockPool->try_access<3>(logicalId, *mArchive, file, logicalId, physId, mac);
        if (auto sector = std::move(std::get<1>(result)))
        {
            return sector;
        }

        //TODO: bad alloc
        auto raw = new file_sector(*mArchive, file, logicalId, physId, mac);
        return std::get<1>(mBlockPool->try_push_external(logicalId, *raw, [](auto &, file_sector *val)
        {
            delete val;
        }));
    }

    std::shared_ptr<file_sector> archive::access(const detail::raw_archive_file &file,
        detail::file_sector_id sector)
    {
        if (!sector)
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }
        std::optional<file_sector_handle> result;
        do
        {
            try
            {
                result = access_impl(file, sector);
            }
            catch (const boost::exception &)
            {
                //TODO: add additional information like requested file_sector_id
                throw;
            }
        } while (!result);
        return *result;
    }

    class archive::file_walker
        : public boost::iterator_facade<archive::file_walker,
            std::shared_ptr<file_sector>, // value_type
            boost::random_access_traversal_tag,   // category
            std::shared_ptr<file_sector>  // reference
        >
    {
        friend class boost::iterator_core_access;

    public:
        file_walker()
            : mArchive(nullptr)
            , mFile(nullptr)
            , mPosition()
        {
        }
        file_walker(archive &owner, const detail::raw_archive_file &file, std::uint64_t initialSectorOffset = {})
            : mArchive(&owner)
            , mFile(&file)
            , mPosition{ file.id, { initialSectorOffset, 0 } }
        {
        }

        const file_sector_id & current() const
        {
            return mPosition;
        }

    private:
        std::shared_ptr<file_sector> dereference() const
        {
            return mArchive->access(*mFile, mPosition);
        }

        bool equal(const file_walker &other)
        {
            return mArchive == other.mArchive
                && mPosition == other.mPosition;

        }

        void increment()
        {
            advance(+1);
        }

        void decrement()
        {
            advance(-1);
        }

        void advance(difference_type n)
        {
            mPosition.position(mPosition.position() + n);
        }

        difference_type distance_to(const file_walker &other) const
        {
            return static_cast<difference_type>(other.mPosition.position() - mPosition.position());
        }


        mutable archive *mArchive;
        const detail::raw_archive_file *mFile;
        file_sector_id mPosition;
    };

    class archive::writer_task
    {
    public:
        using sector_vector = std::vector<std::shared_ptr<file_sector>>;
        using sector_stack = std::stack<std::shared_ptr<file_sector>, sector_vector>;

        writer_task(archive &owner, std::shared_ptr<file_sector> sector)
            : mOwner(owner)
            , mSector(std::move(sector))
        {
        }

        void operator()()
        {
            if (!mSector)
            {
                return;
            }

            raw_archive_file_ptr file;
            try
            {
                file = mOwner.get_file_handle(mSector->id());
            }
            catch (const std::out_of_range &)
            {
                // file is dead, so we let go of this sector
                if (mSector->dirty_flag().exchange(false, std::memory_order_release))
                {
                    --mOwner.mDirtyObjects;
                }
                return;
            }

            std::unique_lock<std::mutex> sectorLock{ mSector->write_mutex(), std::defer_lock };
            std::shared_lock<std::shared_mutex> shrinkLock{ file->shrink_mutex, std::defer_lock };
            std::lock(shrinkLock, sectorLock);
            VEFS_ERROR_EXIT{ mOwner.mark_dirty(mSector); };
            VEFS_SCOPE_EXIT{ mSector->write_queued_flag().clear(std::memory_order_release); };

            if (!mSector->dirty_flag())
            {
                return;
            }

            if (!file->valid)
            {
                mSector->dirty_flag().store(false, std::memory_order_release);
                return;
            }

            std::array<std::byte, raw_archive::sector_payload_size + 16> encryptionMem;
            blob ciphertext{ encryptionMem };
            blob mac = ciphertext.slice(0, 16);
            ciphertext.remove_prefix(16);

            mOwner.mArchive->write_sector(ciphertext, mac, *file, mSector->sector(), mSector->data_view());

            auto sectorChain = mSector;
            while (!try_finish_update(*file, sectorChain->id(), mac))
            {
                sectorChain = update_parents(*file, std::move(sectorChain), ciphertext, mac);
            }

            mSector->dirty_flag().store(false, std::memory_order_release);
            --mOwner.mDirtyObjects;
        }

    private:
        bool try_finish_update(raw_archive_file &file, file_sector_id logicalId, blob_view mac)
        {
            std::lock_guard<std::shared_mutex> fileIntegrityLock{ file.integrity_mutex };

            if (logicalId.layer() != file.tree_depth)
            {
                return false;
            }
            mac.copy_to(file.start_block_mac_blob());
            return true;
        }

        std::shared_ptr<file_sector> update_parents(raw_archive_file &file, std::shared_ptr<file_sector> lastUpdated,
            blob ciphertext, blob mac)
        {
            auto logicalId = lastUpdated->id();
            auto parents = acquire_parents(file, logicalId);
            while (!parents.empty())
            {
                auto offset = logicalId.position_array_offset();
                logicalId = logicalId.parent();
                {
                    auto &parent = parents.top();
                    std::unique_lock<std::mutex> parentLock{ parent->write_mutex() };

                    auto ref = &parent->data().as<RawSectorReference>() + offset;
                    ref->reference = lastUpdated->sector();
                    mac.copy_to(blob{ ref->mac });

                    mOwner.mArchive->write_sector(ciphertext, mac, file, parent->sector(), parent->data_view());

                    if (parent->dirty_flag().exchange(false))
                    {
                        --mOwner.mDirtyObjects;
                    }
                }
                lastUpdated = std::move(parents.top());
                parents.pop();
            }
            return lastUpdated;
        }

        sector_stack acquire_parents(const raw_archive_file &file, const file_sector_id &leaf)
        {
            file_sector_id logicalId{ leaf.file_id(), {} };
            std::array<std::byte, 16> macMem;
            sector_id physId;
            blob mac{ macMem };
            std::shared_ptr<file_sector> currentSector;
            {
                std::shared_lock<std::shared_mutex> integrityLock{ file.integrity_mutex };
                macMem = file.start_block_mac;
                physId = file.start_block_idx;

                logicalId.layer_position({ 0, file.tree_depth });
                if (file.tree_depth == 0)
                {
                    return {};
                }
                currentSector = acquire(file, logicalId, physId, mac);
            }

            sector_vector stackStorage;
            stackStorage.reserve(logicalId.layer());
            sector_stack stack{ std::move(stackStorage) };
            stack.push(std::move(currentSector));

            //tree_path wishlist{ logicalId.layer(), leaf.layer_position().parent() };

            std::stack<tree_position> wishlist;
            {
                auto parentId = leaf.parent();
                for (std::uint8_t i = 1, limit = logicalId.layer(); i < limit; ++i)
                {
                    wishlist.push(parentId.layer_position());
                    parentId = parentId.parent();
                }
                assert(parentId.layer_position() == logicalId.layer_position());
            }

            while (!wishlist.empty())
            {
                logicalId.layer_position(wishlist.top());
                wishlist.pop();

                auto &sector = stack.top();
                std::lock_guard<std::mutex> sectorLock{ sector->write_mutex() };

                const auto ref = &sector->data_view().as<RawSectorReference>() + logicalId.position_array_offset();
                physId = ref->reference;
                macMem = ref->mac;

                stack.push(acquire(file, logicalId, physId, mac));
            }
            return stack;

        }
        std::shared_ptr<file_sector> acquire(const raw_archive_file &file,
            const file_sector_id &logicalId, sector_id physId, blob_view mac)
        {
            auto result = mOwner.mBlockPool->try_access<3>(logicalId, *mOwner.mArchive, file, logicalId, physId, mac);
            if (auto sector = std::move(std::get<1>(result)))
            {
                return sector;
            }

            //TODO: bad alloc
            auto raw = new file_sector(*mOwner.mArchive, file, logicalId, physId, mac);
            return std::get<1>(mOwner.mBlockPool->try_push_external(logicalId, *raw, [](auto &, file_sector *val)
            {
                if (val)
                {
                    delete val;
                }
            }));
        }

        archive &mOwner;
        const std::shared_ptr<file_sector> mSector;
    };

    archive::archive()
        : mFreeSectorPoolMutex()
        , mDirtyObjects(0)
    {
        mBlockPool = std::make_unique<block_pool_t>([this](const file_sector_id &id, block_pool_t::handle sector)
        {
            if (sector->dirty_flag().load(std::memory_order_acquire))
            {
                if (id.layer() == 0)
                {
                    // we only queue data sectors for writing
                    if (!sector->write_queued_flag().test_and_set(std::memory_order_acq_rel))
                    {
                        // we only queue each sector round about once (plus races)
                        mOpsPool->exec(writer_task{ *this, std::move(sector) });
                    }
                    // data sectors are locked by their writer tasks and thus don't need to
                    // pollute the access queue
                    return false;
                }
                return true;
            }
            return false;
        });
    }

    archive::archive(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider *cryptoProvider, blob_view userPRK)
        : archive()
    {
        file::ptr archiveFile = fs->open(archivePath, file_open_mode::readwrite);

        mArchive = std::make_unique<detail::raw_archive>(std::move(archiveFile), cryptoProvider, userPRK);
        read_archive_index();
        read_free_sector_index();
    }
    archive::archive(filesystem::ptr fs, std::string_view archivePath,
        crypto::crypto_provider * cryptoProvider, blob_view userPRK, create_tag)
        : archive()
    {
        file::ptr archiveFile = fs->open(archivePath, file_open_mode::readwrite | file_open_mode::create | file_open_mode::truncate);

        mArchive = std::make_unique<detail::raw_archive>(archiveFile, cryptoProvider, userPRK, create);
        access_or_append(mArchive->index_file(), file_sector_id{ file_id::archive_index, { 0, 0 } });
        access_or_append(mArchive->free_sector_index_file(), file_sector_id{ file_id::free_block_index, { 0, 0 } });
    }

    archive::~archive()
    {
        sync();
        mOpsPool.reset();
    }

    void vefs::archive::sync()
    {
        using namespace std::chrono_literals;

        while (mDirtyObjects)
        {
            mBlockPool->queue_dirty();
            std::this_thread::sleep_for(500us);
        }

        write_archive_index();
        write_free_sector_index();

        while (mDirtyObjects)
        {
            mBlockPool->queue_dirty();
            std::this_thread::sleep_for(500us);
        }

        mArchive->update_header();
        mArchive->sync();
    }

    file_id vefs::archive::open(std::string_view filePath, file_open_mode_bitset mode)
    {
        file_id result;
        if (mIndex.find_fn(filePath, [&result](const detail::file_id &elem) { result = elem; }))
        {
            return result;
        }
        else if (mode % file_open_mode::create)
        {
            auto file = mArchive->create_file();

            if (!mFiles.insert(file->id, file))
            {
                BOOST_THROW_EXCEPTION(logic_error{});
            }

            access_or_append(*file, file_sector_id{ file->id, { 0, 0 } });
            mIndex.insert_or_assign(std::string{ filePath }, file->id);

            return file->id;
        }
        else
        {
            BOOST_THROW_EXCEPTION(exception{});
        }
    }

    void vefs::archive::erase(std::string_view filePath)
    {
        file_id fid;
        if (!mIndex.erase_fn(filePath, [&fid](const detail::file_id &elem) { fid = elem; return true; }))
        {
            BOOST_THROW_EXCEPTION(std::out_of_range{"filePath wasn't found in the archive index"});
        }

        raw_archive_file_ptr file = mFiles.find(fid);
        std::unique_lock<std::shared_mutex> shrinkLock{ file->shrink_mutex };
        shrink_file(*file, 0);

        std::unique_lock<std::shared_mutex> integrityLock{ file->integrity_mutex };
        file->valid = false;
        mFiles.erase(fid);

        mArchive->erase_sector(*file, file->start_block_idx);
        dealloc_sectors({ file->start_block_idx });
    }

    void vefs::archive::read(detail::file_id id, blob buffer, std::uint64_t readFilePos)
    {
        if (!buffer)
        {
            return;
        }
        raw_archive_file_ptr file;
        if (!mFiles.find_fn(id, [&file](const raw_archive_file_ptr &elem) { file = elem; }))
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        auto offset = readFilePos % raw_archive::sector_payload_size;
        file_walker walker{ *this, *file, readFilePos / raw_archive::sector_payload_size };
        while (buffer)
        {
            const auto sector = *walker++;
            auto chunk = sector->data_view().slice(offset);
            offset = 0;
            chunk.copy_to(buffer);
            buffer.remove_prefix(chunk.size());
        }
    }

    void vefs::archive::grow_file(detail::raw_archive_file & file, std::uint64_t size)
    {
        auto endSectorPos = size ? (size - 1) / raw_archive::sector_payload_size : 0;
        std::uint64_t fileSize;
        {
            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            fileSize = file.size;
        }
        auto startSectorPos = fileSize ? (fileSize - 1) / raw_archive::sector_payload_size : 0;

        file_walker walker{ *this, file, startSectorPos };

        // the first sector is always allocated
        walker += 1;
        while (walker.current().position() <= endSectorPos)
        {
            if (!walker.current().is_allocated(fileSize))
            {
                access_or_append(file, walker.current());

                std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
                auto newSize = std::min((walker.current().position() + 1) * raw_archive::sector_payload_size, size);
                fileSize = file.size = std::max(file.size, newSize);
            }
            walker += 1;
        }

        {
            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            if (size != file.size)
            {
                file.size = size;
            }
        }
    }

    void vefs::archive::shrink_file(detail::raw_archive_file & file, const std::uint64_t size)
    {
        std::uint64_t fileSize;
        std::uint8_t treeDepth;
        sector_id nextSector;
        std::array<std::byte, 16> nextMac;
        {
            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            fileSize = file.size;
            treeDepth = static_cast<std::uint8_t>(file.tree_depth);
            nextSector = file.start_block_idx;
            nextMac = file.start_block_mac;
        }
        // we always keep the first sector alive
        if (fileSize <= raw_archive::sector_payload_size)
        {
            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            file.size = size;
            return;
        }
        auto endSectorId = size ? (size - 1) / raw_archive::sector_payload_size : 0;

        std::vector<detail::sector_id> collectedIds;
        file_walker walker{ *this, file, (fileSize-1) / raw_archive::sector_payload_size };
        while (walker.current().position() > endSectorId)
        {
            auto cutOff = false;
            auto newSize = std::max(size, walker.current().position() * raw_archive::sector_payload_size);

            file_sector_id logId{ file.id, { 0, treeDepth } };

            std::shared_ptr<file_sector> it = safe_acquire(file, logId, nextSector, blob_view{ nextMac });

            auto width = utils::upow(file_sector_id::references_per_sector, treeDepth - 1);
            logId.layer(treeDepth - 1);
            logId.position(walker.current().position() / width);


            if (walker.current().position() == width)
            {
                std::unique_lock<std::mutex> writeLock{ it->write_mutex() };

                cutOff = true;

                collectedIds.push_back(it->sector());
                mArchive->erase_sector(file, it->sector());
                if (it->dirty_flag().exchange(false))
                {
                    --mDirtyObjects;
                }

                auto ref = &it->data().as<RawSectorReference>();

                std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };

                file.start_block_idx = ref->reference;
                file.start_block_mac = ref->mac;
                treeDepth = static_cast<std::uint8_t>(--file.tree_depth);
                utils::secure_data_erase(*ref++);

                nextSector = ref->reference;
                nextMac = ref->mac;
                utils::secure_data_erase(*ref);
            }
            else
            {
                auto ref = &it->data().as<RawSectorReference>() + logId.position();

                nextSector = ref->reference;
                nextMac = ref->mac;

            }

            auto offset = logId.position_array_offset();
            while (logId.layer() > 0)
            {
                if (cutOff || walker.current().position() == logId.position() * width)
                {

                    std::unique_lock<std::mutex> writeLock{ it->write_mutex() };

                    if (!cutOff)
                    {
                        auto ref = &it->data().as<RawSectorReference>() + offset;
                        utils::secure_data_erase(*ref);
                        mark_dirty(it);
                    }
                    else if (it->dirty_flag().exchange(false))
                    {
                        --mDirtyObjects;
                    }
                    cutOff = true;

                }

                it = safe_acquire(file, logId, nextSector, blob_view{ nextMac });

                width /= file_sector_id::references_per_sector;
                logId.layer(logId.layer() - 1);
                logId.position(walker.current().position() / width);

                offset = logId.position_array_offset();
                auto ref = &it->data().as<RawSectorReference>() + offset;

                nextSector = ref->reference;
                nextMac = ref->mac;

                if (cutOff)
                {
                    std::unique_lock<std::mutex> writeLock{ it->write_mutex() };

                    utils::secure_data_erase(*ref);
                    if (it->dirty_flag().exchange(false))
                    {
                        --mDirtyObjects;
                    }
                    collectedIds.push_back(it->sector());
                    mArchive->erase_sector(file, it->sector());
                }

            }

            if (!cutOff)
            {
                auto ref = &it->data().as<RawSectorReference>() + offset;
                utils::secure_data_erase(*ref);
                mark_dirty(it);
            }
            // acquire the data sector
            it = safe_acquire(file, logId, nextSector, blob_view{ nextMac });
            collectedIds.push_back(it->sector());
            mArchive->erase_sector(file, it->sector());
            if (it->dirty_flag().exchange(false))
            {
                --mDirtyObjects;
            }

            /*
            auto current = *walker--;
            std::lock_guard<std::mutex> sectorLock{ current->write_mutex() };

            {
                auto tbcSecId = current->sector();
                auto it = walker.current();
                auto parentId = it.parent();
                do
                {
                    // process all parents which will be deallocated
                    auto parent = access(file, parentId);
                    std::lock_guard<std::mutex> sectorLock{ parent->write_mutex() };

                    collectedIds.push_back(tbcSecId);
                    tbcSecId = parent->sector();

                    auto itOffset = it.position_array_offset();
                    utils::secure_data_erase(*(&parent->data().as<RawSectorReference>() + itOffset));

                    if (parent->dirty_flag().exchange(false, std::memory_order_acq_rel))
                    {
                        --mDirtyObjects;
                    }

                    if (parentId.layer() == file.tree_depth && !parentId.is_allocated(newSize))
                    {
                        assert(itOffset == 1);
                        // the current root must be erased
                        std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
                        file.tree_depth -= 1;

                        auto ref = parent->data().as<RawSectorReference>();
                        file.start_block_idx = ref.reference;
                        file.start_block_mac = ref.mac;
                        utils::secure_data_erase(ref);
                        break;
                    }

                    it = parentId;
                    parentId = parentId.parent();
                } while (!it.is_allocated(newSize));
            }


            {
                // we need to synchronize with pending writer tasks
                if (current->dirty_flag().exchange(false, std::memory_order_release))
                {
                    --mDirtyObjects;
                }
                mArchive->erase_sector(file, current->sector());
                utils::secure_memzero(current->data());
            }
            */

            --walker;
            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            file.size = std::max(newSize, size);
        }

        {
            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            if (file.size != size)
            {
                file.size = size;
            }
        }

        dealloc_sectors(std::move(collectedIds));

        mark_dirty(*walker);
    }

    void vefs::archive::write(detail::raw_archive_file & file, blob_view data, std::uint64_t writeFilePos)
    {
        auto startSectorPos = writeFilePos / raw_archive::sector_payload_size;

        std::shared_lock<std::shared_mutex> shrinkLock{ file.shrink_mutex };
        grow_file(file, writeFilePos + 1);

        auto offset = writeFilePos % raw_archive::sector_payload_size;
        file_walker walker{ *this, file, startSectorPos };

        auto newMinSize = writeFilePos + data.size();

        while (data)
        {
            auto sector = access_or_append(file, (walker++).current());
            auto chunk = sector->data().slice(offset);
            offset = 0;
            {
                std::lock_guard<std::mutex> sectorLock{ sector->write_mutex() };

                data.copy_to(chunk);

                mark_dirty(sector);

            }
            data.remove_prefix(chunk.size());

            std::lock_guard<std::shared_mutex> integrityLock{ file.integrity_mutex };
            auto newSize = std::min(walker.current().position() * raw_archive::sector_payload_size, newMinSize);
            file.size = std::max(file.size, newSize);
        }
    }

    void vefs::archive::write(detail::file_id id, blob_view data, std::uint64_t writeFilePos)
    {
        if (!data)
        {
            return;
        }
        raw_archive_file_ptr file;
        if (!mFiles.find_fn(id, [&file](const raw_archive_file_ptr &elem) { file = elem; }))
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        write(*file, data, writeFilePos);
    }

    void vefs::archive::resize(detail::file_id id, std::uint64_t size)
    {
        raw_archive_file_ptr file;
        if (!mFiles.find_fn(id, [&file](const raw_archive_file_ptr &elem) { file = elem; }))
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        std::unique_lock<std::shared_mutex> shrinkLock{ file->shrink_mutex };
        std::uint64_t fileSize;
        {
            std::unique_lock<std::shared_mutex> integrityLock{ file->integrity_mutex };
            fileSize = file->size;
        }

        if (fileSize < size)
        {
            shrinkLock.unlock();
            std::shared_lock<std::shared_mutex> growLock{ file->shrink_mutex };
            grow_file(*file, size);
        }
        else if (fileSize > size)
        {
            shrink_file(*file, size);
        }
    }

    std::uint64_t vefs::archive::size_of(detail::file_id id)
    {
        raw_archive_file_ptr file;
        if (!mFiles.find_fn(id, [&file](const raw_archive_file_ptr &elem) { file = elem; }))
        {
            BOOST_THROW_EXCEPTION(exception{});
        }

        std::shared_lock<std::shared_mutex> intLock{ file->integrity_mutex };

        return file->size;
    }

    void vefs::archive::sync(detail::file_id id)
    {
        raw_archive_file_ptr file;
        if (!mFiles.find_fn(id, [&file](const raw_archive_file_ptr &elem) { file = elem; }))
        {
            BOOST_THROW_EXCEPTION(exception{});
        }
    }

    void archive::read_archive_index()
    {
        using alloc_bitset_t = utils::bitset_overlay;

        const auto &indexDesc = mArchive->index_file();

        file_walker idxWalker{ *this, indexDesc };

        adesso::vefs::FileDescriptor descriptor;

        for (std::uint64_t consumed = 0; consumed < indexDesc.size;
            consumed += detail::raw_archive::sector_payload_size)
        {
            auto sector = *idxWalker++;

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

                auto currentFile = std::make_unique<detail::raw_archive_file>();
                {
                    if (!parse_blob(descriptor, sectorBlob.slice(2, descriptorLength)))
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{});
                    }
                    VEFS_SCOPE_EXIT{ erase_secrets(descriptor); };

                    unpack(*currentFile, descriptor);
                }

                mIndex.insert_or_assign(descriptor.filepath(), currentFile->id);
                mFiles.uprase_fn(currentFile->id, [&currentFile](raw_archive_file_ptr &f)
                {
                    assert(currentFile); // we depend on an implementation detail of uprase_fn
                    f = std::move(currentFile);
                    return false;
                }, std::move(currentFile));

                sectorBlob.remove_prefix(utils::div_ceil(sizeof(std::uint16_t) + descriptorLength, block_size));
            }
        }
    }

    void vefs::archive::write_archive_index()
    {
        using alloc_bitset_t = utils::bitset_overlay;

        auto lockedindex = mIndex.lock_table();
        auto &indexDesc = mArchive->index_file();

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
            auto file = mFiles.find(std::get<1>(*it));
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

                write(indexDesc, data, fileOffset);
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

            write(indexDesc, data, fileOffset);
            fileOffset += raw_archive::sector_payload_size;
        }

        {
            std::lock_guard<std::shared_mutex> indexLock{ indexDesc.shrink_mutex };
            shrink_file(indexDesc, fileOffset);
        }
    }

    void archive::read_free_sector_index()
    {
        const auto &free_sector_index = mArchive->free_sector_index_file();
        if (free_sector_index.size % sizeof(RawFreeSectorRange) != 0)
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        std::lock_guard<std::mutex> freeSectorLock{ mFreeSectorPoolMutex };
        file_walker index_walker{ *this, free_sector_index };

        for (std::uint64_t consumed = 0; consumed < free_sector_index.size; )
        {
            auto sector = *index_walker++;

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

    void vefs::archive::write_free_sector_index()
    {
        auto &freeSectorIndex = mArchive->free_sector_index_file();

        RawFreeSectorRange entry = {};
        const blob_view entryView = as_blob_view(entry);
        std::uint64_t writePos = 0;

        std::unique_lock<std::mutex> freeSectorLock{ mFreeSectorPoolMutex };

        do
        {
            while (freeSectorIndex.size / sizeof(RawFreeSectorRange) > mFreeSectorPool.size() + 2)
            {
                freeSectorLock.unlock();
                {
                    std::unique_lock<std::shared_mutex> shrinkLock{ freeSectorIndex.shrink_mutex };
                    shrink_file(freeSectorIndex, (mFreeSectorPool.size() + 2) * sizeof(RawFreeSectorRange));
                }
                freeSectorLock.lock();
            }

            if (freeSectorIndex.size / sizeof(RawFreeSectorRange) < mFreeSectorPool.size())
            {
                freeSectorLock.unlock();
                {
                    std::shared_lock<std::shared_mutex> shrinkLock{ freeSectorIndex.shrink_mutex };
                    grow_file(freeSectorIndex, mFreeSectorPool.size() * sizeof(RawFreeSectorRange));
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

            write(freeSectorIndex, entryView, writePos);
            writePos += entryView.size();
        }

        entry = {};
        while (writePos < freeSectorIndex.size)
        {
            write(freeSectorIndex, entryView, writePos);
            writePos += entryView.size();
        }
    }

    std::shared_ptr<file_sector> vefs::archive::access_or_append(detail::raw_archive_file & file, file_sector_id id)
    {
        assert(file.id == id.file_id());
        if (auto sector = mBlockPool->try_access(id))
        {
            return sector;
        }

        std::shared_lock<std::shared_mutex> fileReadLock{ file.integrity_mutex };
        if (id.layer() > file.tree_depth)
        {
            assert(id.position() == 0);
            fileReadLock.unlock();
            std::unique_lock<std::shared_mutex> fileWriteLock{ file.integrity_mutex };

            if (id.layer() <= file.tree_depth)
            {
                fileWriteLock.unlock();
                return access(file, id);
            }

            auto physId = alloc_sector();
            auto sector = std::get<1>(mBlockPool->access(id, id, physId));

            auto ref = &sector->data().as<RawSectorReference>();
            ref->reference = file.start_block_idx;
            ref->mac = file.start_block_mac;

            file.start_block_idx = physId;
            file.start_block_mac = {};
            file.tree_depth += 1;

            mark_dirty(sector);

            return sector;
        }

        fileReadLock.unlock();

        if (id.is_allocated(file.size))
        {
            return access(file, id);
        }
        if (id.position() == 1 || id.position_array_offset() == 0)
        {
            access_or_append(file, id.parent());
        }

        auto physId = alloc_sector();
        auto [cached, sector] = mBlockPool->access(id, id, physId);
        if (!cached)
        {
            mark_dirty(sector);
        }
        return sector;
    }

    std::map<detail::sector_id, std::uint64_t>::iterator vefs::archive::grow_archive_impl(unsigned int num)
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

    std::vector<detail::sector_id> vefs::archive::alloc_sectors(unsigned int num)
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

    void vefs::archive::dealloc_sectors(std::vector<detail::sector_id> sectors)
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
    void vefs::archive::dealloc_sectors_impl(std::vector<detail::sector_id> sectors)
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

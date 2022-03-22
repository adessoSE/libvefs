#pragma once

#include <atomic>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>

#include <vefs/archive_fwd.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/enum_bitset.hpp>

namespace vefs
{

enum class creation
{
    open_existing = 0,
    only_if_not_exist = 1,
    if_needed = 2,
    always_new = 4,
};

enum class file_open_mode
{
    read = 0b0000,
    write = 0b0001,
    readwrite = read | write,
    truncate = 0b0010,
    create = 0b0100,
};

/**
 * @brief Allows the user to specify if and how a back up of the VEFS archive
 * file is created if necessary.
 */
enum class backup_mode
{
    /**
     * @brief Do not create a backup.
     */
    none,
    /**
     * @brief Create a backup file automatically. A random file name is picked
     * by the implementation. The file is placed next to the current VEFS
     * archive file.
     */
    clone_extents,
};

std::true_type allow_enum_bitset(file_open_mode &&);
using file_open_mode_bitset = enum_bitset<file_open_mode>;

struct file_query_result
{
    file_open_mode_bitset allowed_flags;
    std::size_t size;
};

/**
 * @brief Handle to an encrypted archive file. Used to read and write virtual
 * files (vfiles) from the encrypted archive. All changes are only written once
 * ::commit() is called.
 *
 * From a user perspective the archive is an <em>encrypted virtual
 * filesystem</em> containing <em>encrypted virtual files</em> and an
 * <em>unencrypted personalization area of 2^12 bytes that can be used store
 * arbitrary data.
 *
 * An archive is made up of sectors of 2^15 bytes each. The first sector is the
 * master sector which contains the information necessary to decrypt and
 * interpret the other sectors.
*/
class archive_handle
{
    using sector_device_owner = std::unique_ptr<detail::sector_device>;
    using sector_allocator_owner
            = std::unique_ptr<detail::archive_sector_allocator>;
    using vfilesystem_owner = std::unique_ptr<vfilesystem>;
    using work_tracker_owner = std::unique_ptr<detail::pooled_work_tracker>;

    sector_device_owner mArchive;
    sector_allocator_owner mSectorAllocator;
    work_tracker_owner mWorkTracker;
    vfilesystem_owner mFilesystem;

public:
    ~archive_handle();
    archive_handle() noexcept;

    archive_handle(archive_handle const &) noexcept = delete;
    auto operator=(archive_handle const &) noexcept
            -> archive_handle & = delete;

    archive_handle(archive_handle &&other) noexcept;
    auto operator=(archive_handle &&other) noexcept -> archive_handle &;

    friend inline void swap(archive_handle &lhs, archive_handle &rhs) noexcept
    {
        using std::swap;
        swap(lhs.mArchive, rhs.mArchive);
        swap(lhs.mSectorAllocator, rhs.mSectorAllocator);
        swap(lhs.mWorkTracker, rhs.mWorkTracker);
        swap(lhs.mFilesystem, rhs.mFilesystem);
    }

    using creation = vefs::creation;

    /**
     * @brief Construct an archive. Gennerally, use the static ::archive
     * methods to create an archive. The constructed object takes ownership of
     * all objects passed to the constructor.
     *
     * @param sectorDevice the underlying sector device
     * @param sectorAllocator the underlying sector allocator
     * @param workTracker underlying thread pool
     * @param filesystem the underlying virtual filesystem
    */
    archive_handle(sector_device_owner sectorDevice,
                   sector_allocator_owner sectorAllocator,
                   work_tracker_owner workTracker,
                   vfilesystem_owner filesystem) noexcept;

    /**
     * @brief Create an archive handle from just an LLFIO file handle.
     *
     * @param file the LLFIO file handle to the encrypted archive file
     * @param userPRK the key used to decrypt and encrypt the archive
     * @param cryptoProvider provides the underyling cryptographic procedures
     * @param creationMode creation in regards to the file
     * @return the archive or errors that occured while creating the archive
     *         handle
    */
    static auto archive(llfio::file_handle const &file,
                        ro_blob<32> userPRK,
                        crypto::crypto_provider *cryptoProvider
                        = crypto::boringssl_aes_256_gcm_crypto_provider(),
                        creation creationMode = creation::open_existing)
            -> result<archive_handle>;
    /**
     * @brief Create an archive from an LLFIO path handle and file.
     *
     * @param base the LLFIO base path to the encrypted archive file
     * @param path the LLFIO path to the encrypted file
     * userPRK the key used to decrypt and encrypt the archive
     * @param cryptoProvider provides the underyling cryptographic procedures
     * @param creationMode creation in regards to the file
     * @return the archive or errors that occured while creating the archive
     *         handle
    */
    static auto archive(llfio::path_handle const &base,
                        llfio::path_view path,
                        ro_blob<32> userPRK,
                        crypto::crypto_provider *cryptoProvider
                        = crypto::boringssl_aes_256_gcm_crypto_provider(),
                        creation creationMode = creation::open_existing)
            -> result<archive_handle>;

    /**
     * @brief Delete any files with corrupted sectors.
     *
     * Temporarily creates a working copy in the directory of the archive with
     * \c .tmp. and 16 random characters appended to the filename.
     *
     * If \c createBackup is \c true, then a backup of the corrupted file (i.e.
     * the file \a before corruption was purged) is created with \c .tmp. and
     * 16 random characters appended to the filename of the original archive
     * file.
     *
     * @param base the LLFIO path handle
     * @param path the LLFIO path to the file relative to the handle
     * @param userPRK the key to the encrypted archive
     * @param cryptoProvider provider for cryptographic operations
     * @param backupMode selects whether and how to create a backup
     * @return indicates success or failure of the operation
    */
    static auto purge_corruption(llfio::path_handle const &base,
                                 llfio::path_view path,
                                 ro_blob<32> userPRK,
                                 crypto::crypto_provider *cryptoProvider,
                                 backup_mode backupMode)
            -> result<void>;

    /**
     * @brief Validate that the given file is a legible archive. An archive
     * file is legible if the contents can be decrypted with the given key and
     * passes all checksum tests.
     *
     * @param base the LLFIO base path to the encrypted archive file
     * @param path the LLFIO path to the encrypted file
     * @param userPRK the key to the encrypted archive
     * @param cryptoProvider provider for cryptographic operations
     * @return indicates success or failure of the validation. In the case of
     *         failure further details are given in the form of errors.
    */
    static auto validate(llfio::path_handle const &base,
                         llfio::path_view path,
                         ro_blob<32> userPRK,
                         crypto::crypto_provider *cryptoProvider)
            -> result<void>;

    /**
     * @brief Commit pending changes by writing them to the underyling encrypted
     * archive file.
     *
     * @return indicates success or failure
    */
    auto commit() -> result<void>;

    /**
     * @brief Open a virtual file within the encrypted archive.
     *
     * @param filePath the path to the file within the encrypted archive
     * @param mode the filemode used to open the virtual file
     * @return a handle to the virtual file or an error
    */
    auto open(const std::string_view filePath, const file_open_mode_bitset mode)
            -> result<vfile_handle>;

    /**
     * @brief Query some information, such as the file size, about a virtual
     * file within the encrypted archive.
     *
     * @param filePath the path to the file within the encrypted archive
     * @return the information about the file or an error
    */
    auto query(const std::string_view filePath) -> result<file_query_result>;

    /**
     * @brief Delete a virtual file from the encrypted archive.
     *
     * Unlike normal filesystem delete operations, the file isn't just unlinked
     * it completely erased.
     *
     * @param filePath the path to the file to be deleted
     * @return indicates success or failure
    */
    auto erase(std::string_view filePath) -> result<void>;

    /**
     * @brief Read some data into a buffer from a virtual file, starting with
     * the given position.
     * 
     * @param handle a handle to a virtual file within the encrypted archive
     * @param buffer the buffer into which the bytes of the file are written
     * @param readFilePos the position, in bytes, from where to start reading
     * the file
     * @return indicates success or failure
    */
    auto read(const vfile_handle &handle,
              rw_dynblob buffer,
              std::uint64_t readFilePos) -> result<void>;

    /**
     * @brief Write some data into a virtual file from a buffer starting from
     * a position.
     * 
     * @param handle a handle to a virtual file within the encrypted archive
     * @param data the buffer with the data to be written
     * @param writeFilePos the position, in bytes, from where to start writing
     * to the file
     * @return indicates success or failure
    */
    auto write(const vfile_handle &handle,
               ro_dynblob data,
               std::uint64_t writeFilePos) -> result<void>;

    /**
     * @brief Force the given virtual file into the given size (in bytes) by
     * truncating the end of the file, if necessary.
     * 
     * @param handle a handle to a virtual file within the encrypted archive
     * @param maxExtent the maximum number of bytes that the virtual file is to
     * have after the method completes
     * @return indicates success or failure
    */
    auto truncate(const vfile_handle &handle, std::uint64_t maxExtent)
            -> result<void>;

    /**
     * @brief Returns the maximum number of bytes that can be stored in the
     * given virtual file.
     * 
     * @param handle a handle to a virtual file within the encrypted archive
     * @return the maximum number of bytes or result that indicates how and why
     * the method failed
    */
    auto maximum_extent_of(const vfile_handle &handle) -> result<std::uint64_t>;

    /**
     * @brief Commit and pending write opertions for a virtual file into the
     * underlying archive file. Changes (like from write) are only persisted
     * once this method has been called.
     * 
     * @param handle a handle to a virtual file within the encrypted archive
     * @return indicates success or failure
    */
    auto commit(const vfile_handle &handle) -> result<void>;

    /**
     * @brief Returns the unencrypted bytes of the personalization area. The
     * bytes within the returned span can be modified, but must be synced to the
     * underlying archive file via ::sync_personalization_area().
     * 
     * @return the personalization area
    */
    auto personalization_area() noexcept -> std::span<std::byte, 1 << 12>;

    /**
     * @brief Synchronize any changes to the personalization area to the
     * underlying archive file.
     * 
     * @return indicates success or failure
    */
    auto sync_personalization_area() noexcept -> result<void>;

private:
    auto ops_pool() -> detail::pooled_work_tracker &;

    static auto open_existing(llfio::file_handle mfh,
                              crypto::crypto_provider *cryptoProvider,
                              ro_blob<32> userPRK) noexcept
            -> result<archive_handle>;
    static auto create_new(llfio::file_handle mfh,
                           crypto::crypto_provider *cryptoProvider,
                           ro_blob<32> userPRK) noexcept
            -> result<archive_handle>;

    static auto purge_corruption(llfio::file_handle &&file,
                                 ro_blob<32> userPRK,
                                 crypto::crypto_provider *cryptoProvider)
            -> result<void>;
};

inline auto vefs::archive_handle::ops_pool() -> detail::pooled_work_tracker &
{
    return *mWorkTracker;
}

inline auto archive(llfio::file_handle const &file,
                    ro_blob<32> userPRK,
                    crypto::crypto_provider *cryptoProvider
                    = crypto::boringssl_aes_256_gcm_crypto_provider(),
                    creation creationMode = creation::open_existing)
{
    return archive_handle::archive(file, userPRK, cryptoProvider, creationMode);
}
inline auto archive(llfio::path_handle const &base,
                    llfio::path_view path,
                    ro_blob<32> userPRK,
                    crypto::crypto_provider *cryptoProvider
                    = crypto::boringssl_aes_256_gcm_crypto_provider(),
                    creation creationMode = creation::open_existing)
        -> result<archive_handle>
{
    return archive_handle::archive(base, std::move(path), userPRK,
                                   cryptoProvider, creationMode);
}

auto read_archive_personalization_area(
        llfio::path_handle const &base,
        llfio::path_view where,
        std::span<std::byte, 1 << 12> out) noexcept -> result<void>;

} // namespace vefs

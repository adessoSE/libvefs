#include "sector_device.hpp"

#include <array>
#include <optional>
#include <random>

#include <dplx/dp.hpp>
#include <dplx/dp/api.hpp>
#include <dplx/dp/codecs/auto_tuple.hpp>
#include <dplx/dp/codecs/std-container.hpp>
#include <dplx/dp/items/emit_core.hpp>
#include <dplx/dp/items/encoded_item_head_size.hpp>
#include <dplx/dp/items/parse_core.hpp>
#include <dplx/dp/items/parse_ranges.hpp>
#include <dplx/dp/legacy/memory_input_stream.hpp>
#include <dplx/dp/legacy/memory_output_stream.hpp>

#include <vefs/span.hpp>
#include <vefs/utils/misc.hpp>

#include <vefs/utils/secure_allocator.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/cbor_box.hpp"
#include "../crypto/kdf.hpp"
#include "../platform/sysrandom.hpp"
#include "archive_file_id.hpp"
#include "io_buffer_manager.hpp"

template <>
class dplx::dp::codec<vefs::detail::master_header>
{
    using master_header = vefs::detail::master_header;

public:
    static auto decode(parse_context &ctx, master_header &value) noexcept
            -> result<void>
    {
        DPLX_TRY(auto headerHead, dp::decode_tuple_head(ctx, std::true_type{}));
        if (headerHead.version != 0)
        {
            return errc::item_version_mismatch;
        }
        if (headerHead.num_properties != 2)
        {
            return errc::tuple_size_mismatch;
        }

        DPLX_TRY(dp::expect_item_head(ctx, type_code::binary,
                                      value.master_secret.size()));
        DPLX_TRY(ctx.in.bulk_read(value.master_secret.data(),
                                  value.master_secret.size()));

        return dp::decode(ctx, value.master_counter);
    }
    static auto size_of(emit_context &ctx, master_header const &value) noexcept
            -> std::uint64_t
    {
        return dp::encoded_item_head_size<type_code::array>(3U)
             + dp::item_size_of_integer(ctx, 0U)
             + dp::encoded_size_of(ctx, value.master_secret)
             + dp::item_size_of_binary(ctx, vefs::crypto::counter::state_size);
    }
    static auto encode(emit_context &ctx, master_header const &value) noexcept
            -> result<void>
    {
        DPLX_TRY(dp::emit_array(ctx, 3U));
        DPLX_TRY(dp::emit_integer(ctx, 0U)); // version prop

        DPLX_TRY(dp::encode(ctx, value.master_secret));

        auto const counter = value.master_counter.load();
        return dp::encode(ctx, counter.view());
    }
};

using namespace vefs::utils; // to be refactored

namespace vefs::detail
{
namespace
{
constexpr auto file_format_id = utils::make_byte_array(0x82,
                                                       0x4E,
                                                       0x0D,
                                                       0x0A,
                                                       0xAB,
                                                       0x7E,
                                                       0x7B,
                                                       0x76,
                                                       0x65,
                                                       0x66,
                                                       0x73,
                                                       0x7D,
                                                       0x7E,
                                                       0xBB,
                                                       0x0A,
                                                       0x1A);

template <std::size_t N>
inline auto byte_literal(char const (&arr)[N]) noexcept
{
    std::span<char const, N> literalMem{arr};
    return as_bytes(literalMem.template first<N - 1>());
}

auto const archive_static_header_kdf_prk
        = byte_literal("vefs/prk/StaticArchiveHeaderPRK");
auto const archive_static_header_kdf_salt
        = byte_literal("vefs/salt/StaticArchiveHeaderWriteCounter");
auto const archive_header_kdf_prk = byte_literal("vefs/prk/ArchiveHeaderPRK");
auto const archive_header_kdf_salt
        = byte_literal("vefs/salt/ArchiveSecretCounter");

auto const archive_secret_counter_kdf
        = byte_literal("vefs/seed/ArchiveSecretCounter");
auto const archive_journal_counter_kdf
        = byte_literal("vefs/seed/JournalCounter");

auto const sector_kdf_salt = byte_literal("vefs/salt/Sector-Salt");
auto const sector_kdf_erase = byte_literal("vefs/erase/Sector");
auto const sector_kdf_prk = byte_literal("vefs/prk/SectorPRK");

auto const file_kdf_secret = byte_literal("vefs/seed/FileSecret");
auto const file_kdf_counter = byte_literal("vefs/seed/FileSecretCounter");

#pragma pack(push, 1)

struct StaticArchiveHeaderPrefix
{
    std::array<std::byte, 4> magic_number;
    std::array<std::byte, 32> static_header_salt;
    std::array<std::byte, 16> static_header_mac;
    uint32_t static_header_length;
};
static_assert(sizeof(StaticArchiveHeaderPrefix) == 56);

struct ArchiveHeaderPrefix
{
    std::array<std::byte, 32> header_salt;
    std::array<std::byte, 16> header_mac;
    uint32_t header_length;

    constexpr static auto unencrypted_prefix_size
            = sizeof(ArchiveHeaderPrefix::header_salt)
            + sizeof(ArchiveHeaderPrefix::header_mac);
};
static_assert(sizeof(ArchiveHeaderPrefix) == 52);

#pragma pack(pop)
} // namespace

auto sector_device::create_file_secrets() noexcept
        -> result<std::unique_ptr<file_crypto_ctx>>
{
    utils::secure_byte_array<32> fileSecret{};
    auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
    VEFS_TRY(crypto::kdf(as_span(fileSecret), master_secret_view(),
                         file_kdf_secret, ctrValue, session_salt_view()));

    crypto::counter::state fileWriteCtrState;
    ctrValue = mArchiveSecretCounter.fetch_increment().value();
    VEFS_TRY(crypto::kdf(as_writable_bytes(as_span(fileWriteCtrState)),
                         master_secret_view(), file_kdf_counter, ctrValue));

    std::unique_ptr<file_crypto_ctx> ctx{new (std::nothrow) file_crypto_ctx(
            as_span(fileSecret), crypto::counter{fileWriteCtrState})};
    if (!ctx)
    {
        return errc::not_enough_memory;
    }
    return ctx;
}

auto sector_device::create_file_secrets2() noexcept
        -> result<file_crypto_ctx::state_type>
{
    utils::secure_byte_array<32> fileSecret{};
    auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
    VEFS_TRY(crypto::kdf(as_span(fileSecret), master_secret_view(),
                         file_kdf_secret, ctrValue, session_salt_view()));

    crypto::counter::state fileWriteCtrState;
    ctrValue = mArchiveSecretCounter.fetch_increment().value();
    VEFS_TRY(crypto::kdf(as_writable_bytes(as_span(fileWriteCtrState)),
                         master_secret_view(), file_kdf_counter, ctrValue));

    return file_crypto_ctx::state_type{fileSecret,
                                       crypto::counter{fileWriteCtrState}};
}

sector_device::sector_device(llfio::file_handle file,
                             crypto::crypto_provider *cryptoProvider,
                             size_t const numSectors)
    : mCryptoProvider(cryptoProvider)
    , mArchiveFile(std::move(file))
    , mArchiveFileLock(mArchiveFile, llfio::lock_kind::unlocked)
    , mSessionSalt(cryptoProvider->generate_session_salt())
    , mNumSectors(numSectors)
{
}

auto sector_device::open_existing(llfio::file_handle fileHandle,
                                  crypto::crypto_provider *cryptoProvider,
                                  ro_blob<32> userPRK) noexcept
        -> result<open_info>
{
    VEFS_TRY(auto &&max_extent, fileHandle.maximum_extent());
    size_t const numSectors = max_extent / sector_size;

    if (numSectors < 1)
    {
        return archive_errc::no_archive_header;
    }

    std::unique_ptr<sector_device> archive{new (std::nothrow) sector_device(
            std::move(fileHandle), cryptoProvider, numSectors)};

    if (!archive)
    {
        return errc::not_enough_memory;
    }
    if (!archive->mArchiveFileLock.try_lock())
    {
        return archive_errc::still_in_use;
    }

    VEFS_TRY(archive->mIoBufferManager,
             io_buffer_manager::create(
                     sector_size, std::thread::hardware_concurrency() * 2U));
    VEFS_TRY(archive->mMasterSector.resize(sector_size));
    auto const buffer = archive->mMasterSector.as_span();
    llfio::byte_io_handle::buffer_type masterSectorBuffer[] = {buffer};

    VEFS_TRY(auto &&readBuffers,
             archive->mArchiveFile.read({masterSectorBuffer, 0}));
    if (readBuffers.size() != 1U || readBuffers[0].size() < sector_size)
    {
        return archive_errc::no_archive_header;
    }
    if (readBuffers[0].data() != buffer.data())
    {
        std::memcpy(buffer.data(), readBuffers[0].data(), sector_size);
    }

    VEFS_TRY_INJECT(archive->parse_static_archive_header(userPRK),
                    ed::archive_file{"[archive-static-header]"}
                            << ed::sector_idx{sector_id::master});

    if (auto headerRx = archive->parse_archive_header(); headerRx.has_value())
    {
        auto &&header = std::move(headerRx).assume_value();
        archive->mArchiveSecretCounter.store(
                crypto::counter(std::span(header.archive_secret_counter)));
        archive->mJournalCounter.store(
                crypto::counter(std::span(header.journal_counter)));

        return open_info{std::move(archive),
                         master_file_info(header.filesystem_index),
                         master_file_info(header.free_sector_index)};
    }
    else
    {
        return std::move(headerRx).assume_error()
            << ed::archive_file{"[archive-header]"}
            << ed::sector_idx{sector_id::master};
    }
}

auto sector_device::create_new(llfio::file_handle fileHandle,
                               crypto::crypto_provider *cryptoProvider,
                               ro_blob<32> userPRK) noexcept
        -> result<open_info>
{
    std::unique_ptr<sector_device> archive{new (std::nothrow) sector_device(
            std::move(fileHandle), cryptoProvider, 0)};

    if (!archive)
    {
        return errc::not_enough_memory;
    }
    if (!archive->mArchiveFileLock.try_lock())
    {
        return archive_errc::still_in_use;
    }

    VEFS_TRY(archive->mIoBufferManager,
             io_buffer_manager::create(
                     sector_size, std::thread::hardware_concurrency() * 2U));
    VEFS_TRY(archive->mMasterSector.resize(sector_size));

    VEFS_TRY(archive->resize(1));

    VEFS_TRY(cryptoProvider->random_bytes(
            as_span(archive->mStaticHeader.master_secret)));

    crypto::counter::state counterState;
    VEFS_TRY(cryptoProvider->random_bytes(
            as_writable_bytes(as_span(counterState))));
    archive->mStaticHeader.master_counter.store(crypto::counter(counterState));

    std::memset(archive->mMasterSector.as_span().data(), 0, sector_size);

    VEFS_TRY(archive->write_static_archive_header(userPRK));

    open_info self{
            .device = std::move(archive),
            .filesystem_index = {},
            .free_sector_index = {},
    };
    VEFS_TRY(self.filesystem_index.crypto_state,
             self.device->create_file_secrets2());

    VEFS_TRY(self.free_sector_index.crypto_state,
             self.device->create_file_secrets2());

    return self;
}

result<void> sector_device::parse_static_archive_header(ro_blob<32> userPRK)
{
    auto const staticHeaderSectors
            = mMasterSector.as_span().first(static_header_size);

    dplx::dp::memory_buffer mstream(staticHeaderSectors);

    // check for magic number
    if (std::span<std::byte, file_format_id.size()> archivePrefix(
                mstream.consume(file_format_id.size()), file_format_id.size());
        !std::ranges::equal(archivePrefix, std::span(file_format_id)))
    {
        return archive_errc::invalid_prefix;
    }

    VEFS_TRY(auto &&staticHeaderBox, crypto::cbor_box_decode_head(mstream));

    if (staticHeaderBox.dataLength > static_cast<int>(static_header_size))
    {
        return archive_errc::oversized_static_header;
    }

    secure_byte_array<44> keyNonce;
    VEFS_TRY(crypto::kdf(as_span(keyNonce), userPRK, staticHeaderBox.salt));

    auto const staticHeader
            = mstream.remaining().first(staticHeaderBox.dataLength);

    if (auto rx = mCryptoProvider->box_open(staticHeader, as_span(keyNonce),
                                            staticHeader, staticHeaderBox.mac);
        rx.has_failure())
    {
        if (rx.has_error() && rx.assume_error() == archive_errc::tag_mismatch)
        {
            return archive_errc::wrong_user_prk
                << ed::wrapped_error{std::move(rx).assume_error()};
        }
        return std::move(rx).as_failure();
    }
    VEFS_SCOPE_EXIT
    {
        utils::secure_memzero(staticHeader);
    };

    dplx::dp::memory_view staticHeaderStream(staticHeader);

    VEFS_TRY(dplx::dp::decode(staticHeaderStream, mStaticHeader));
    return success();
}

auto read_archive_personalization_area(
        llfio::file_handle &file, std::span<std::byte, 1 << 12> out) noexcept
        -> result<void>
{
    std::byte masterSectorMemory[sector_device::static_header_size];

    static_assert(1 << 12 == sector_device::personalization_area_size);
    llfio::byte_io_handle::buffer_type outBuffers[] = {
            {masterSectorMemory, sizeof(masterSectorMemory)},
            {        out.data(),                 out.size()}
    };

    VEFS_TRY(auto &&readBuffers, file.read({outBuffers, 0}));

    if (readBuffers[0].size() != sector_device::static_header_size
        || readBuffers[1].size() != sector_device::personalization_area_size)
    {
        vefs::fill_blob(out);
        return archive_errc::no_archive_header;
    }
    if (!std::ranges::equal(
                std::span(readBuffers[0]).first<file_format_id.size()>(),
                std::span(file_format_id)))
    {
        vefs::fill_blob(out);
        return archive_errc::invalid_prefix;
    }
    if (readBuffers[1].data() != out.data())
    {
        vefs::copy(std::span(readBuffers[1]), out);
    }

    return oc::success();
}

auto sector_device::parse_archive_header(header_id which)
        -> result<archive_header>
{
    auto const offset = header_offset(which);
    auto const encryptedHeaderArea
            = mMasterSector.as_span().subspan(offset, pheader_size);

    dplx::dp::memory_buffer mstream(encryptedHeaderArea);

    VEFS_TRY(auto &&headerBox, crypto::cbor_box_decode_head(mstream));

    if (headerBox.dataLength > static_cast<int>(pheader_size))
    {
        return archive_errc::oversized_static_header;
    }

    secure_byte_array<44> keyNonce;
    VEFS_TRY(crypto::kdf(as_span(keyNonce), master_secret_view(),
                         archive_header_kdf_prk, headerBox.salt));

    auto const headerArea = mstream.remaining().first(headerBox.dataLength);

    VEFS_TRY(mCryptoProvider->box_open(headerArea, as_span(keyNonce),
                                       headerArea, headerBox.mac));
    VEFS_SCOPE_EXIT
    {
        utils::secure_memzero(headerArea);
    };

    dplx::dp::memory_view headerStream{headerArea};

    archive_header header{};
    VEFS_TRY(dplx::dp::decode(headerStream, header));

    return header;
}

auto sector_device::parse_archive_header() -> result<archive_header>
{
    result<archive_header> header[2]
            = {parse_archive_header(header_id::first),
               parse_archive_header(header_id::second)};

    int selector;
    // determine which header to apply
    if (header[0] && header[1])
    {
        VEFS_TRY(auto &&cmp,
                 mCryptoProvider->ct_compare(
                         header[0].assume_value().archive_secret_counter,
                         header[1].assume_value().archive_secret_counter));
        if (0 == cmp)
        {
            // both headers are at the same counter value which is an
            // invalid state which cannot be produced by a conforming
            // implementation
            return archive_errc::identical_header_version;
        }

        // select the header with the greater counter value
        selector = 0 < cmp ? 0 : 1;
    }
    else if (header[0])
    {
        selector = 0;
    }
    else if (header[1])
    {
        selector = 1;
    }
    else
    {
        return archive_errc::no_archive_header
            << ed::wrapped_error{std::move(header[0]).assume_error()};
    }
    return std::move(header[selector]);

    // mFilesystemIndex = std::move(header[selector].filesystem_index);
    // mFreeSectorIndex = std::move(header[selector].free_sector_index);

    // mArchiveSecretCounter.store(
    //    crypto::counter(header[selector].archive_secret_counter));
    // mJournalCounter.store(
    //    crypto::counter(header[selcetor].journal_counter));
    // mHeaderSelector = static_cast<header_id>(selector);
}

result<void> sector_device::write_static_archive_header(ro_blob<32> userPRK)
{
    dplx::dp::memory_buffer staticHeaderSectors(
            mMasterSector.as_span().first(static_header_size));

    // insert file format id
    std::memcpy(staticHeaderSectors.consume(file_format_id.size()),
                file_format_id.data(), file_format_id.size());

    // we need to increment the master key counter _before_ we
    // synthesize the static archive header, because otherwise the
    // counter value used for this encryption round gets serialized
    // and reused.
    auto keyUsageCount = mStaticHeader.master_counter.fetch_increment();

    std::array<std::byte, static_header_size> encodingBuffer;
    dplx::dp::memory_buffer plainStream(encodingBuffer.data(),
                                        encodingBuffer.size(), 0);
    VEFS_SCOPE_EXIT
    {
        utils::secure_memzero(encodingBuffer);
    };

    VEFS_TRY(dplx::dp::encode(plainStream, mStaticHeader));
    auto const encoded = plainStream.consumed();

    VEFS_TRY(auto &&boxHead,
             crypto::cbor_box_layout_head(
                     staticHeaderSectors,
                     static_cast<std::uint16_t>(encoded.size())));

    VEFS_TRY(crypto::kdf(boxHead.salt, keyUsageCount.view(),
                         archive_static_header_kdf_salt, session_salt_view()));

    secure_byte_array<44> key;
    VEFS_TRY(crypto::kdf(as_span(key), userPRK, boxHead.salt));

    VEFS_TRY(mCryptoProvider->box_seal(
            rw_dynblob(staticHeaderSectors.consume(encoded.size()),
                       encoded.size()),
            boxHead.mac, key, encoded));

    std::memset(
            staticHeaderSectors.consume(staticHeaderSectors.remaining_size()),
            0, staticHeaderSectors.remaining_size());

    llfio::file_handle::const_buffer_type writeBuffers[]
            = {mMasterSector.as_span().first(
                    std::max(static_header_size, mIoBufferManager.page_size))};
    VEFS_TRY(mArchiveFile.write({writeBuffers, 0}));

    return oc::success();
}

auto sector_device::sync_personalization_area() noexcept -> result<void>
{
    std::span<std::byte const> persArea = personalization_area();

    llfio::file_handle::const_buffer_type writeBuffers[] = {persArea};
    VEFS_TRY(mArchiveFile.write({writeBuffers, static_header_size}));

    return oc::success();
}

auto sector_device::read_sector(rw_blob<sector_payload_size> contentDest,
                                file_crypto_ctx const &fileCtx,
                                sector_id sectorIdx,
                                ro_blob<16> contentMAC) noexcept -> result<void>
{
    using io_buffer = llfio::file_handle::buffer_type;

    constexpr auto sectorIdxLimit
            = std::numeric_limits<std::uint64_t>::max() / sector_size;
    if (sectorIdx == sector_id::master)
    {
        return errc::invalid_argument;
    }
    if (static_cast<std::uint64_t>(sectorIdx) >= sectorIdxLimit)
    {
        return errc::invalid_argument;
    }
    if (contentDest.size() != sector_device::sector_payload_size)
    {
        return errc::invalid_argument;
    }

    VEFS_TRY(auto ioBuffer, mIoBufferManager.allocate());
    utils::scope_guard deallocationGuard = [&]
    {
        mIoBufferManager.deallocate(ioBuffer);
    };

    auto const sectorOffset = to_offset(sectorIdx);
    io_buffer reqBuffers[] = {ioBuffer};

    if (auto readrx = mArchiveFile.read({reqBuffers, sectorOffset}))
    {
        auto const buffers = readrx.assume_value();
        assert(buffers.size() == 1U);
        assert(buffers[0].size() == sector_size);
        assert(buffers[0].data() != nullptr);

        VEFS_TRY_INJECT(
                fileCtx.unseal_sector(
                        contentDest, *mCryptoProvider,
                        std::span<std::byte const, sector_size>(buffers[0]),
                        contentMAC),
                ed::sector_idx{sectorIdx});
        return oc::success();
    }
    else
    {
        result<void> adaptedrx{std::move(readrx).as_failure()};
        adaptedrx.assume_error() << ed::sector_idx{sectorIdx};
        return adaptedrx;
    }
}

template <typename file_crypto_ctx_T>
auto sector_device::write_sector(
        rw_blob<16> const mac,
        file_crypto_ctx_T &fileCtx,
        sector_id const sectorIdx,
        ro_blob<sector_payload_size> const data) noexcept -> result<void>
{
    constexpr auto sectorIdxLimit
            = std::numeric_limits<std::uint64_t>::max() / sector_size;
    if (sectorIdx == sector_id::master)
    {
        return errc::invalid_argument;
    }
    if (static_cast<std::uint64_t>(sectorIdx) >= sectorIdxLimit)
    {
        return errc::invalid_argument;
    }

    VEFS_TRY(auto const ioBuffer, mIoBufferManager.allocate());
    utils::scope_guard deallocationGuard = [&]
    {
        mIoBufferManager.deallocate(ioBuffer);
    };

    VEFS_TRY_INJECT(fileCtx.seal_sector(
                            std::span<std::byte, sector_size>(ioBuffer), mac,
                            *mCryptoProvider, session_salt_view(), data),
                    ed::sector_idx{sectorIdx});

    auto const sectorOffset = to_offset(sectorIdx);
    llfio::file_handle::const_buffer_type reqBuffers[] = {ioBuffer};

    VEFS_TRY_INJECT(mArchiveFile.write({reqBuffers, sectorOffset}),
                    ed::sector_idx{sectorIdx});

    return oc::success();
}

auto sector_device::erase_sector(sector_id const sectorIdx) noexcept
        -> result<void>
{
    if (sectorIdx == sector_id::master)
    {
        return errc::invalid_argument;
    }

    VEFS_TRY(auto const ioBuffer, mIoBufferManager.allocate());
    utils::scope_guard deallocationGuard = [&]
    {
        mIoBufferManager.deallocate(ioBuffer);
    };
    auto const writeBuffer = ioBuffer.first(io_buffer_manager::page_size);

    auto const nonce = mEraseCounter.fetch_add(1, std::memory_order::relaxed);
    VEFS_TRY(crypto::kdf(writeBuffer, mSessionSalt, ro_blob_cast(nonce),
                         sector_kdf_erase));

    auto const sectorOffset = to_offset(sectorIdx);
    llfio::file_handle::const_buffer_type reqBuffers[] = {writeBuffer};

    VEFS_TRY_INJECT(mArchiveFile.write({reqBuffers, sectorOffset}),
                    ed::sector_idx{sectorIdx});
    return oc::success();
}

auto vefs::detail::sector_device::update_header(
        file_crypto_ctx const &filesystemIndexCtx,
        root_sector_info filesystemIndexRoot,
        file_crypto_ctx const &freeSectorIndexCtx,
        root_sector_info freeSectorIndexRoot) -> result<void>
{
    archive_header assembled{
            .filesystem_index = {   detail::file_id::archive_index.as_uuid(),
                                 filesystemIndexCtx, filesystemIndexRoot,},
            .free_sector_index = {detail::file_id::free_block_index.as_uuid(),
                                 freeSectorIndexCtx, freeSectorIndexRoot,},
            .archive_secret_counter = { },
 .journal_counter = {                                 },
    };

    // fetch a counter value before serialization for header encryption
    auto const ectr = mArchiveSecretCounter.fetch_increment().value();

    vefs::copy(as_bytes(mArchiveSecretCounter.fetch_increment().view()),
               std::span(assembled.archive_secret_counter));
    vefs::copy(as_bytes(mJournalCounter.fetch_increment().view()),
               std::span(assembled.journal_counter));

    switch_header();

    std::byte serializationMemory[pheader_size];
    VEFS_SCOPE_EXIT
    {
        utils::secure_memzero(serializationMemory);
    };
    dplx::dp::memory_buffer serializationBuffer{
            std::span<std::byte>{serializationMemory}};

    VEFS_TRY(dplx::dp::encode(serializationBuffer, assembled));

    auto const headerOffset = header_offset(mHeaderSelector);
    auto const writeArea
            = mMasterSector.as_span().subspan(headerOffset, pheader_size);

    dplx::dp::memory_buffer encryptionBuffer(writeArea);
    VEFS_TRY(auto &&box, crypto::cbor_box_layout_head(
                                 encryptionBuffer,
                                 static_cast<std::uint16_t>(
                                         serializationBuffer.consumed_size())));

    VEFS_TRY(crypto::kdf(box.salt, as_bytes(std::span(ectr)),
                         archive_header_kdf_salt, mSessionSalt));

    secure_byte_array<44> headerKeyNonce;
    VEFS_TRY(crypto::kdf(as_span(headerKeyNonce), master_secret_view(),
                         archive_header_kdf_prk, box.salt));

    VEFS_TRY_INJECT(mCryptoProvider->box_seal(encryptionBuffer.remaining(),
                                              box.mac, as_span(headerKeyNonce),
                                              serializationBuffer.consumed()),
                    ed::archive_file{"[archive-header]"});

    encryptionBuffer.move_consumer(serializationBuffer.consumed_size());
    std::memset(encryptionBuffer.remaining_begin(), 0,
                encryptionBuffer.remaining_size());

    VEFS_TRY_INJECT(
            mArchiveFile.write(headerOffset,
                               {
                                       {writeArea.data(), writeArea.size()}
    }),
            ed::archive_file{"[archive-header]"});

    return oc::success();
}

result<void>
vefs::detail::sector_device::update_static_header(ro_blob<32> newUserPRK)
{
    return write_static_archive_header(newUserPRK);
}

template result<void>
sector_device::write_sector(rw_blob<16> mac,
                            file_crypto_ctx &fileCtx,
                            sector_id sectorIdx,
                            ro_blob<sector_payload_size> data);
template result<void> sector_device::write_sector<file_crypto_ctx_interface>(
        rw_blob<16> mac,
        file_crypto_ctx_interface &fileCtx,
        sector_id sectorIdx,
        ro_blob<sector_payload_size> data);

} // namespace vefs::detail

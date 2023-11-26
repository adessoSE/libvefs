#include "vefs/cli/commandlets/extract_personalization.hpp"

#include <array>
#include <chrono>
#include <cstddef>

#include <boost/endian/conversion.hpp>
#include <dplx/scope_guard.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>

#include "vefs/cli/error.hpp"

namespace vefs::cli
{

auto extract_personalization::exec(lyra::group const &) const -> result<void>
{
    using namespace std::chrono_literals;
    using file_handle = llfio::file_handle;

    std::array<std::byte, 1 << 12> personalizationContent{};
    VEFS_TRY(vefs::read_archive_personalization_area({}, mArchiveOptions.path,
                                                     personalizationContent));

    VEFS_TRY(auto &&outFile,
             llfio::file({}, mTargetFile, file_handle::mode::write,
                         file_handle::creation::always_new));
    dplx::scope_guard outFileRemover = [&outFile] {
        if (outFile.is_valid())
        {
            (void)outFile.unlink(3s);
            (void)outFile.close();
        }
    };

    file_handle::const_buffer_type outBuffers[1] = {};

    if (mArchiveOptions.mdcProvider)
    {
        unsigned boxSize
                = boost::endian::load_big_u16(reinterpret_cast<unsigned char *>(
                        personalizationContent.data()));

        if (boxSize > (1u << 12) - 2u)
        {
            return cli_errc::malformed_mdc_key_box;
        }

        outBuffers[0] = ro_dynblob(personalizationContent).subspan(2U, boxSize);
    }
    else
    {
        outBuffers[0] = ro_dynblob(personalizationContent);
    }

    VEFS_TRY(outFile.write({outBuffers, 0U}));

    VEFS_TRY(outFile.close());
    return oc::success();
}

} // namespace vefs::cli

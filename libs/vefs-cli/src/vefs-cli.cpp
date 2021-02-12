
#include <cstddef>

#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include <vefs/archive.hpp>

namespace bpo = boost::program_options;

int main(int argc, char *argv[])
{
    using std::string;
    using std::vector;

    bpo::options_description globalopts{"vefs-cli options"};
    globalopts.add_options()("help", "outputs this useful help dialog")(
            "archive-path,a", bpo::value<string>(), "the path to the archive")(
            "command", bpo::value<string>(), "archive command to execute")(
            "cmdargs", bpo::value<vector<string>>(), "arguments for command");
    ;

    bpo::positional_options_description posargs;
    posargs.add("archive-path", 1).add("command", 1).add("cmdargs", -1);

    bpo::variables_map vm;

    auto opts = bpo::command_line_parser(argc, argv)
                        .options(globalopts)
                        .positional(posargs)
                        .allow_unregistered()
                        .run();

    bpo::store(opts, vm);

    if (vm.count("command") && vm.count("archive-path"))
    {
        auto fs = vefs::os_filesystem();
        auto cprov = vefs::crypto::boringssl_aes_256_gcm_crypto_provider();

        auto cmd = vm["command"].as<std::string>();
        auto apath = vm["archive-path"].as<std::string>();
        std::array<std::byte, 32> prk{};
        if (cmd == "create")
        {
            auto ac = vefs::archive::open(
                    fs, apath, cprov, vefs::blob_view{prk},
                    vefs::file_open_mode::readwrite
                            | vefs::file_open_mode::create);
        }
    }
    return 0;
}

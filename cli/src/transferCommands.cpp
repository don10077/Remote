#include "transferCommands.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/transport/concordTransfer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>

namespace {
/// Пути argv уже переведены в UTF-8 главным модулем CLI.
std::filesystem::path path(const char* text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(text)); }
/// Печатает структурированную ошибку transport с параметрами.
int failure(const harmony::Error& error) {
    std::cerr << error.message;
    std::cerr << '\n';
    return 2;
}
}

int runTransferCommand(std::span<char*> args, const std::string& host) {
    const std::string_view command(args[1]);
    if (command == "models") {
        std::cout << "ID\tModel\tRead\tWrite file\tWrite tested\tPhoto/zones\n";
        for (const auto& model : harmony::remoteCatalog())
            std::cout << model.id << '\t' << model.name << '\t' << (model.readable ? "yes" : "no") << '\t'
                      << (model.writable ? "yes" : "no") << '\t' << (model.writeTested ? "yes" : "no") << '\t'
                      << (model.visualProfile.empty() ? "no" : "yes") << '\n';
        return 0;
    }
    const bool writing = command == "transfer-write";
    const bool reading = command == "transfer-read";
    if (args.size() < (writing || reading ? 4u : 3u)) {
        std::cerr << "Specify model ID and (for read/write) a configuration file. See --help.\n";
        return 1;
    }
    const auto* model = harmony::catalogModel(args[2]);
    if (!model) { std::cerr << "Unknown model; use harmony-cli models.\n"; return 1; }
    bool yes = false, withoutBackup = false;
    std::filesystem::path backup;
    for (std::size_t i = writing || reading ? 4u : 3u; i < args.size(); ++i) {
        const std::string_view option(args[i]);
        if (writing && option == "--yes") yes = true;
        else if (writing && option == "--without-backup") withoutBackup = true;
        else if (writing && option == "--backup" && i + 1 < args.size()) backup = path(args[++i]);
        else { std::cerr << "Unknown option: " << option << '\n'; return 1; }
    }
    harmony::formats::Bytes data;
    if (writing) {
        if (!yes || (model->readable && backup.empty()) || (!model->readable && !withoutBackup)) {
            std::cerr << "Writing requires --yes and --backup <new-file>; models without readback require --without-backup.\n";
            return 1;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(path(args[3]), ec);
        if (ec || size > 64 * 1024 * 1024) { std::cerr << "Cannot read input file or file too large.\n"; return 2; }
        std::ifstream input(path(args[3]), std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(input), {});
        if (data.size() != size) { std::cerr << "Incomplete input file.\n"; return 2; }
        auto file = harmony::transport::inspectTransferFile(data);
        if (!file) return failure(file.error());
    }
    harmony::transport::ConcordTransfer session;
    auto info = session.connect(model->id, {.host = host});
    if (!info) return failure(info.error());
    std::cout << info->remote.model << ": architecture=" << info->remote.identity.architecture
              << " skin=" << info->remote.identity.skin << " protocol=" << info->protocol << '\n';
    if (writing) {
        const auto result = session.write(data, backup, withoutBackup);
        if (!result) return failure(result.error());
        std::cout << "Configuration written.\n";
    } else if (reading) {
        auto result = session.read();
        if (!result) return failure(result.error());
        std::ofstream output(path(args[3]), std::ios::binary | std::ios::noreplace);
        if (!output) { std::cerr << "Cannot create output (existing files are not overwritten).\n"; return 2; }
        output.write(reinterpret_cast<const char*>(result->data()), static_cast<std::streamsize>(result->size()));
        output.close();
        if (!output) { std::cerr << "Could not finish output file.\n"; return 2; }
        std::cout << "Configuration saved (" << result->size() << " bytes).\n";
    }
    return 0;
}

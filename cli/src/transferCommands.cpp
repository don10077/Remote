#include "transferCommands.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/transport/concordTransfer.h"
#include <chrono>
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
        // Один файл для пересылки: контейнер с пульта и сведения о снятии (*.remote.json).
        const auto capture = harmony::transport::makeCapture(*info, std::move(*result));
        const auto json = harmony::formats::writeRemoteCapture(capture);
        if (!json) return failure(json.error());
        // Если указана папка, имя берётся по умолчанию: Harmony-<модель>-<дата>-<время>.remote.json.
        auto target = path(args[3]);
        std::error_code ec;
        if (std::filesystem::is_directory(target, ec)) {
            const auto stamp = std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now());
            const std::chrono::year_month_day day{std::chrono::floor<std::chrono::days>(stamp)};
            const std::chrono::hh_mm_ss time{stamp - std::chrono::floor<std::chrono::days>(stamp)};
            target /= std::filesystem::path(reinterpret_cast<const char8_t*>(harmony::formats::captureFileName(
                capture.modelId, static_cast<int>(day.year()),
                static_cast<int>(static_cast<unsigned>(day.month())),
                static_cast<int>(static_cast<unsigned>(day.day())), static_cast<int>(time.hours().count()),
                static_cast<int>(time.minutes().count())).c_str()));
        }
        std::ofstream output(target, std::ios::binary | std::ios::noreplace);
        if (!output) { std::cerr << "Cannot create output (existing files are not overwritten).\n"; return 2; }
        output.write(json->data(), static_cast<std::streamsize>(json->size()));
        output.close();
        if (!output) { std::cerr << "Could not finish output file.\n"; return 2; }
        std::cout << "File: " << target.string() << '\n';
        std::cout << "Configuration saved: model " << (capture.modelId.empty() ? "?" : capture.modelId) << ", "
                  << capture.data.size() << " bytes (" << harmony::formats::containerName(capture.container) << ").\n";
        // Проверка сохранённого: читаем файл с диска заново, как его прочтёт получатель.
        std::ifstream saved(target, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(saved), {}};
        const auto parsed = harmony::formats::parseRemoteCapture(text);
        if (!parsed) return failure(parsed.error());
        const auto check = harmony::transport::inspectCapture(*parsed);
        if (!check) return failure(check.error());
        std::cout << "Check passed: SHA-256, container checksum"
                  << (check->frame ? ", GSPM frame" : "") << ".\n";
    }
    return 0;
}

/**
 * @file cliFiles.cpp
 * @brief Запись файлов консольной утилиты.
 */

#include "cliFiles.h"

#include <fstream>
#include <string>
#include <system_error>

namespace harmony::cli {

std::filesystem::path freePath(const std::filesystem::path& wanted) {
    std::error_code error;
    if (!std::filesystem::exists(wanted, error) && !error) {
        return wanted;
    }
    const std::filesystem::path folder = wanted.parent_path();
    const std::filesystem::path stem = wanted.stem();
    const std::filesystem::path extension = wanted.extension();
    for (int number = 2; number < 1000; ++number) {
        std::filesystem::path candidate = folder / stem;
        candidate += "-" + std::to_string(number);
        candidate += extension;
        if (!std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

bool writeFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code error;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        file.close();
        if (file.fail()) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

} // namespace harmony::cli

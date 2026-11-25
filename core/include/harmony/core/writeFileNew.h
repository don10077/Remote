#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>

namespace harmony {
/// Creates a binary file exclusively and reports write and close failures.
inline bool writeFileNew(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    // std::ios::noreplace is unavailable in some MinGW runtime builds even when
    // the C++23 headers expose it. C11 exclusive creation works in UCRT as well.
#ifdef _WIN32
    auto* raw = _wfopen(path.c_str(), L"wbx");
#else
    auto* raw = std::fopen(path.c_str(), "wbx");
#endif
    if (!raw) return false;
    // glibc annotates fclose; GCC 13 warns if its attributed pointer type is
    // used as a template argument. A callable keeps the deleter portable.
    const auto closeFile = [](std::FILE* handle) { std::fclose(handle); };
    std::unique_ptr<std::FILE, decltype(closeFile)> file(raw, closeFile);
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size();
    const bool closed = std::fclose(file.release()) == 0;
    if (written && closed) return true;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return false;
}

} // namespace harmony

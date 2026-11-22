#pragma once

#include "formatFixtures.h"
#include "harmony/core/formats/zipArchive.h"

#include <string_view>

namespace harmony::test {

/**
 * @brief ZIP-конфигурация Harmony 900 с заданным XML и известными метаданными записей.
 * @param xml Содержимое UserConfiguration.xml.
 * @param identity Identity контейнера; по умолчанию 900 (15/61).
 * @return EZHex без зависимости от локальных дампов.
 */
inline Bytes zipEzHex(std::string_view xml = "<Root/>", const formats::RemoteIdentity identity = {}) {
    formats::ZipArchive archive;
    constexpr std::uint32_t time = 1'700'000'000;
    const auto add = [&](const std::string& name, std::uint32_t mode, std::string_view text) {
        archive.entries.push_back(formats::makeEntry(name, mode, Bytes(text.begin(), text.end()), time));
    };
    add("userconfig/", 040775, "");
    add(".preinstall", 0100775, "#!/bin/sh\necho fixture\n");
    add("userconfig/ActionLists.xml", 0100664, std::string(1024, ' ') + "<Root/>");
    const auto tables = codeTables();
    archive.entries.push_back(formats::makeEntry("userconfig/SsIr.bin", 0100664, tables.ssIr, time));
    archive.entries.push_back(formats::makeEntry("userconfig/IrProto.bin", 0100664, tables.irProto, time));
    add("userconfig/UserConfiguration.xml", 0100664, xml);
    formats::EzHex file;
    file.identity = identity;
    file.payload = formats::writeZip(archive);
    return *formats::writeEzHex(file);
}

} // namespace harmony::test

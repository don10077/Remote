#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace harmony {

/// Драйвер подключения пульта (`connection.driver` в remote.json).
namespace driver {
inline constexpr std::string_view kNone = "none";       ///< Без подключения
inline constexpr std::string_view kUsbNet = "usb-net";  ///< USB-сеть, адрес 169.254.1.2 (900, 1100)
inline constexpr std::string_view kUsbHid = "usb-hid";  ///< USB HID (One, 600–700)
inline constexpr std::string_view kHub = "hub";         ///< Через Harmony Hub по сети
inline constexpr std::array kAll = {kNone, kUsbNet, kUsbHid, kHub};
} // namespace driver

/// Формат конфигурации пульта (`connection.format` в remote.json).
namespace configFormat {
inline constexpr std::string_view kNone = "none";                     ///< Неизвестен
inline constexpr std::string_view kHarmonyXml = "harmony-xml";        ///< UserConfiguration.xml + IrProto.bin/SsIr.bin (900)
inline constexpr std::string_view kDeviceSequence = "device-sequence"; ///< 1100
inline constexpr std::string_view kGspm = "gspm";                     ///< One (architecture 12)
inline constexpr std::string_view kArch14 = "arch14";                 ///< 600–700 (architecture 14)
inline constexpr std::string_view kHubJson = "hub-json";              ///< Файлы ресурсов Harmony Hub
inline constexpr std::array kAll = {kNone, kHarmonyXml, kDeviceSequence, kGspm, kArch14, kHubJson};
} // namespace configFormat

/// @return true, если запись на пульт реализована для пары «подключение + формат».
[[nodiscard]] constexpr bool writableCombination(std::string_view driverName, std::string_view format) {
    return driverName == driver::kUsbNet && format == configFormat::kHarmonyXml;
}

/// @return true, если программа умеет читать пульт с таким подключением.
[[nodiscard]] constexpr bool readableConnection(std::string_view driverName) {
    return driverName == driver::kUsbNet || driverName == driver::kUsbHid;
}

/// Целевая модель проекта: способ подключения и формат конфигурации.
struct RemoteProfile {
    std::string id;
    std::string name;
    std::string image;
    bool writable = false;   ///< Запись реализована (writableCombination)
    std::string driver;      ///< connection.driver (driver::k…)
    std::string format;      ///< connection.format
};

inline const std::array bundledRemoteProfiles = {
#include "harmony/core/remoteProfiles.inc"
};

/// Каталог текущего сеанса; приложение заменяет его после проверки внешних ресурсов.
inline std::vector<RemoteProfile> remoteProfiles(bundledRemoteProfiles.begin(), bundledRemoteProfiles.end());

/// @return Профиль по id; nullptr — неизвестная модель. Указатель действует до обновления каталога.
inline const RemoteProfile* remoteProfile(std::string_view id) {
    for (const auto& profile : remoteProfiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

/// Ссылка на оформление модели; не хранит указатель, который может устареть после reload().
struct ProfileRef {
    std::string folder;  ///< remotes/<id>; пусто — модель не выбрана.
    bool loaded = false; ///< Точный id есть в текущем реестре профилей.
};

/// @brief Разрешает оформление по точному id, без подстановки папок других моделей.
/// @param modelId Id модели; пусто — модель не выбрана.
/// @return Папка оформления и признак его наличия в текущем реестре.
inline ProfileRef profileFor(std::string_view modelId) {
    if (modelId.empty()) return {};
    return {"remotes/" + std::string(modelId), remoteProfile(modelId) != nullptr};
}

/**
 * @brief Пульт по умолчанию: первый в каталоге.
 *
 * Порядок каталога задаёт сборка (cmake/RemoteProfiles.cmake ставит модель по умолчанию первой),
 * пользовательские пульты reload() добавляет после штатных.
 * @return Копия профиля: указатель в каталог устарел бы после его обновления.
 */
inline RemoteProfile defaultRemoteProfile() {
    return remoteProfiles.empty() ? bundledRemoteProfiles.front() : remoteProfiles.front();
}

/// @return id пульта по умолчанию (см. defaultRemoteProfile()).
inline std::string defaultRemoteId() { return defaultRemoteProfile().id; }

} // namespace harmony

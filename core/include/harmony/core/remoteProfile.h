#pragma once

#include <array>
#include <string>
#include <vector>
#include <string_view>

namespace harmony {

/// Целевая модель проекта и выбранные реализованные адаптеры.
struct RemoteProfile {
    std::string id;
    std::string name;
    std::string image;
    bool writable;
    std::string driver = writable ? "harmony900" : "hid";
    std::string configFormat = writable ? "harmony900" : "unsupported";
};

inline const std::array bundledRemoteProfiles = {
#include "harmony/core/remoteProfiles.inc"
};

/// Каталог текущего сеанса; приложение заменяет его после проверки внешних ресурсов.
inline std::vector<RemoteProfile> remoteProfiles(bundledRemoteProfiles.begin(), bundledRemoteProfiles.end());

/// @return true, если профиль использует сетевой адаптер Harmony 900.
inline bool usesHarmony900(std::string_view id) {
    for (const auto& profile : remoteProfiles) if (profile.id == id) return profile.driver == "harmony900";
    return false;
}

/// @return Профиль по id; nullptr — неизвестная модель. Указатель действует до обновления каталога.
inline const RemoteProfile* remoteProfile(std::string_view id) {
    for (const auto& profile : remoteProfiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

} // namespace harmony

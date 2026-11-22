#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace harmony {

/// Целевая модель проекта: оформление модели — id, имя и картинка.
/// Право записи решает `canWriteTo()` (W1/W2), профиль на запись не влияет.
struct RemoteProfile {
    std::string id;
    std::string name;
    std::string image;
    std::string driver;  ///< connection.driver: пусто — значение из каталога; задано — приоритет файла (W3)
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

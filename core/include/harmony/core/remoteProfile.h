#pragma once

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

/**
 * @brief Штатные модели сборки, в порядке списка моделей.
 *
 * Вектор, а не std::array: набор может быть пустым (U1) — пустую длину `std::array`
 * вывести через CTAD нельзя. Порядок задаёт генератор (`cmake/RemoteProfiles.cmake` —
 * сортировка по id), профиль 900 больше не ставится первым.
 */
inline const std::vector<RemoteProfile> bundledRemoteProfiles = {
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

} // namespace harmony

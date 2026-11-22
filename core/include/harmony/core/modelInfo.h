#pragma once

#include "harmony/core/remoteCatalog.h"

namespace harmony {

/// Сводка модели из каталога и текущего реестра оформлений; содержимого профиля здесь нет.
struct ModelInfo {
    std::string id;             ///< Пусто — модель не выбрана.
    std::string name;           ///< Каталог → профиль → id; пустое имя переводит UI.
    int architecture = 0;      ///< 0 — семейство неизвестно.
    LinkKind link = LinkKind::None; ///< Подключение только из каталога.
    bool known = false;        ///< Модель есть в каталоге.
    bool custom = false;       ///< Модели нет в каталоге, но есть загруженное оформление.
    bool hasProfile = false;   ///< profileFor(id).loaded в момент запроса.
};

/// @brief Собирает сведения о модели без зависимости от Qt и содержимого оформления.
/// @param modelId Точный id модели; пусто — модель не выбрана.
/// @return Сводка по текущему каталогу и реестру, без подстановки другой модели.
ModelInfo modelInfo(std::string_view modelId);

} // namespace harmony

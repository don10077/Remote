#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace harmony {

/// Модель физического пульта. Каталог транспорта не зависит от фотографий редактора.
struct RemoteModel {
    std::string_view id;
    std::string_view name;
    std::vector<int> skins;
    bool network;
    bool readable;
    bool writable;
    bool writeTested;
    std::string_view visualProfile;
};

/// @return Все известные модели, включая модели без визуальных ресурсов.
std::span<const RemoteModel> remoteCatalog();
/// @return Модель по стабильному id либо nullptr.
const RemoteModel* catalogModel(std::string_view id);

} // namespace harmony

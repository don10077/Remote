#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harmony {

/// Способ связи с пультом. Хранится в каталоге модели, а не в профиле оформления.
enum class LinkKind {
    None,   ///< связи нет (или модель неизвестна)
    UsbNet, ///< USB-сеть (900, 1000, 1100, Xbox 360)
    UsbHid, ///< USB HID (600–700, One и старые модели)
    Mh,     ///< MyHarmony по сети (Touch, Ultimate, 200–350, 800)
    Hub,    ///< сам хаб (Link, Home Hub, Ultimate Hub)
};

/// Сведения физического пульта. Каталог хранится в `remoteCatalog.json`, профиль
/// (`remote.json`) — только оформление; связь между ними — id модели.
struct RemoteModel {
    std::string_view id;
    std::string_view name;
    std::vector<int> skins;
    int architecture = 0; ///< Семейство прошивки; 0 — неизвестно.
    LinkKind link = LinkKind::None;
    bool readable = false;
    bool writable = false;
    bool writeTested = false;
};

/// @return Все записи каталога, включая модели без профиля.
std::span<const RemoteModel> remoteCatalog();
/// @return Модель по стабильному id или nullptr.
const RemoteModel* catalogModel(std::string_view id);
/// @return Модель по skin (идентификатор из файла или с пульта); nullptr — skin неизвестен.
const RemoteModel* catalogModelBySkin(int skin);
/// @return Способ связи модели; неизвестная модель — `LinkKind::None`. Профиль не влияет.
LinkKind modelLink(std::string_view modelId);
/// @return true, если модель подключается по USB-сети.
bool usesUsbNet(std::string_view modelId);
/// @return Нормализованный id: нижний регистр; `1100I` → `1100i`. Только для сравнения.
std::string normalizedModelId(std::string_view id);

} // namespace harmony

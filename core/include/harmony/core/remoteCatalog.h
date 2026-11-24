#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harmony {

/// Способ связи с пультом. Значение по умолчанию — в каталоге модели; пользователь может
/// задать свой способ в `connection.driver` файла remote.json, и заданное значение важнее
/// каталога (решено владельцем 22.11). Право записи при этом решает каталог (OD9).
enum class LinkKind {
    None,   ///< связи нет (или модель неизвестна)
    UsbNet, ///< USB-сеть (900, 1000, 1100, Xbox 360)
    UsbHid, ///< USB HID (600–700, One и старые модели)
    Mh,     ///< MyHarmony по сети (Touch, Ultimate, 200–350, 800)
    Hub,    ///< сам хаб (Link, Home Hub, Ultimate Hub)
};

/// Имена способов связи — они же значения `connection.driver` в remote.json.
inline constexpr std::array<std::string_view, 5> kLinkNames = {"none", "usb-net", "usb-hid", "mh", "hub"};

/// @return Способ связи по имени из remote.json; nullopt — имени нет в kLinkNames.
std::optional<LinkKind> linkFromName(std::string_view name);
/// @return Имя способа связи — то же, что пишется в `connection.driver`.
std::string_view linkName(LinkKind link);

/// Сведения физического пульта. Каталог хранится в `remoteCatalog.json`, профиль
/// (`remote.json`) — только оформление; связь между ними — id модели.
struct RemoteModel {
    std::string_view id;
    std::string_view name;
    std::vector<int> skins;
    int architecture = 0; ///< Семейство прошивки; 0 — неизвестно.
    LinkKind link = LinkKind::None;
    bool readable = false;
    bool rawRestore = false;       ///< Библиотека пересылает снятый образ как есть (восстановление); отредактированную конфигурацию — нет.
    bool configWriteTested = false; ///< Правка конфигурации и запись на пульт проверены на настоящем пульте; единственный источник для canWriteTo().
    bool supportsExtender = false;  ///< Может ли модель иметь модуль Z-Wave. Единственное место ответа: в коде нет ни списка моделей, ни слова «экстендер» (I7).
};

/// @return Все записи каталога, включая модели без профиля.
std::span<const RemoteModel> remoteCatalog();
/// @return Модель по стабильному id или nullptr.
const RemoteModel* catalogModel(std::string_view id);
/// @return Модель по skin (идентификатор из файла или с пульта); nullptr — skin неизвестен.
const RemoteModel* catalogModelBySkin(int skin);
/// @return Способ связи модели: поле `connection.driver` файла remote.json, если оно задано,
/// иначе — каталог; неизвестная модель без поля — `LinkKind::None`.
LinkKind modelLink(std::string_view modelId);
/// @return true, если модель подключается по USB-сети.
bool usesUsbNet(std::string_view modelId);
/// @return Нормализованный id: нижний регистр; `1100I` → `1100i`. Только для сравнения.
std::string normalizedModelId(std::string_view id);
/// @brief Выбирает имя модели для показа независимо от наличия оформления.
/// @param modelId Id модели; пустой id означает, что модель не выбрана.
/// @return Каталог → загруженный профиль → id как есть; для пустого id — пусто (переводит UI).
std::string modelDisplayName(std::string_view modelId);

} // namespace harmony

#include "harmony/core/remoteCatalog.h"

#include "harmony/core/remoteProfile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>

namespace harmony {

std::span<const RemoteModel> remoteCatalog() {
    static const std::array models = {
#include "harmony/core/remoteCatalog.inc"
    };
    return models;
}

const RemoteModel* catalogModel(std::string_view id) {
    static const RemoteModel automatic{.id = "auto",
                                       .name = "Automatic detection",
                                       .skins = {},
                                       .architecture = 0,
                                       .link = LinkKind::None,
                                       .readable = true,
                                       .rawRestore = true,
                                       .configWriteTested = false};
    if (id == "auto") return &automatic;
    for (const auto& model : remoteCatalog())
        if (model.id == id) return &model;
    return nullptr;
}

const RemoteModel* catalogModelBySkin(int skin) {
    for (const auto& model : remoteCatalog())
        if (std::ranges::find(model.skins, skin) != model.skins.end()) return &model;
    return nullptr;
}

std::optional<LinkKind> linkFromName(std::string_view name) {
    for (const auto& item : kLinkNames) {
        if (item != name) continue;
        if (item == "none") return LinkKind::None;
        if (item == "usb-net") return LinkKind::UsbNet;
        if (item == "usb-hid") return LinkKind::UsbHid;
        if (item == "mh") return LinkKind::Mh;
        return LinkKind::Hub;
    }
    return std::nullopt;
}

std::string_view linkName(LinkKind link) {
    switch (link) {
        case LinkKind::UsbNet: return "usb-net";
        case LinkKind::UsbHid: return "usb-hid";
        case LinkKind::Mh:     return "mh";
        case LinkKind::Hub:    return "hub";
        case LinkKind::None:   break;
    }
    return "none";
}

/**
 * @brief Способ связи модели: значение `connection.driver` профиля, если оно задано,
 * иначе — каталог (W3, решено владельцем 22.11).
 * @param modelId Id модели; пустой id — каталог (для `auto` он отвечает через catalogModel).
 * @return Способ связи; неизвестная модель без поля в файле — `LinkKind::None`.
 */
LinkKind modelLink(std::string_view modelId) {
    if (const auto* profile = remoteProfile(modelId)) {
        if (const auto kind = linkFromName(profile->driver)) return *kind;
    }
    const auto* model = catalogModel(modelId);
    return model != nullptr ? model->link : LinkKind::None;
}

bool usesUsbNet(std::string_view modelId) { return modelLink(modelId) == LinkKind::UsbNet; }

std::string normalizedModelId(std::string_view id) {
    std::string result(id);
    std::ranges::transform(result, result.begin(),
                           [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });
    return result;
}

std::string modelDisplayName(std::string_view modelId) {
    if (modelId.empty()) return {};
    if (const auto* model = catalogModel(modelId)) return std::string(model->name);
    if (const auto* profile = remoteProfile(modelId)) return profile->name;
    return std::string(modelId);
}

} // namespace harmony

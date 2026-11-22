#include "harmony/core/remoteCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>

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
                                       .writable = true,
                                       .writeTested = false};
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

LinkKind modelLink(std::string_view modelId) {
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

} // namespace harmony

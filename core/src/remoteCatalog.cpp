#include "harmony/core/remoteCatalog.h"

#include <array>
#include <algorithm>

namespace harmony {
std::span<const RemoteModel> remoteCatalog() {
    static const std::array models = {
#include "harmony/core/remoteCatalog.inc"
    };
    return models;
}

const RemoteModel* catalogModel(std::string_view id) {
    static const RemoteModel automatic{"auto", "Automatic detection", {}, false, true, true, false, {}};
    if (id == "auto") return &automatic;
    for (const auto& model : remoteCatalog()) if (model.id == id) return &model;
    return nullptr;
}
} // namespace harmony

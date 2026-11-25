#include "harmony/core/modelInfo.h"
#include "harmony/core/remoteProfile.h"

namespace harmony {

ModelInfo modelInfo(std::string_view modelId) {
    ModelInfo result;
    result.id = modelId;
    result.name = modelDisplayName(modelId);
    result.hasProfile = profileFor(modelId).loaded;
    if (const auto* model = catalogModel(modelId)) {
        result.architecture = model->architecture;
        result.known = true;
        result.supportsExtender = model->supportsExtender;
    }
    // Способ связи: значение файла приоритетнее каталога (W3).
    result.link = modelLink(modelId);
    result.custom = !result.known && result.hasProfile;
    return result;
}

} // namespace harmony

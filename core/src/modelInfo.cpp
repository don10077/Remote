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
        result.link = model->link;
        result.known = true;
    }
    result.custom = !result.known && result.hasProfile;
    return result;
}

} // namespace harmony

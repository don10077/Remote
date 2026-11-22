#include "harmony/core/writePolicy.h"

#include "harmony/core/modelInfo.h"
#include "harmony/core/remoteCatalog.h"

#include <algorithm>

namespace harmony {

// Правила применяются по порядку шага W1; решение одно для документа и для модели без
// документа, поэтому UI нигде не повторяет их сам.
WriteDecision canWriteTo(const ModelInfo& model, bool formatWrites, int skin) {
    if (!model.known) {
        return {WriteVerdict::UnknownModel, "no target model, or it is not in the catalog"};
    }
    if (!formatWrites) {
        return {WriteVerdict::WrongFormat, "this document format is not written to the remote"};
    }
    const auto* catalog = catalogModel(model.id);  // known — значит запись есть
    if (skin >= 0 && std::ranges::find(catalog->skins, skin) == catalog->skins.end()) {
        return {WriteVerdict::ModelMismatch, "the file identity does not belong to the target model"};
    }
    if (catalog->configWriteTested) {
        return {WriteVerdict::Allowed, "writing is confirmed on the real remote"};
    }
    return {WriteVerdict::Experimental, "the format is understood but writing is untested on this model"};
}

WriteDecision canWriteTo(const ConfigDocument& document) {
    return canWriteTo(document.targetModel(), document.backend()->canWriteToRemote(), document.identity().skin);
}

} // namespace harmony

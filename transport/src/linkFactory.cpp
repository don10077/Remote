/**
 * @file linkFactory.cpp
 * @brief Фабрика связи по модели (U4b): решает по ModelInfo, а не по факту «это 900».
 */

#include "harmony/transport/linkFactory.h"

#include "harmony/transport/usbNetLink.h"

namespace harmony::transport {

Result<LinkChoice> makeLink(const ModelInfo& model, const LinkTimings& timings) {
    // Модель не выбрана или её нет в справочнике: фабрика не угадывает модель, связь
    // выбирается по подключённому железу — это путь connect("auto") (Q11, решение 23.11.2026).
    if (model.id.empty())
        return makeError(ErrorCode::Unsupported,
                         "No model chosen: the connection is chosen by the connected hardware (auto)");
    if (!model.known)
        return makeError(ErrorCode::Unsupported,
                         "Model %1 is not in the catalog: the connection is chosen by the connected hardware (auto)",
                         {model.name});

    LinkChoice choice;
    choice.kind = model.link;
    switch (model.link) {
    case LinkKind::UsbNet:
        choice.link = std::make_unique<UsbNetLink>(ChannelFactory{}, timings);
        return choice;
    case LinkKind::UsbHid:
    case LinkKind::Mh:
        // Прежний путь ConcordTransfer: интерфейсы в этом шаге не объединяем (U4b).
        choice.transferPath = true;
        return choice;
    case LinkKind::Hub:
        return makeError(ErrorCode::Unsupported,
                         "Model %1 (%2): the hub configuration is read from its file, no connection is opened",
                         {model.name, std::string(linkName(model.link))});
    case LinkKind::None:
        return makeError(ErrorCode::Unsupported,
                         "Model %1 (%2): no connection is defined for this model",
                         {model.name, std::string(linkName(model.link))});
    }
    return makeError(ErrorCode::Unsupported, "Model %1: unknown connection", {model.name});
}

} // namespace harmony::transport

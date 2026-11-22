#pragma once

/**
 * @file linkFactory.h
 * @brief Фабрика связи: чем подключаться к модели (U4b).
 *
 * Решение принимает ModelInfo: связь по USB-сети — UsbNetLink; USB HID и MyHarmony — прежний
 * путь ConcordTransfer (интерфейсы в этом шаге не объединяем); хаб и модель без связи —
 * Unsupported. Когда модель не выбрана или её нет в справочнике, фабрика не угадывает: связь
 * выбирается по подключённому железу — путь `connect("auto")`.
 */

#include "harmony/core/error.h"
#include "harmony/core/modelInfo.h"
#include "harmony/transport/remoteLink.h"
#include "harmony/transport/usbNetLink.h"

#include <memory>

namespace harmony::transport {

/// Что фабрика решила для модели.
struct LinkChoice {
    LinkKind kind = LinkKind::None;   ///< способ связи: `connection.driver` файла, иначе справочник
    std::unique_ptr<RemoteLink> link;  ///< связь для LinkKind::UsbNet; пусто для остальных
    bool transferPath = false;         ///< usb-hid и mh — прежний путь ConcordTransfer
};

/**
 * @brief Связь для модели.
 * @param model Сводка модели (`modelInfo(id)`).
 * @param timings Тайминги связи (для короткой проверки подключения их задаёт вызывающий код).
 * @return Решение; ошибка Unsupported — хаб, модель без связи или модель вне справочника.
 */
[[nodiscard]] Result<LinkChoice> makeLink(const ModelInfo& model, const LinkTimings& timings = {});

} // namespace harmony::transport

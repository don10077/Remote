#pragma once

/**
 * @file documentEdit.h
 * @brief Правка документа конфигурации как шаг истории (отмена и повтор).
 *
 * Команда запоминает снимки документа до и после правки (ConfigDocument::Snapshot).
 * Это проще и надёжнее,
 * чем писать обратную операцию для каждой правки: отмена всегда возвращает
 * файлы ровно в прежнее состояние.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/error.h"
#include "harmony/core/undo/undoStack.h"

#include <functional>
#include <memory>
#include <string>

namespace harmony::undo {

/**
 * @brief Выполняет правку и возвращает её как команду для UndoStack.
 *
 * Правка применяется сразу; первый redo() команды (его вызывает UndoStack::push)
 * ничего не делает. При ошибке правки документ не меняется и команда не создаётся.
 * Если отмена или повтор не смогли вернуть снимок (ConfigDocument::restore()),
 * undo()/redo() команды бросают std::runtime_error, документ и история не меняются.
 *
 * Пример:
 * @code
 * auto command = undo::editDocument(document, "Переименовать «ТВ»",
 *     [&](ConfigDocument& d) { return d.setDeviceLabel(id, "Телевизор"); });
 * if (!command) { показать(command.error().message); return; }
 * stack.push(std::move(*command));
 * @endcode
 *
 * @param document Документ; должен жить дольше команды (дольше стека отмены).
 * @param text Описание шага для меню «Отменить …».
 * @param change Правка документа.
 * @return Команда или ошибка правки.
 */
[[nodiscard]] Result<std::unique_ptr<UndoCommand>> editDocument(
    ConfigDocument& document, std::string text,
    const std::function<Result<void>(ConfigDocument&)>& change);

} // namespace harmony::undo

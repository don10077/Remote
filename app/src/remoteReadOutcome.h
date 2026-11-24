#pragma once

/**
 * @file remoteReadOutcome.h
 * @brief I4: решение после чтения с пульта — открывать редактор или оставить файл.
 *
 * Решение о том, что может открыться в редакторе, живёт в ядре: вход `ConfigDocument::open()`
 * отвечает данными читателя таблицы (`configReaders()`) или `Unsupported`, если читателя нет.
 * Здесь соединяются два факта — ответ входа и галочка пользователя («Open the editor after
 * reading from the remote»), чтобы решение оставалось в одном месте (правило 8).
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/error.h"

namespace harmony::app {

/// Что делать с файлом, куда легло чтение с пульта.
enum class ReadOutcome {
    OpenInEditor,  ///< вход ответил документом и пользователь просил открыть
    KeepFile,      ///< галочка снята, или читателя таблицы нет — файл остаётся файлом
};

/**
 * @brief Решает, что делать с прочитанным.
 * @param opened Ответ входа `ConfigDocument::open()`: документ — читатель может открыть,
 *               ошибка — не может (текст ошибки называет файл и модель — правило 18).
 * @param openInEditor Галочка пользователя.
 * @return OpenInEditor — показывать редактор; KeepFile — сообщить, куда лёг файл.
 */
[[nodiscard]] ReadOutcome readOutcome(const Result<ConfigDocument>& opened, bool openInEditor);

} // namespace harmony::app

#pragma once

/**
 * @file assignmentGate.h
 * @brief Единственное место, где решается, можно ли менять кнопки документа.
 *
 * Правило (T4): список клавиш модели пуст — действия закрыты. Какой список передаётся,
 * гейт не выбирает: сейчас это `RemoteView::hardKeyIds()` (зоны `Key` профиля с валидным
 * фото), в A1 — `modelKeys()` ядра без проверки фото.
 *
 * Проверка «аппаратная клавиша в списке» остаётся в местах вызова, потому что они разные:
 * `editButton` пропускает пустое имя (клавишу ещё выбирается в окне), `moveButton` и
 * `assignCommand` — не пропускают. Свести их к одной функции нельзя без изменения
 * поведения. В A1 гейт становится `MainWindow::assignmentAllowed(const ButtonRef&)` по
 * правилу блока A.
 */

#include <QStringList>

namespace harmony::app {

/// @param keys Список клавиш модели (пусто — нет ни профиля, ни таблицы, ни конфигурации).
/// @return true — действия с кнопками открыты.
[[nodiscard]] bool assignmentAllowed(const QStringList& keys);

} // namespace harmony::app

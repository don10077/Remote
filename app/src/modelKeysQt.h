#pragma once

/**
 * @file modelKeysQt.h
 * @brief A1: клавиши модели для приложения — профиль без проверки фото и конфигурация документа.
 *
 * Ядро (`harmony::modelKeys()`, K4) видит реестр оформлений, но не их содержимое: имена зон
 * вида `Key` и поля `key` ячеек экранов собирает приложение из снимка ресурсов. Порядок — тот
 * же, что у ответа ядра (зоны по файлу, ячейки экранов, таблица по словарю), поэтому сортировка
 * списков не меняется.
 *
 * Проверка «размер фото совпал с `hardware.size`» здесь не делается: фото нужно только для
 * показа. Для пульта, который `reload()` принял, эта проверка выполняется сама собой —
 * `checkModel()` отклоняет профиль с несовпавшим размером, и такой пульт не попадает в снимок.
 *
 * Кеша здесь нет: функция вызывается из обновлений таблиц, гейтов и диалогов, а не из функций
 * отрисовки. Список зависит от конфигурации документа, поэтому ключ кеша должен учитывать правку
 * (id модели + `resources::revision()` + счётчик правок) — шаг K5 оставил список без кеша, пока
 * нет ответа, откуда брать счётчик (вопрос отчёта K5).
 */

#include "harmony/core/model/remoteConfig.h"
#include "harmony/core/modelKeys.h"

#include <QString>
#include <QStringList>

namespace harmony::app {

/**
 * @brief Клавиши модели: зоны `Key` профиля → ячейки экранов → таблица семейства → конфигурация.
 * @param modelId Точный id модели. Пустой id — модель не выбрана (Q3): без модели по умолчанию
 *        отвечают только конфигурация и словарь.
 * @param config Фактическая конфигурация документа; nullptr — без конфигурации.
 * @return Ответ ядра по правилам K4.
 */
[[nodiscard]] harmony::ModelKeys modelKeysFor(const QString& modelId,
                                              const harmony::model::RemoteConfig* config = nullptr);

/// Те же клавиши как список имён — для гейтов и проверок, в порядке `modelKeysFor()`.
[[nodiscard]] QStringList modelKeyIds(const QString& modelId,
                                      const harmony::model::RemoteConfig* config = nullptr);

/**
 * @brief Имена словаря для вида зон — список, который UI предлагает, когда модели отвечать нечем.
 * @param kind Вид зоны (`PhysicalKind::Key` — список команд зоны и подсказка `id`).
 * @return Имена словаря в порядке словаря (`keyNamesForKind`); пустой список — вида нет в словаре.
 */
[[nodiscard]] QStringList dictionaryIdsForKind(harmony::PhysicalKind kind);

/**
 * @brief Список для списков интерфейса: клавиши модели и, когда модели отвечать нечем, словарь.
 * @param modelId Точный id модели. Пустой id — модель не выбрана (Q3).
 * @param config Фактическая конфигурация документа; nullptr — без конфигурации.
 * @return `modelKeyIds()` и поверх словарь вида `Key`, если ядро сказал `vocabularyFallback` (K4).
 *         Решение «модели отвечать нечем» принимает ядро, а не «список пуст»: имена конфигурации
 *         остаются в списке, а словарь добавляется к ним.
 */
[[nodiscard]] QStringList uiKeyIds(const QString& modelId,
                                   const harmony::model::RemoteConfig* config = nullptr);

} // namespace harmony::app

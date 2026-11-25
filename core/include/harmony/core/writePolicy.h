#pragma once

#include "harmony/core/configDocument.h"
#include "harmony/core/modelInfo.h"

#include <string>

namespace harmony {

/// Решение о записи конфигурации на пульт его целевой модели.
enum class WriteVerdict {
    Allowed,       ///< Запись подтверждена на настоящем пульте (каталог: configWriteTested).
    Experimental,  ///< Формат понятен, но запись на модели не проверена — запись запрещена.
    WrongFormat,   ///< Документ в формате, который на пульт не пишется (хаб, бинарный импорт).
    ModelMismatch, ///< Skin файла не принадлежит целевой модели.
    UnknownModel,  ///< Модель не выбрана или её нет в каталоге.
};

/// Решение с обоснованием; reason без перевода — UI переводит по verdict.
struct WriteDecision {
    WriteVerdict verdict = WriteVerdict::UnknownModel;
    std::string reason;
};

/**
 * @brief Принадлежит ли skin файла целевой модели каталога.
 *
 * Одно решение для права записи и для проверки (C4): проверка сообщает о противоречии
 * identity и модели независимо от того, пишет ли этот формат на пульт.
 * @return false — модели нет в каталоге, skin неизвестен (< 0) или он не skin этой модели.
 */
bool skinBelongs(const ModelInfo& model, int skin);

/**
 * @brief Можно ли записать документ на пульт его целевой модели.
 *
 * Единственный источник права записи: каталог (configWriteTested) + формат документа +
 * identity. Профиль оформления (profile.json) не влияет (W3). Правила в порядке:
 * неизвестная модель → формат не пишется на пульт → skin не принадлежит модели →
 * модель не подтверждена записью.
 * @param document Открытый документ конфигурации.
 * @return Решение и обоснование; запись разрешена только при verdict == Allowed.
 */
WriteDecision canWriteTo(const ConfigDocument& document);

/**
 * @brief То же решение для модели, когда документа ещё нет (выбор целевой модели в диалоге).
 *
 * Правила про формат и skin применяются только тогда, когда они известны: без документа
 * формат берётся как «пишется» и skin как «неизвестен». Решение остаётся в одном месте —
 * эта функция вызывает общую, а UI не спрашивает `ConfigBackend::canWriteToRemote()` напрямую.
 * @param model Сводка модели (`modelInfo(id)`).
 * @param formatWrites Формат документа пишет конфигурацию на пульт; по умолчанию — да.
 * @param skin identity документа; < 0 — неизвестен, правило про skin пропускается.
 * @return Решение и обоснование.
 */
WriteDecision canWriteTo(const ModelInfo& model, bool formatWrites = true, int skin = -1);

} // namespace harmony

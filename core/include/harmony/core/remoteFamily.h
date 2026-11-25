#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "harmony/core/formats/container.h"

namespace harmony {

/**
 * @brief Геометрия экрана семейства: размеры LCD и границы, по которым читается текст подписей.
 *
 * Нули — экрана нет или он неизвестен. Для 900 и 1000 одна архитектура, но разные
 * размеры, поэтому геометрия модели решается отдельно, не на уровне семейства.
 */
struct ScreenGeometry {
    int width = 0;             ///< пиксели LCD; 0 — экрана нет или он неизвестен
    int height = 0;            ///< пиксели LCD; 0 — экрана нет или он неизвестен
    int headerBottom = 0;      ///< y ниже заголовка
    int footerTop = 0;         ///< y начала нижней строки подсказки
    int softQuarterX = 0;      ///< левая/правая граница группировки подписей
    int softQuarterY = 0;      ///< верхняя/нижняя граница группировки подписей
    bool monochrome = false;   ///< монохромный экран
};

/**
 * @brief Форма ответа на чтение файла по файловому протоколу.
 *
 * Два файла на одном пульте — две формы: `/sys/sysinfo` отвечает строками «имя значение»,
 * `/rf/deviceinfo` — ответом на запрос в виде JSON. Форму нельзя переносить с файла на файл.
 * `Unknown` означает, что форма файла не подтверждена: читатель её не предполагает.
 */
enum class FileShape { TextLines, JsonObject, BinaryContainer, Unknown };

/**
 * @brief Файл, который семейство подтверждает по файловому протоколу.
 *
 * `confirmedOn` — модели каталога, на которых ответ подтверждён readings. Пустой список —
 * подтверждения нет. Строка ничего не обещает моделям, которых в списке нет: путь не считается
 * существующим, пока пульт на него не ответил.
 */
struct AnswerableFile {
    std::string_view path;                         ///< путь, который пульт открывает
    FileShape shape;                               ///< чем именно он отвечает
    std::span<const std::string_view> confirmedOn; ///< модели, где ответ подтверждён
};

/**
 * @brief Соответствие логического номера секции фактическому слоту контейнера.
 *
 * Пустая таблица означает прямое соответствие номера и слота. Если таблица заполнена,
 * ненайденный номер не читается, как это делала ветка arch 10.
 */
struct SectionSlot {
    unsigned base = 0;
    unsigned raw = 0;
};

/**
 * @brief Описание семейства архитектуры.
 *
 * S1 содержит ключ, название, рамку контейнера и экран. S2 добавляет поля по каждой
 * реальной ветке разбора: текст, глифы, программа рисования, секции и сдвиг страниц.
 */
struct RemoteFamily {
    int architecture = 0;                            ///< ключ
    std::string_view title;                          ///< название семейства
    std::optional<formats::ContainerFamily> container; ///< рамка контейнера, если есть

    // --- S2: особенности разбора образа, по одной на каждую реальную ветку кода ---
    bool wideTextCodes = true;                       ///< код символа >127 продолжается вторым байтом
    bool monoGlyphs = false;                         ///< монохромные глифы
    bool nestedDrawingFollows0x16 = true;            ///< код 0x16 читает вложенную программу рисования
    unsigned commandIndexShift = 0;                  ///< сдвиг номера команды
    int sectionFirstSplit = -1;                      ///< после этого логического номера слот сдвигается
    int sectionSecondSplit = -1;                     ///< второй сдвиг слотов
    std::span<const SectionSlot> sectionSlots;       ///< точные слоты, если прямой номер не подходит

    ScreenGeometry screen;

    /// Файлы, которые семейство подтверждает по файловому протоколу (I9). Пустой список —
    /// подтверждений нет, и читатель ничего не отправляет.
    std::span<const AnswerableFile> answerableFiles;

    /// Имена значков экранных кнопок, которые знает семейство (U6). Пустой список — имён нет:
    /// семейство ничего не утверждает, и проверка не пишет предупреждение про значок.
    std::span<const std::string_view> buttonIcons;

    /// @return фактический слот секции по логическому номеру; nullopt — секции нет.
    [[nodiscard]] std::optional<unsigned> sectionSlot(unsigned base) const {
        if (!sectionSlots.empty()) {
            for (const auto& slot : sectionSlots) {
                if (slot.base == base) {
                    return slot.raw;
                }
            }
            return std::nullopt;
        }
        unsigned raw = base;
        if (sectionFirstSplit >= 0 && base >= static_cast<unsigned>(sectionFirstSplit)) {
            ++raw;
        }
        if (sectionSecondSplit >= 0 && base >= static_cast<unsigned>(sectionSecondSplit)) {
            ++raw;
        }
        return raw;
    }
};

/// @return описание семейства по архитектуре; nullptr — семейство не описано.
[[nodiscard]] const RemoteFamily* remoteFamily(int architecture);
/// @return строка файла по пути для архитектуры; nullptr — такой строки в семействе нет.
[[nodiscard]] const AnswerableFile* answerableFile(int architecture, std::string_view path);
/// @return true, если ответ на этот путь подтверждён именно на этой модели семейства.
[[nodiscard]] bool confirmedOn(const AnswerableFile& entry, std::string_view modelId);

} // namespace harmony

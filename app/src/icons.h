#pragma once

/**
 * @file icons.h
 * @brief Значки интерфейса, нарисованные программно.
 *
 * Значки строятся из векторных контуров при запросе, поэтому одинаково чёткие
 * на любом масштабе экрана и не требуют файлов или модуля QtSvg.
 */

#include <QColor>
#include <QIcon>

#include <string>

namespace harmony::app::icons {

/// Набор значков приложения.
enum class Icon {
    Undo,        ///< Отменить
    Redo,        ///< Повторить
    ChevronDown, ///< Стрелка выпадающего меню
    Upload,      ///< Записать на пульт
    Download,    ///< Прочитать с пульта
    Folder,      ///< Открыть файл
    Remote,      ///< Пульт
    Play,        ///< Посмотреть пример
    Dot,         ///< Цветная точка состояния
    Plus,        ///< Создать
    Save,        ///< Сохранить
    // Типы устройств (плитки боковой панели)
    Television,  ///< Телевизор, монитор, проектор
    Speaker,     ///< Ресивер, усилитель
    Disc,        ///< Плеер дисков
    Gamepad,     ///< Игровая приставка
    SetTopBox,   ///< ТВ-приставка, спутник, кабель, PVR
    Computer,    ///< Компьютер, медиацентр
};

/**
 * @brief Создаёт значок.
 * @param icon Какой значок.
 * @param color Цвет линий; недействительный цвет — основной цвет текста темы.
 * @return Значок с вариантами для обычного и неактивного состояния.
 */
QIcon make(Icon icon, const QColor& color = QColor());

/**
 * @brief Значок по типу устройства из конфигурации.
 * @param type Тип («Television», «Receiver», «DvdCd», ...).
 * @param color Цвет линий; недействительный — цвет текста темы.
 * @return Значок; для неизвестных типов — пульт.
 */
QIcon deviceType(const std::string& type, const QColor& color = QColor());

} // namespace harmony::app::icons

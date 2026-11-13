#pragma once

/**
 * @file quickSearch.h
 * @brief Быстрый поиск (Ctrl+K): устройства, активности, кнопки и команды по нескольким буквам.
 *
 * Пишете «громк» или «hdmi» — список сразу показывает подходящее: устройства
 * и активности первыми, затем кнопки и команды. Стрелки — по списку, Enter —
 * открыть найденное в главном окне.
 */

#include "harmony/core/model/remoteConfig.h"

#include <QDialog>
#include <QString>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QTreeWidget;

namespace harmony::app {

/// Окно быстрого поиска.
class QuickSearch final : public QDialog {
    Q_OBJECT

public:
    /// Что найдено.
    struct Hit {
        enum class Kind {
            Device,
            Activity,
            DeviceKey,    ///< Аппаратная кнопка устройства
            ActivityKey,  ///< Аппаратная кнопка активности
            DeviceSoft,   ///< Экранная кнопка устройства
            ActivitySoft, ///< Экранная кнопка активности
            Command,      ///< Команда устройства
        };
        Kind kind = Kind::Device;
        std::string id;      ///< Устройство или активность
        std::string key;     ///< Аппаратная кнопка
        int position = -1;   ///< Место экранной кнопки
        std::string command; ///< Команда (для Kind::Command)
        QString title;       ///< Первая колонка
        QString where;       ///< Вторая колонка: «Кнопка · ТВ»
        QString haystack; ///< Где ищется: всё вместе в нижнем регистре
    };

    /**
     * @brief Создаёт окно по конфигурации.
     * @param config Конфигурация (живёт дольше окна).
     * @param parent Родительское окно.
     */
    explicit QuickSearch(const model::RemoteConfig& config, QWidget* parent = nullptr);

    /// @return Выбранное (окно закрыто с Accepted).
    [[nodiscard]] const std::optional<Hit>& chosen() const { return chosen_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Собирает всё, что можно найти.
    void collect(const model::RemoteConfig& config);

    /// Заполняет список по тексту поиска.
    void update();

    /// Выбирает текущую строку и закрывает окно.
    void activateCurrent();

    std::vector<Hit> all_;
    std::vector<std::size_t> shown_; ///< Номера в all_ по строкам списка
    std::optional<Hit> chosen_;

    QLineEdit* edit_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
};

} // namespace harmony::app

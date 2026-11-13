#pragma once

/**
 * @file propertiesDialog.h
 * @brief Окно «Свойства» устройства или активности: известные свойства с пояснениями,
 *        неизвестные — под своим именем, как текст.
 *
 * У устройства — две части: паузы между нажатиями (<Commands><Properties>) и свойства
 * самого устройства; у активности — её свойства. «Не задано» убирает свойство из файла.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/propertyCatalog.h"

#include <QDialog>

#include <vector>

class QTableWidget;
class QWidget;

namespace harmony::app {

class PropertiesDialog final : public QDialog {
    Q_OBJECT

public:
    /// Изменённое значение (пусто — убрать свойство).
    struct Change {
        PropertyScope scope;
        std::string name;
        std::string value;
    };

    /**
     * @param document Открытая конфигурация.
     * @param activity true — активность, false — устройство.
     * @param id Устройство или активность.
     * @param title Имя для заголовка.
     */
    PropertiesDialog(const ConfigDocument& document, bool activity, const std::string& id, const QString& title,
                     QWidget* parent = nullptr);

    /// @return Что изменилось по сравнению с файлом.
    [[nodiscard]] std::vector<Change> changes() const;

private:
    struct Row {
        PropertyScope scope;
        std::string name;
        std::string original; ///< Значение в файле; пусто — не задано
        const PropertyInfo* info = nullptr;
        QWidget* editor = nullptr;
    };

    void addSection(QTableWidget* table, const ConfigDocument& document, PropertyScope scope, const std::string& id);
    [[nodiscard]] static std::string valueOf(const Row& row);

    std::vector<Row> rows_;
};

} // namespace harmony::app

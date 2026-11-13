#pragma once

/**
 * @file versionsDialog.h
 * @brief Окно «Версии»: прежние состояния конфигурации и восстановление.
 *
 * Слева — версии (новые сверху): когда и почему сохранена. Справа — что
 * изменится, если восстановить выбранную (отличия от открытой сейчас).
 * «Восстановить» заменяет содержимое открытой конфигурации версией — это
 * обычная правка, её можно отменить; текущее состояние перед этим тоже
 * сохраняется версией.
 */

#include "../versionStore.h"
#include "harmony/core/configDocument.h"

#include <QDialog>
#include <optional>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace harmony::app {

/// Версии конфигурации.
class VersionsDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно.
     * @param document Открытая конфигурация (живёт дольше окна).
     * @param parent Родительское окно.
     */
    VersionsDialog(const ConfigDocument& document, QWidget* parent = nullptr);

    /// @return Версия, которую решили восстановить (окно закрыто с Accepted).
    [[nodiscard]] const std::optional<versions::Version>& chosen() const { return chosen_; }

private:
    /// Заполняет список версий.
    void fill();

    /// Показывает отличия выбранной версии.
    void showSelected();

    const ConfigDocument& document_;
    QString key_;
    QList<versions::Version> versions_;
    std::optional<versions::Version> chosen_;

    QTreeWidget* list_ = nullptr;
    QTreeWidget* changes_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* restore_ = nullptr;
};

} // namespace harmony::app

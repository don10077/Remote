#pragma once

/**
 * @file activityDialog.h
 * @brief Окно активности: новая или правка состава — устройства, роли, входы.
 *
 * У новой активности кнопки пульта назначаются сами по ролям; при правке это
 * можно сделать заново по желанию.
 */

#include "harmony/core/activityDraft.h"

#include <QDialog>
#include <QStringList>

#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace harmony::app {

/// Сборка новой активности или правка состава существующей.
class ActivityDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно.
     * @param config Конфигурация (в ней должно быть хотя бы одно устройство).
     * @param existing Активность для правки; nullptr — новая.
     * @param parent Родительское окно.
     */
    explicit ActivityDialog(const model::RemoteConfig& config, const model::Activity* existing = nullptr,
                            QWidget* parent = nullptr);

    /// @return Черновик активности по введённым данным.
    [[nodiscard]] ActivityDraft draft() const;

    /// @return Правка: назначить аппаратные кнопки заново по ролям.
    [[nodiscard]] bool reassignButtons() const;

    /// Закрывает окно; если из активности убрали устройства — сначала спрашивает.
    void accept() override;

private:
    /// @return Имена устройств, которые были в активности и не отмечены в черновике.
    [[nodiscard]] QStringList removedDevices(const ActivityDraft& draft) const;

    /// Заполняет поля составом существующей активности.
    void prefill(const ActivityDraft& draft);

    /// Пересобирает выбор ролей и входов под отмеченные устройства.
    void devicesChanged();

    /// Обновляет сводку о назначенных кнопках и доступность «Создать».
    void updatePreview();

    /// @return Отмеченные устройства (id) в порядке списка.
    [[nodiscard]] std::vector<std::string> checkedDevices() const;

    /**
     * @brief Заполняет список ролей отмеченными устройствами.
     * @param combo Список.
     * @param ids Отмеченные устройства.
     * @param preferred Кого выбрать, если прежний выбор больше не отмечен.
     * @param allowNone Первым пунктом — «нет».
     */
    void fillRole(QComboBox* combo, const std::vector<std::string>& ids, const std::string& preferred,
                  bool allowNone) const;

    const model::RemoteConfig& config_;
    const model::Activity* existing_ = nullptr; ///< Правка; nullptr — новая активность
    bool nameEdited_ = false; ///< Имя ввёл пользователь — больше не подставляем по типу

    QLineEdit* name_ = nullptr;
    QLabel* nameProblem_ = nullptr; ///< «Такое имя уже есть»
    QComboBox* type_ = nullptr;
    QListWidget* devices_ = nullptr;
    QComboBox* control_ = nullptr;
    QComboBox* volume_ = nullptr;
    QComboBox* display_ = nullptr;
    QFormLayout* inputsForm_ = nullptr;
    QLabel* inputsHint_ = nullptr;
    std::map<std::string, QComboBox*> inputs_; ///< Устройство → выбор входа
    QCheckBox* reassign_ = nullptr; ///< Только при правке
    QLabel* preview_ = nullptr;
    QPushButton* create_ = nullptr;
};

} // namespace harmony::app

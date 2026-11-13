#pragma once

/**
 * @file remoteSettingsDialog.h
 * @brief Окно «Настройки пульта»: звук, подсветка, шрифт, тема, часы, блокировка.
 *
 * Значения можно взять из открытой конфигурации или с подключённого пульта,
 * а применить — к конфигурации (уйдут на пульт при записи, с отменой) или сразу
 * к подключённому пульту (без перезапуска и без записи конфигурации).
 */

#include "harmony/core/configDocument.h"

#include <QDialog>
#include <QMap>

#include <functional>

class QLabel;
class QWidget;

namespace harmony::app {

class RemoteSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param document Открытая конфигурация; nullptr — только подключённый пульт.
     * @param applyToDocument Применяет значения к конфигурации шагом отмены (ключ → значение;
     *        пусто — пульт оставит своё). Возвращает true при успехе.
     */
    RemoteSettingsDialog(const ConfigDocument* document,
                         std::function<bool(const QMap<QString, QString>&)> applyToDocument,
                         QWidget* parent = nullptr);

private:
    /// Значения полей: ключ → значение; пусто — «как на пульте».
    [[nodiscard]] QMap<QString, QString> values() const;
    void setValues(const QMap<QString, QString>& values);

    void readFromRemote();
    void writeToRemote();
    void saveToDocument();

    const ConfigDocument* document_;
    std::function<bool(const QMap<QString, QString>&)> applyToDocument_;
    QMap<QString, QWidget*> fields_; ///< QComboBox или QSpinBox по ключу настройки
    QLabel* status_ = nullptr;
};

} // namespace harmony::app

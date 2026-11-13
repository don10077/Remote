#pragma once

class QComboBox;

/**
 * @file remotePanel.h
 * @brief Панель справа: фото пульта с живым экраном и легенда.
 *
 * Берёт данные из модели конфигурации и передаёт их в RemoteView:
 *  - для устройства — какие кнопки назначены, свободны или ссылаются на несуществующую команду;
 *  - для активности — каким цветом какое устройство управляется;
 *  - на экран — список активностей или экранные кнопки выбранного объекта.
 */

#include "harmony/core/model/remoteConfig.h"

#include <QColor>
#include <QList>
#include <QPair>
#include <QWidget>
#include <vector>

class QLabel;
class QVBoxLayout;

namespace harmony::app {

class RemoteView;

/**
 * @brief Панель пульта.
 *
 * Показывает кнопки выбранного объекта; правка — перетаскиванием кнопок и команд
 * на картинке (сигналы ниже), сама правка документа — в главном окне.
 */
class RemotePanel final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Создаёт панель.
     * @param parent Родительский виджет.
     */
    explicit RemotePanel(QWidget* parent = nullptr);

    /**
     * @brief Задаёт конфигурацию и показывает обзор (список активностей).
     * @param config Модель; nullptr — пустой пульт. Должна жить, пока показана.
     */
    void setConfig(const model::RemoteConfig* config);

    /// Показывает устройство с индексом @p index в списке устройств.
    void showDevice(int index);

    /// Показывает активность с индексом @p index в списке активностей.
    void showActivity(int index);

    /// Показывает обзор: экран «Мои активности», кнопки без раскраски.
    void showOverview();

    /// Подсвечивает аппаратную кнопку (пустая строка — снять выделение).
    void selectHardKey(const QString& key);
    /// Подсветить назначения команды в текущем устройстве или активности.
    void selectCommand(const std::string& deviceId, const std::string& command);

    /// Подсвечивает экранную кнопку по позиции (-1 — снять выделение).
    void selectSoftButton(int position);

    /// Текущий экран пульта: id раскладки из profile.json (например, «activities»).
    QString screenMode() const;

    /// Вернуть экран пульта после пересборки панели (пустой — экран по умолчанию).
    void setScreenMode(const QString& mode);

    /**
     * @brief Цвет устройства: тот же на пульте, в легенде и на плитке боковой панели.
     * @param index Индекс устройства в конфигурации; < 0 — серый.
     */
    static QColor deviceColour(int index);

signals:
    void hardKeyClicked(const QString& key); ///< Щелчок по аппаратной кнопке
    void softButtonClicked(int position);    ///< Щелчок по экранной кнопке
    /// Аппаратную кнопку перетащили на другую (copy — с Ctrl: копировать, а не переносить).
    void hardKeyDropped(const QString& from, const QString& to, bool copy);
    /// Экранную кнопку перетащили на другое место.
    void softButtonDropped(int from, int to);
    /// Команду устройства перетащили на аппаратную кнопку.
    void commandDropped(const QString& key, const QString& deviceId, const QString& command);
    /// Команду устройства перетащили на место экранной кнопки.
    void commandDroppedOnSoft(int position, const QString& deviceId, const QString& command);
    void deviceClicked(int index); ///< Выбор устройства в списке на экране пульта
    void activityClicked(int index);   ///< Щелчок по активности на экране пульта
    void message(const QString& text); ///< Пояснение для строки состояния

private:
    enum class Focus { Overview, Device, Activity };

    void createView();
    void refresh();
    void refreshActivityList();
    void refreshDevice();
    void refreshActivity();
    void rebuildLegend();
    void updateScreenButtons();

    /// Индекс устройства по id из действия кнопки («<id>_<команда>_<модификатор>»), -1 — нет.
    [[nodiscard]] int deviceIndexOf(const std::string& actionId) const;

    const model::RemoteConfig* config_ = nullptr;
    Focus focus_ = Focus::Overview;
    int index_ = -1; ///< Индекс показанного устройства или активности

    RemoteView* remote_ = nullptr;
    QWidget* interactiveRemote_ = nullptr;
    QWidget* remoteControls_ = nullptr;
    QLabel* remoteHint_ = nullptr;
    QLabel* context_ = nullptr;
    QComboBox* screenModes_ = nullptr;

    QWidget* legend_ = nullptr;
    QVBoxLayout* legendLayout_ = nullptr;
    QList<QPair<QString, QColor>> activityDevices_; ///< Устройства активности для легенды
    std::vector<int> deviceOrder_; ///< Индексы отправляемых устройств
    std::vector<int> activityOrder_; ///< Индексы активностей конфигурации в порядке экрана пульта
};

} // namespace harmony::app

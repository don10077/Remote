#pragma once

#include <QByteArray>
#include <QString>
#include <functional>
class QWidget;

namespace harmony::app::windowsUsb {
/// Состояние USBLAN Harmony 900/1000/1100 независимо от выбранного профиля и TCP.
enum class State { Absent, DriverMissing, Disabled, NoAddress, Ready, DeviceError, Multiple, Unknown };
struct Status {
    State state = State::Unknown;
    QString deviceId;
    QString adapter;
    QString address;
    QString detail;
};
/// @return Windows поддерживает диагностику и восстановление USBLAN.
bool available();
/// @param json Ответ штатного диагностического сценария.
/// @return Разобранное состояние; некорректный ответ даёт Unknown.
Status parseStatus(const QByteArray& json);
/// @brief Читает PnP и параметры адаптера. Вызывать в рабочем потоке: ожидание до 8 секунд.
Status inspect();
/// @param status Полученное состояние оборудования.
/// @return Сообщение для интерфейса и журнала, включая имя адаптера и IP при наличии.
QString describe(const Status& status);
/// @brief Включает/перезапускает только адаптер Harmony после UAC, затем вызывает done.
/// @param parent Владелец сообщений и фонового наблюдения процесса.
/// @param done Повторная проверка связи после окончания восстановления.
void recover(QWidget* parent, std::function<void()> done);
} // namespace harmony::app::windowsUsb

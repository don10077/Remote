#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QThread;

namespace harmony::app {
/// Снятие конфигурации с любого пульта в файл: USB (*.remote.json) и Hub по сети (*.hub.json).
/// Записи здесь нет: окно только читает и сохраняет, пульт не изменяется.
class ConfigTransferDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ConfigTransferDialog(QWidget* parent = nullptr);
    ~ConfigTransferDialog() override;
    /// Не закрывает окно во время чтения.
    void reject() override;
    /// @return Файл *.hub.json, прочитанный с хаба (окно при этом закрывается); пусто — хаб не читали.
    [[nodiscard]] QString hubFile() const { return hubFile_; }
private:
    /// Обновляет доступные операции по выбранной модели.
    void refresh();
    /// Спрашивает, куда сохранить, и снимает конфигурацию по USB в отдельном потоке.
    void readToFile();
    /// Спрашивает адрес Harmony Hub и читает его конфигурацию по сети в файл *.hub.json.
    void readHub();
    /// Блокирует или разблокирует элементы окна на время фоновой операции.
    void setBusy(bool busy);
    QComboBox* models_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* read_ = nullptr;
    QPushButton* hub_ = nullptr;
    QPushButton* close_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QThread* worker_ = nullptr;
    QString hubFile_; ///< Сохранённая конфигурация хаба после успешного чтения
};
} // namespace harmony::app

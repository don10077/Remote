#pragma once

#include <QDialog>
#include <QString>

class QComboBox;

namespace harmony::app {

/// Выбор целевой модели, фотография и доступность записи.
class RemoteProfileDialog final : public QDialog {
public:
    explicit RemoteProfileDialog(const QString& current, QWidget* parent = nullptr);
    [[nodiscard]] QString selectedRemote() const;
private:
    QComboBox* models_ = nullptr;
};

} // namespace harmony::app

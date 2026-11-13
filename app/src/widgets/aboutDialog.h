#pragma once

/**
 * @file aboutDialog.h
 * @brief Окно «О программе»: версия, лицензия, ссылки, поддержка проекта, сторонние компоненты.
 */

#include <QDialog>

namespace harmony::app {

/// Окно «О программе».
class AboutDialog final : public QDialog {
    Q_OBJECT

public:
    /// Страница пожертвований на развитие проекта (DonationAlerts).
    static constexpr const char* kDonateUrl = "https://www.donationalerts.com/r/alexshkurko";
    /// QR-код страницы пожертвований; есть в ресурсах, только если файл лежит в исходниках.
    static constexpr const char* kDonateQr = ":/res/app/donate-qr.png";

    explicit AboutDialog(QWidget* parent = nullptr);

    /**
     * @return Версия программы, Qt и системы одной строкой — для сообщения об ошибке.
     */
    [[nodiscard]] static QString versionInfo();
};

} // namespace harmony::app

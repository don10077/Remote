/**
 * @file theme.cpp
 * @brief Палитра приложения и загрузка таблицы стилей.
 */

#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QString>
#include <QStyle>
#include <QStyleHints>

#include "appSettings.h"

#include <algorithm>
#include <array>
#include <string>

namespace harmony::app::theme {

namespace {

/**
 * @brief Палитра: имя токена в QSS и значение.
 *
 * Принципы: светлая боковая панель, белая рабочая область, тёмный текст,
 * один синий акцент. Второстепенный текст достаточно тёмный, чтобы читаться
 * на фоне панели (контраст не ниже 7:1).
 */
struct Entry {
    Color role;
    const char* token;
    const char* value; ///< Светлая тема
    const char* dark;  ///< Тёмная тема
};

constexpr std::array kPalette = {
    // Значения совпадают с modern.qss: те же панели, текст и акцент.
    Entry{Color::Window, "window", "#e8ecf1", "#181c22"},
    Entry{Color::Surface, "surface", "#ffffff", "#1e232a"},
    Entry{Color::SurfaceAlt, "surfaceAlt", "#f6f8fb", "#232931"},
    Entry{Color::Border, "border", "#d3d9e0", "#323942"},
    Entry{Color::Text, "text", "#161b22", "#e6eaf0"},
    Entry{Color::TextMuted, "textMuted", "#4d5866", "#a3adba"},
    Entry{Color::Accent, "accent", "#2563eb", "#4b8bf5"},
    Entry{Color::AccentHover, "accentHover", "#1d4ed8", "#6aa1ff"},
    Entry{Color::AccentSoft, "accentSoft", "#e6eefd", "#1f3b63"},
    Entry{Color::Success, "success", "#1f7a4d", "#5fcf8f"},
    Entry{Color::Warning, "warning", "#b45309", "#f2b43c"},
    Entry{Color::Danger, "danger", "#b42318", "#f87171"},
    // Тёмная кнопка на тёмной панели не видна — в тёмной теме светлее; подсветка — как на картинке.
    Entry{Color::RemoteKey, "remoteKey", "#1a1c1f", "#3d444e"},
    Entry{Color::RemoteKeyFree, "remoteKeyFree", "#7d8187", "#7d8187"},
    Entry{Color::RemoteKeyFreeLine, "remoteKeyFreeLine", "#3a4149", "#a3adba"},
    Entry{Color::RemoteBroken, "remoteBroken", "#ff5a52", "#ff5a52"},
    Entry{Color::RemoteSelected, "remoteSelected", "#8ab8ff", "#8ab8ff"},
};

bool dark = false; ///< Тёмная тема применена

/**
 * @brief Цвета modern.qss в тёмной теме: свойства текста (color) и заливки/рамки — отдельно.
 *
 * Один и тот же цвет в светлой теме бывает и текстом, и фоном: белый — фон
 * панелей и текст на синей кнопке, тёмно-серый — текст и фон подсказки.
 */
struct DarkColor {
    const char* light;
    const char* text; ///< Для color / selection-color; nullptr — как есть
    const char* fill; ///< Для фона и рамок; nullptr — как есть
};

constexpr std::array kDarkColors = {
    // фоны
    DarkColor{"#ffffff", nullptr, "#1e232a"},   DarkColor{"#f6f8fb", nullptr, "#232931"},
    DarkColor{"#f1f3f6", nullptr, "#181c22"},   DarkColor{"#e8ecf1", nullptr, "#14181d"},
    DarkColor{"#f0f2f5", nullptr, "#262c34"},   DarkColor{"#f0f3f6", nullptr, "#262c34"},
    DarkColor{"#f4f6f9", nullptr, "#2a3038"},   DarkColor{"#f9fafb", nullptr, "#2a3038"},
    DarkColor{"#e9edf1", nullptr, "#2c333c"},   DarkColor{"#e9ecef", nullptr, "#2c333c"},
    DarkColor{"#eef2f7", nullptr, "#2c333c"},   DarkColor{"#eef3f9", nullptr, "#24303e"},
    // акцент: мягкие фоны и рамки
    DarkColor{"#eef3fd", nullptr, "#22324a"},   DarkColor{"#e6eefd", nullptr, "#1f3b63"},
    DarkColor{"#d3def6", nullptr, "#355a8c"},   DarkColor{"#c7d8fb", nullptr, "#3a5f96"},
    DarkColor{"#b6ccf9", nullptr, "#3f68a5"},   DarkColor{"#b8cdf5", nullptr, "#3f68a5"},
    DarkColor{"#8fb0f3", nullptr, "#35527f"},   DarkColor{"#7fa3cc", nullptr, "#4f78a8"},
    // рамки
    DarkColor{"#dde3ea", nullptr, "#323942"},   DarkColor{"#d3d9e0", nullptr, "#2f363f"},
    DarkColor{"#dfe4ea", nullptr, "#343b45"},   DarkColor{"#d5dbe0", nullptr, "#3a424c"},
    DarkColor{"#e3e7eb", nullptr, "#30363e"},   DarkColor{"#b9c2cd", nullptr, "#4b5562"},
    DarkColor{"#aab4c0", nullptr, "#56606d"},
    // акцент
    DarkColor{"#2563eb", "#6aa1ff", "#2f6fe0"}, DarkColor{"#1d4ed8", "#8ab8ff", "#2a5fc4"},
    DarkColor{"#1e40af", "#9cc3ff", "#244fa8"},
    // текст
    DarkColor{"#161b22", "#e6eaf0", "#e6eaf0"}, DarkColor{"#2a313b", "#d4dae2", "#3a4350"},
    DarkColor{"#3b4552", "#c3cad4", "#4a5462"}, DarkColor{"#4d5866", "#a3adba", "#4d5866"},
    DarkColor{"#838d99", "#6f7987", "#56606d"},
    // состояния
    DarkColor{"#2e6b46", "#6fd39b", nullptr},   DarkColor{"#e2efe7", nullptr, "#1d3a2a"},
    DarkColor{"#b42318", "#ff8a80", "#b42318"}, DarkColor{"#fdecea", nullptr, "#4a2320"},
    DarkColor{"#e8b4ae", nullptr, "#7a3a33"},   DarkColor{"#b45309", "#fbbf24", nullptr},
    DarkColor{"#d97706", nullptr, "#f59e0b"},
};

/// Перекрашивает modern.qss в тёмную тему (по строкам: свойство решает, текст это или фон).
QString darken(const QString& styleSheet) {
    static const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{6}\\b"));
    QStringList lines = styleSheet.split('\n');
    for (QString& line : lines) {
        const QString property = line.section(':', 0, 0).trimmed();
        const bool text = property == QLatin1String("color") || property == QLatin1String("selection-color");
        QString result;
        qsizetype from = 0;
        for (auto match = hex.globalMatch(line); match.hasNext();) {
            const auto found = match.next();
            result += line.mid(from, found.capturedStart() - from);
            const QString light = found.captured().toLower();
            QString replacement = found.captured();
            for (const DarkColor& color : kDarkColors) {
                if (light == QLatin1String(color.light)) {
                    const char* value = text ? color.text : color.fill;
                    replacement = value != nullptr ? QString::fromLatin1(value) : replacement;
                    break;
                }
            }
            result += replacement;
            from = found.capturedEnd();
        }
        line = result + line.mid(from);
    }
    // Тёмные стрелки на тёмном фоне не видны — светлые.
    return lines.join('\n')
        .replace(QStringLiteral("arrow-down.png"), QStringLiteral("arrow-down-dark.png"))
        .replace(QStringLiteral("arrow-up.png"), QStringLiteral("arrow-up-dark.png"));
}

/// Палитра Qt для тёмной темы: родные окна (выбор файла, сообщения) — тоже тёмные.
QPalette darkPalette() {
    QPalette palette;
    const QColor window("#181c22");
    const QColor base("#1e232a");
    const QColor text("#e6eaf0");
    const QColor muted("#6f7987");
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, QColor("#232931"));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor("#262c34"));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor("#2f6fe0"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#3a4350"));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Link, QColor("#6aa1ff"));
    palette.setColor(QPalette::Mid, QColor("#323942"));
    palette.setColor(QPalette::Dark, QColor("#101317"));
    palette.setColor(QPalette::Light, QColor("#2c333c"));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, muted);
    }
    return palette;
}

/// Заменяет токены @name в таблице стилей значениями палитры.
QString substituteTokens(QString styleSheet) {
    // Длинные имена первыми, чтобы @accentHover не превратился в «<accent>Hover».
    auto entries = kPalette;
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
        return std::char_traits<char>::length(a.token) > std::char_traits<char>::length(b.token);
    });
    for (const Entry& entry : entries) {
        styleSheet.replace(QLatin1Char('@') + QLatin1String(entry.token),
                           QLatin1String(dark ? entry.dark : entry.value));
    }
    return styleSheet;
}

} // namespace

QColor color(Color role) {
    for (const Entry& entry : kPalette) {
        if (entry.role == role) {
            return QColor(QLatin1String(dark ? entry.dark : entry.value));
        }
    }
    return {}; // недостижимо: каждая роль есть в палитре
}

bool isDark() {
    return dark;
}

bool apply() {
    // Тема: из настроек; «как в системе» — по схеме цветов системы.
    const settings::ThemeMode mode = settings::themeMode();
    dark = mode == settings::ThemeMode::Dark ||
           (mode == settings::ThemeMode::System &&
            QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    // Родной стиль Windows палитру почти не слушает: в тёмной теме — Fusion.
    static const QString nativeStyle = QApplication::style()->name();
    QApplication::setStyle(dark ? QStringLiteral("Fusion") : nativeStyle);
    QApplication::setPalette(dark ? darkPalette() : QApplication::style()->standardPalette());

    // Шрифт: макет рассчитан на 13px system-ui, 10pt даёт близкий размер.
    // Список семейств — по убыванию приоритета: Windows, затем Linux.
    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"),
                      QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
    font.setPointSizeF(10.0);
    QApplication::setFont(font);

    // Основная тема (modern.qss) и дополнение для новых элементов (harmony.qss).
    QFile base(QStringLiteral(":/res/theme/modern.qss"));
    QFile extra(QStringLiteral(":/res/theme/harmony.qss"));
    if (!base.open(QIODevice::ReadOnly) || !extra.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QString modern = QString::fromUtf8(base.readAll());
    qApp->setStyleSheet((dark ? darken(modern) : modern) + QLatin1Char('\n') +
                        substituteTokens(QString::fromUtf8(extra.readAll())));
    return true;
}

} // namespace harmony::app::theme

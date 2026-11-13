/**
 * @file i18n.cpp
 * @brief Переводчик из файлов .ts, поиск своих переводов и установка языка.
 */

#include "i18n.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace harmony::app::i18n {

namespace {

QString currentCode = QStringLiteral("en");

/// Встроенный перевод языка.
QString embeddedFile(const QString& code) {
    return QStringLiteral(":/res/i18n/harmony_%1.ts").arg(code);
}

/// Пары «исходная строка → перевод» из .ts; пустые и устаревшие переводы пропускаются.
QHash<QString, QString> readTs(const QString& path) {
    QHash<QString, QString> table;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return table;
    }
    QXmlStreamReader xml(&file);
    QString source;
    while (!xml.atEnd()) {
        if (!xml.readNextStartElement()) {
            continue;
        }
        if (xml.name() == QLatin1String("message")) {
            source.clear();
        } else if (xml.name() == QLatin1String("source")) {
            source = xml.readElementText();
        } else if (xml.name() == QLatin1String("translation")) {
            const bool obsolete =
                xml.attributes().value(QLatin1String("type")) == QLatin1String("obsolete");
            const QString text = xml.readElementText();
            if (!source.isEmpty() && !text.isEmpty() && !obsolete) {
                table.insert(source, text);
            }
        }
    }
    return table;
}

/// Переводчик по таблице «исходная строка → перевод».
class TsTranslator final : public QTranslator {
public:
    /// Добавляет переводы из файла поверх уже загруженных.
    void merge(const QHash<QString, QString>& table) { table_.insert(table); }

    QString translate(const char* /*context*/, const char* sourceText,
                      const char* /*disambiguation*/, int /*n*/) const override {
        return table_.value(QString::fromUtf8(sourceText));
    }

    [[nodiscard]] bool isEmpty() const override { return table_.isEmpty(); }

private:
    QHash<QString, QString> table_;
};

/// Код языка из имени файла harmony_<код>.ts; пусто — не файл перевода.
QString codeOf(const QString& fileName) {
    static const QRegularExpression pattern(
        QStringLiteral("^harmony_([A-Za-z]{2,3}(?:_[A-Za-z]{2,4})?)\\.ts$"));
    const auto match = pattern.match(fileName);
    return match.hasMatch() ? match.captured(1) : QString();
}

/// Название языка на нём самом: «Deutsch», «Português (Brasil)».
QString nativeName(const QString& code) {
    const QLocale locale(code);
    QString name = locale.nativeLanguageName();
    if (name.isEmpty() || locale.language() == QLocale::C) {
        return code;
    }
    if (code.contains('_') && !locale.nativeTerritoryName().isEmpty()) {
        name += QStringLiteral(" (%1)").arg(locale.nativeTerritoryName());
    }
    return name.left(1).toUpper() + name.mid(1);
}

} // namespace

QString userTranslationFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Harmony/translations"));
}

QStringList translationFolders() {
    return {QApplication::applicationDirPath() + QStringLiteral("/translations"),
            userTranslationFolder()};
}

QList<Language> languages() {
    QList<Language> list = {{QStringLiteral("en"), QStringLiteral("English")},
                            {QStringLiteral("ru"), QStringLiteral("Русский")}};
    auto find = [&list](const QString& code) -> Language* {
        for (auto& language : list) {
            if (language.code == code) {
                return &language;
            }
        }
        return nullptr;
    };
    for (const QString& folder : translationFolders()) {
        for (const QString& name :
             QDir(folder).entryList({QStringLiteral("harmony_*.ts")}, QDir::Files)) {
            const QString code = codeOf(name);
            if (code.isEmpty()) {
                continue;
            }
            if (Language* known = find(code)) {
                known->external = true;
            } else {
                list.append({code, nativeName(code), true});
            }
        }
    }
    return list;
}

void install(QApplication& app, const QString& code) {
    // Строки в коде — по-английски; другой язык — встроенный перевод и свои файлы поверх.
    currentCode = code.isEmpty() ? QStringLiteral("en") : code;
    auto* translator = new TsTranslator;
    translator->setParent(&app);
    translator->merge(readTs(embeddedFile(currentCode)));
    for (const QString& folder : translationFolders()) {
        translator->merge(readTs(QDir(folder).filePath(QStringLiteral("harmony_%1.ts").arg(currentCode))));
    }
    if (translator->isEmpty()) {
        delete translator; // английский без своих правок: строки в коде уже на нём
        if (currentCode != QLatin1String("en") && !QFile::exists(embeddedFile(currentCode))) {
            currentCode = QStringLiteral("en"); // перевода нет — английский
        }
    } else {
        QApplication::installTranslator(translator);
    }
    QLocale::setDefault(QLocale(currentCode));

    // Стандартные кнопки и диалоги Qt — из переводов самого Qt, если они установлены.
    auto* qt = new QTranslator(&app);
    const QString qtFile = QStringLiteral("qtbase_") + currentCode;
    if (qt->load(qtFile, QApplication::applicationDirPath() + QStringLiteral("/translations")) ||
        qt->load(qtFile, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(qt);
    } else {
        delete qt;
    }
}

QString current() {
    return currentCode;
}

bool writeTemplate(const QString& file, const QString& code) {
    // Строки — из встроенного русского перевода (там все строки программы).
    QFile source(embeddedFile(QStringLiteral("ru")));
    if (!source.open(QIODevice::ReadOnly)) {
        return false;
    }
    QXmlStreamReader in(&source);
    QSaveFile target(file);
    if (!target.open(QIODevice::WriteOnly)) {
        return false;
    }
    QXmlStreamWriter out(&target);
    out.setAutoFormatting(true);
    out.writeStartDocument();
    out.writeDTD(QStringLiteral("<!DOCTYPE TS>"));
    out.writeStartElement(QStringLiteral("TS"));
    out.writeAttribute(QStringLiteral("version"), QStringLiteral("2.1"));
    out.writeAttribute(QStringLiteral("language"), code);
    out.writeAttribute(QStringLiteral("sourcelanguage"), QStringLiteral("ru_RU"));
    out.writeStartElement(QStringLiteral("context"));
    out.writeTextElement(QStringLiteral("name"), QStringLiteral("Harmony"));
    QString text;
    while (!in.atEnd()) {
        if (!in.readNextStartElement()) {
            continue;
        }
        if (in.name() == QLatin1String("source")) {
            text = in.readElementText();
        } else if (in.name() == QLatin1String("translation")) {
            const bool obsolete =
                in.attributes().value(QLatin1String("type")) == QLatin1String("obsolete");
            const QString english = in.readElementText();
            if (text.isEmpty() || obsolete) {
                continue;
            }
            out.writeStartElement(QStringLiteral("message"));
            out.writeTextElement(QStringLiteral("source"), text);
            if (!english.isEmpty()) {
                out.writeTextElement(QStringLiteral("extracomment"),
                                     QStringLiteral("English: ") + english);
            }
            out.writeStartElement(QStringLiteral("translation"));
            out.writeAttribute(QStringLiteral("type"), QStringLiteral("unfinished"));
            out.writeEndElement();
            out.writeEndElement();
            text.clear();
        }
    }
    out.writeEndElement(); // context
    out.writeEndElement(); // TS
    out.writeEndDocument();
    return !in.hasError() && target.commit();
}

} // namespace harmony::app::i18n

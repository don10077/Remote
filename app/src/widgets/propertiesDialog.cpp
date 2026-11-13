/**
 * @file propertiesDialog.cpp
 * @brief Окно «Свойства» устройства или активности.
 */

#include "propertiesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

namespace harmony::app {

namespace {

QString qs(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string lowered(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

/// Заголовок раздела на всю ширину таблицы.
void addHeading(QTableWidget* table, const QString& text) {
    const int row = table->rowCount();
    table->insertRow(row);
    auto* item = new QTableWidgetItem(text);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    item->setFlags(Qt::ItemIsEnabled);
    table->setItem(row, 0, item);
    table->setSpan(row, 0, 1, 2);
}

} // namespace

PropertiesDialog::PropertiesDialog(const ConfigDocument& document, bool activity, const std::string& id,
                                   const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Properties — %1").arg(title));
    resize(640, 600);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("The remote changes its behaviour by these settings. Hover a name to see what "
                               "it does. “Not set” removes the setting from the file: the remote then uses "
                               "its own default."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* table = new QTableWidget(0, 2, this);
    table->setHorizontalHeaderLabels({tr("Setting"), tr("Value")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionMode(QAbstractItemView::NoSelection);
    if (activity) {
        addSection(table, document, PropertyScope::Activity, id);
    } else {
        addHeading(table, tr("Pauses between presses"));
        addSection(table, document, PropertyScope::DeviceTiming, id);
        addHeading(table, tr("Device"));
        addSection(table, document, PropertyScope::Device, id);
    }
    layout->addWidget(table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PropertiesDialog::addSection(QTableWidget* table, const ConfigDocument& document, PropertyScope scope,
                                  const std::string& id) {
    const auto present = document.properties(scope, id);
    auto valueIn = [&present](std::string_view name) {
        for (const auto& [key, value] : present) {
            if (key == name) return value;
        }
        return std::string();
    };
    // Сначала известные (в порядке каталога), затем те, что есть в файле, но программе не знакомы.
    std::vector<Row> section;
    std::set<std::string> known;
    for (const PropertyInfo& info : propertyCatalog()) {
        if (info.scope == scope) {
            section.push_back({scope, std::string(info.name), valueIn(info.name), &info, nullptr});
            known.insert(std::string(info.name));
        }
    }
    for (const auto& [name, value] : present) {
        if (!known.contains(name)) {
            section.push_back({scope, name, value, nullptr, nullptr});
        }
    }

    for (Row& row : section) {
        const int index = table->rowCount();
        table->insertRow(index);
        auto* label = new QTableWidgetItem(row.info != nullptr ? tr(row.info->label.data()) : qs(row.name));
        label->setFlags(Qt::ItemIsEnabled);
        label->setToolTip(row.info != nullptr ? qs(row.name) + "\n\n" + tr(row.info->description.data())
                                              : tr("%1 — not described; edited as text").arg(qs(row.name)));
        table->setItem(index, 0, label);

        const PropertyInfo::Type type = row.info != nullptr ? row.info->type : PropertyInfo::Type::Text;
        if (type == PropertyInfo::Type::Bool) {
            auto* combo = new QComboBox;
            combo->addItem(tr("Not set"), QString());
            combo->addItem(tr("Yes"), "true");
            combo->addItem(tr("No"), "false");
            const std::string value = lowered(row.original);
            combo->setCurrentIndex(value.empty() ? 0 : (value == "true" || value == "1") ? 1 : 2);
            row.editor = combo;
        } else if (type == PropertyInfo::Type::Int) {
            auto* spin = new QSpinBox;
            spin->setRange(row.info->min - 1, row.info->max);
            spin->setSpecialValueText(tr("Not set"));
            bool ok = false;
            const int number = qs(row.original).toInt(&ok);
            spin->setValue(ok ? number : row.info->min - 1);
            row.editor = spin;
        } else if (type == PropertyInfo::Type::Enum) {
            // Список встречавшихся значений, но можно ввести и своё: перечень — не закрытый.
            auto* combo = new QComboBox;
            combo->setEditable(true);
            combo->addItem(QString());
            for (const std::string_view value : row.info->values) {
                combo->addItem(qs(value));
            }
            combo->setCurrentText(qs(row.original));
            row.editor = combo;
        } else {
            auto* edit = new QLineEdit(qs(row.original));
            edit->setPlaceholderText(tr("Not set"));
            row.editor = edit;
        }
        if (row.info != nullptr && row.info->readOnly) {
            row.editor->setEnabled(false);
            row.editor->setToolTip(tr("Written by the remote itself"));
        }
        table->setCellWidget(index, 1, row.editor);
    }
    std::ranges::move(section, std::back_inserter(rows_));
}

std::string PropertiesDialog::valueOf(const Row& row) {
    if (const auto* spin = qobject_cast<const QSpinBox*>(row.editor)) {
        return spin->value() == spin->minimum() ? std::string() : std::to_string(spin->value());
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(row.editor)) {
        return (combo->isEditable() ? combo->currentText() : combo->currentData().toString()).trimmed().toStdString();
    }
    if (const auto* edit = qobject_cast<const QLineEdit*>(row.editor)) {
        return edit->text().trimmed().toStdString();
    }
    return row.original;
}

std::vector<PropertiesDialog::Change> PropertiesDialog::changes() const {
    std::vector<Change> out;
    for (const Row& row : rows_) {
        if (row.info != nullptr && row.info->readOnly) {
            continue;
        }
        const std::string value = valueOf(row);
        const bool boolean = row.info != nullptr && row.info->type == PropertyInfo::Type::Bool;
        // Логические сравниваются без регистра: «True» в файле и «Да» в окне — одно и то же.
        const bool same = boolean ? lowered(value) == lowered(row.original) ||
                                        (lowered(row.original) == "1" && value == "true") ||
                                        (lowered(row.original) == "0" && value == "false")
                                  : value == row.original;
        if (!same) {
            out.push_back({row.scope, row.name, value});
        }
    }
    return out;
}

} // namespace harmony::app

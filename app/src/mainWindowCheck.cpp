/**
 * @file mainWindowCheck.cpp
 * @brief Главное окно: сравнение конфигураций, быстрый поиск и проверка.
 */

#include "mainWindow.h"
#include "labels.h"
#include "widgets/configView.h"
#include "widgets/checkDialog.h"
#include "widgets/compareConfigsDialog.h"
#include "widgets/quickSearch.h"

#include <QStyle>
#include <QToolButton>

namespace harmony::app {

void MainWindow::compareWithOther() {
    if (!document_) {
        return;
    }
    CompareConfigsDialog dialog(*document_, this);
    dialog.exec();
}

void MainWindow::showQuickSearch() {
    if (!document_) {
        return;
    }
    QuickSearch search(document_->model(), this);
    if (search.exec() != QDialog::Accepted || !search.chosen()) {
        return;
    }
    using Kind = QuickSearch::Hit::Kind;
    const QuickSearch::Hit& hit = *search.chosen();
    switch (hit.kind) {
    case Kind::Device:
    case Kind::Activity:
        configView_->select(hit.kind == Kind::Device ? ConfigView::Item::Device : ConfigView::Item::Activity, hit.id);
        break;
    case Kind::DeviceKey:
    case Kind::ActivityKey:
        configView_->select(hit.kind == Kind::DeviceKey ? ConfigView::Item::Device : ConfigView::Item::Activity, hit.id);
        configView_->selectButton(ButtonRef::hard(QString::fromStdString(hit.key)));
        break;
    case Kind::DeviceSoft:
    case Kind::ActivitySoft:
        configView_->select(hit.kind == Kind::DeviceSoft ? ConfigView::Item::Device : ConfigView::Item::Activity,
                            hit.id);
        configView_->selectButton(ButtonRef::screen(hit.position));
        break;
    case Kind::Command:
        configView_->selectCommand(hit.id, hit.command);
        break;
    }
}

void MainWindow::updateProblems() {
    if (problems_ == nullptr) {
        return; // ещё строится окно
    }
    if (!document_) {
        problems_->hide();
        return;
    }
    const auto problems = document_->check();
    const auto errors = static_cast<qsizetype>(countProblems(problems, Problem::Severity::Error));
    const auto warnings = static_cast<qsizetype>(countProblems(problems, Problem::Severity::Warning));
    if (problems.empty()) {
        problems_->hide();
        return;
    }
    QStringList parts;
    if (errors > 0) {
        parts << labels::plural(errors, "error", "errors");
    }
    if (warnings > 0) {
        parts << labels::plural(warnings, "warning", "warnings");
    }
    problems_->setIcon(style()->standardIcon(errors > 0 ? QStyle::SP_MessageBoxCritical
                                                        : QStyle::SP_MessageBoxWarning));
    problems_->setText(parts.join(QStringLiteral(" · ")));
    problems_->show();
}

void MainWindow::showCheck() {
    if (!document_) {
        return;
    }
    CheckDialog dialog(*document_, [this](const QString& text, const auto& change) { return applyEdit(text, change); },
                       this);
    if (dialog.exec() == QDialog::Accepted && dialog.target()) {
        showProblem(*dialog.target());
    }
}

void MainWindow::showProblem(const Problem& problem) {
    if (problem.where == Problem::Where::Config) {
        return;
    }
    configView_->select(problem.where == Problem::Where::Device ? ConfigView::Item::Device
                                                               : ConfigView::Item::Activity,
                        problem.id);
    if (!problem.hardKey.empty()) {
        configView_->selectButton(ButtonRef::hard(QString::fromStdString(problem.hardKey)));
    } else if (problem.softPosition >= 0) {
        configView_->selectButton(ButtonRef::screen(problem.softPosition));
    }
}

} // namespace harmony::app

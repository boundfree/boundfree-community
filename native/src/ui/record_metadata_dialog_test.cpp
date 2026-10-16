#include "ui/record_metadata_dialog.h"
#include "i18n/i18n_service.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "record-metadata-dialog-test: FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    bf::I18nService i18n(QStringLiteral(BF_REPO_ROOT));
    QString error;
    if (!expect(i18n.loadLocale("en_US", &error), "load en_US")) return 1;
    QTemporaryDir temporary;
    if (!expect(temporary.isValid(), "temporary directory")) return 1;
    const QDir directory(temporary.path());
    const auto originalEntries = directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);

    bf::RecordMetadataDialog cancelled(&i18n, temporary.path());
    int accepted = 0;
    QObject::connect(&cancelled, &QDialog::accepted, [&]() { ++accepted; });
    cancelled.show();
    QApplication::processEvents();
    auto* cancelButton = cancelled.findChild<QPushButton*>("recordCancel");
    if (!expect(cancelButton != nullptr, "cancel button")) return 1;
    cancelButton->click();
    if (!expect(accepted == 0 && cancelled.result() == QDialog::Rejected, "cancel must not accept")
        || !expect(originalEntries == directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot), "opening/cancelling must not create files")) return 1;

    bf::RecordMetadataDialog dialog(&i18n, temporary.path());
    auto* animal = dialog.findChild<QLineEdit*>("recordAnimalId");
    auto* experiment = dialog.findChild<QLineEdit*>("recordExperimentType");
    auto* notes = dialog.findChild<QPlainTextEdit*>("recordNotes");
    auto* output = dialog.findChild<QLineEdit*>("recordOutputDirectory");
    auto* start = dialog.findChild<QPushButton*>("recordStart");
    auto* browse = dialog.findChild<QPushButton*>("recordBrowseDirectory");
    auto* validation = dialog.findChild<QLabel*>("recordDirectoryValidation");
    if (!expect(animal && experiment && notes && output && start && browse && validation, "all form fields")) return 1;
    QObject::connect(&dialog, &QDialog::accepted, [&]() { ++accepted; });
    if (!expect(start->isEnabled() && accepted == 0, "valid default must await explicit start")) return 1;
    output->clear();
    dialog.accept();
    if (!expect(!start->isEnabled() && accepted == 0, "empty path must not accept")) return 1;
    const QString missingPath = directory.filePath("must-not-create");
    output->setText(missingPath);
    dialog.accept();
    if (!expect(!start->isEnabled() && accepted == 0 && !QFileInfo::exists(missingPath), "missing directory must not be created")) return 1;
    const QString filePath = directory.filePath("not-a-directory");
    QFile file(filePath);
    if (!expect(file.open(QIODevice::WriteOnly), "create path-file fixture")) return 1;
    file.close();
    output->setText(filePath);
    dialog.accept();
    if (!expect(!start->isEnabled() && accepted == 0, "file is not a directory")) return 1;
    const QString removedPath = directory.filePath("removed-before-start");
    if (!expect(QDir().mkdir(removedPath), "create removable directory")) return 1;
    output->setText(removedPath);
    if (!expect(start->isEnabled(), "existing writable directory valid")) return 1;
    if (!expect(QDir().rmdir(removedPath), "remove directory before start")) return 1;
    dialog.accept();
    if (!expect(accepted == 0 && !start->isEnabled(), "accept must revalidate removed directory")) return 1;

    for (const QString& locale : bf::I18nService::kStableLocales) {
        if (!expect(i18n.loadLocale(locale, &error), "switch locale")) return 1;
        if (!expect(browse->text() == i18n.text("app.record.browse") && !browse->text().startsWith("app."), "browse localized on language change")
            || !expect(validation->text() == i18n.text("app.record.directory_invalid") && !validation->text().startsWith("app."), "validation localized")) return 1;
    }
    output->setText(temporary.path());
    animal->setText("  Mouse_07  ");
    experiment->setText("  sleep baseline  ");
    notes->setPlainText("  first line\n\u7b2c\u4e8c\u884c\nthird line  ");
    const QJsonObject metadata = dialog.metadata();
    if (!expect(metadata.value("animal_id").toString() == "Mouse_07"
        && metadata.value("exp_type").toString() == "sleep baseline"
        && metadata.value("notes").toString() == QString::fromUtf8("first line\n\u7b2c\u4e8c\u884c\nthird line"), "all metadata and multiline notes preserved")) return 1;
    if (!expect(dialog.outputDirectory() == temporary.path(), "output directory retained")) return 1;
    start->click();
    if (!expect(accepted == 1 && dialog.result() == QDialog::Accepted, "explicit start accepts once")) return 1;
    if (!expect(directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot) == QStringList{"not-a-directory"}, "form acceptance alone must not create recording files")) return 1;
    animal->clear();
    if (!expect(dialog.metadata().value("animal_id").toString() == "unknown", "legacy empty animal fallback")) return 1;
    std::cout << "record-metadata-dialog-test-ok\n";
    return 0;
}

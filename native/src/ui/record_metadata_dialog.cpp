#include "ui/fluent_controls.h"
#include "ui/record_metadata_dialog.h"

#include "i18n/i18n_service.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>

namespace bf {

RecordMetadataDialog::RecordMetadataDialog(I18nService* i18n, const QString& initialOutputDirectory, QWidget* parent)
    : QDialog(parent), i18n_(i18n) {
    setObjectName("recordMetadataDialog");
    setMinimumWidth(360);
    resize(520, 360);
    auto* form = new QFormLayout(this);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    animal_ = new TextInput(QStringLiteral("Animal_01"), this);
    experiment_ = new TextInput(QStringLiteral("LFP_baseline"), this);
    notes_ = new QPlainTextEdit(this);
    notes_->setMinimumHeight(80);
    notes_->setMaximumHeight(160);
    notes_->setTabChangesFocus(true);
    output_ = new TextInput(this);
    animal_->setObjectName("recordAnimalId");
    experiment_->setObjectName("recordExperimentType");
    notes_->setObjectName("recordNotes");
    output_->setObjectName("recordOutputDirectory");
    animalLabel_ = new QLabel(this);
    experimentLabel_ = new QLabel(this);
    notesLabel_ = new QLabel(this);
    outputLabel_ = new QLabel(this);
    animalLabel_->setBuddy(animal_);
    experimentLabel_->setBuddy(experiment_);
    notesLabel_->setBuddy(notes_);
    outputLabel_->setBuddy(output_);
    form->addRow(animalLabel_, animal_);
    form->addRow(experimentLabel_, experiment_);
    form->addRow(notesLabel_, notes_);
    auto* directoryRow = new QHBoxLayout;
    browse_ = new ActionButton(this);
    browse_->setObjectName("recordBrowseDirectory");
    directoryRow->addWidget(output_, 1);
    directoryRow->addWidget(browse_);
    form->addRow(outputLabel_, directoryRow);
    validationLabel_ = new QLabel(this);
    validationLabel_->setObjectName("recordDirectoryValidation");
    validationLabel_->setWordWrap(true);
    form->addRow(validationLabel_);
    auto* buttons = new QDialogButtonBox(this);
    start_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    cancel_ = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
    start_->setObjectName("recordStart");
    cancel_->setObjectName("recordCancel");
    start_->setDefault(true);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &RecordMetadataDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(output_, &QLineEdit::textChanged, this, [this]() { validateDirectory(); });
    connect(browse_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, i18n_->text("app.record.output_dir"), outputDirectory());
        if (!path.isEmpty()) output_->setText(path);
    });
    connect(i18n_, &I18nService::languageChanged, this, &RecordMetadataDialog::retranslateUi);
    QString directory = initialOutputDirectory;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!QFileInfo(directory).isDir()) directory = QDir::homePath();
    }
    output_->setText(directory);
    retranslateUi();
}

QJsonObject RecordMetadataDialog::metadata() const {
    const QString animal = animal_->text().trimmed();
    return {{"animal_id", animal.isEmpty() ? QStringLiteral("unknown") : animal},
            {"exp_type", experiment_->text().trimmed()}, {"notes", notes_->toPlainText().trimmed()}};
}

QString RecordMetadataDialog::outputDirectory() const {
    const QString value = output_->text().trimmed();
    return value.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(value).absoluteFilePath());
}

bool RecordMetadataDialog::validateDirectory() {
    const QString path = outputDirectory();
    const QFileInfo directory(path);
    const bool valid = !path.isEmpty() && directory.exists() && directory.isDir() && directory.isWritable();
    start_->setEnabled(valid);
    validationLabel_->setText(valid ? QString() : i18n_->text("app.record.directory_invalid"));
    validationLabel_->setVisible(!valid);
    return valid;
}

void RecordMetadataDialog::accept() {
    if (!validateDirectory()) {
        output_->setFocus();
        return;
    }
    QDialog::accept();
}

void RecordMetadataDialog::retranslateUi() {
    setWindowTitle(i18n_->text("app.record.metadata"));
    animalLabel_->setText(i18n_->text("app.record.animal_id"));
    experimentLabel_->setText(i18n_->text("app.record.exp_type"));
    notesLabel_->setText(i18n_->text("app.record.notes"));
    outputLabel_->setText(i18n_->text("app.record.output_dir"));
    browse_->setText(i18n_->text("app.record.browse"));
    start_->setText(i18n_->text("app.record.start"));
    cancel_->setText(i18n_->text("app.record.cancel"));
    validateDirectory();
}

}  // namespace bf

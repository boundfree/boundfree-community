#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace bf {
class I18nService;

// Collects metadata only. The caller starts the recorder after Accepted;
// constructing, validating and cancelling this form never creates files.
class RecordMetadataDialog final : public QDialog {
public:
    explicit RecordMetadataDialog(I18nService* i18n, const QString& initialOutputDirectory = {}, QWidget* parent = nullptr);
    QJsonObject metadata() const;
    QString outputDirectory() const;
    void accept() override;

private:
    bool validateDirectory();
    void retranslateUi();

    I18nService* i18n_;
    QLineEdit* animal_;
    QLineEdit* experiment_;
    QPlainTextEdit* notes_;
    QLineEdit* output_;
    QLabel* animalLabel_;
    QLabel* experimentLabel_;
    QLabel* notesLabel_;
    QLabel* outputLabel_;
    QLabel* validationLabel_;
    QPushButton* browse_;
    QPushButton* start_;
    QPushButton* cancel_;
};
}  // namespace bf

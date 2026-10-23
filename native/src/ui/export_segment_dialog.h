#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace bf {
class I18nService;

class ExportSegmentDialog final : public QDialog {
public:
    ExportSegmentDialog(I18nService* i18n, double duration, double start, double end,
                        bool processed, const QString& processingSummary, QWidget* parent = nullptr);
    double startTime() const;
    double endTime() const;
    bool processed() const;

private:
    void refresh();
    void retranslateUi();
    I18nService* i18n_;
    QString processingSummary_;
    double sourceDuration_;
    QDoubleSpinBox* start_;
    QDoubleSpinBox* end_;
    QComboBox* mode_;
    QLabel* startLabel_;
    QLabel* endLabel_;
    QLabel* modeLabel_;
    QLabel* hint_;
    QLabel* validation_;
    QPushButton* save_;
    QPushButton* cancel_;
};
}  // namespace bf

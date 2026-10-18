#include "ui/export_segment_dialog.h"
#include "i18n/i18n_service.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <algorithm>

namespace bf {
ExportSegmentDialog::ExportSegmentDialog(I18nService* i18n, double duration, double start, double end,
                                       bool processed, const QString& processingSummary, QWidget* parent)
    : QDialog(parent), i18n_(i18n), processingSummary_(processingSummary) {
    setObjectName("eegExportSegmentDialog");
    resize(480, 280);
    auto* form = new QFormLayout(this);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    start_ = new QDoubleSpinBox(this);
    end_ = new QDoubleSpinBox(this);
    for (auto* spin : {start_, end_}) {
        spin->setDecimals(6);
        spin->setRange(0.0, duration);
        spin->setSuffix(" s");
        spin->setKeyboardTracking(false);
    }
    start_->setObjectName("eegExportStart");
    end_->setObjectName("eegExportEnd");
    start_->setValue(std::clamp(start, 0.0, duration));
    end_->setValue(std::clamp(end, 0.0, duration));
    mode_ = new QComboBox(this);
    mode_->setObjectName("eegExportDataMode");
    mode_->addItems({QString(), QString()});
    mode_->setCurrentIndex(processed ? 1 : 0);
    startLabel_ = new QLabel(this);
    endLabel_ = new QLabel(this);
    modeLabel_ = new QLabel(this);
    startLabel_->setBuddy(start_);
    endLabel_->setBuddy(end_);
    modeLabel_->setBuddy(mode_);
    form->addRow(startLabel_, start_);
    form->addRow(endLabel_, end_);
    form->addRow(modeLabel_, mode_);
    hint_ = new QLabel(this);
    hint_->setObjectName("eegExportDataHint");
    hint_->setWordWrap(true);
    hint_->setTextFormat(Qt::PlainText);
    form->addRow(hint_);
    validation_ = new QLabel(this);
    validation_->setObjectName("eegExportValidation");
    validation_->setWordWrap(true);
    form->addRow(validation_);
    auto* buttons = new QDialogButtonBox(this);
    save_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    cancel_ = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
    save_->setObjectName("eegExportContinue");
    save_->setDefault(true);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(start_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { refresh(); });
    connect(end_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { refresh(); });
    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { refresh(); });
    connect(i18n_, &I18nService::languageChanged, this, [this]() { retranslateUi(); });
    retranslateUi();
}

double ExportSegmentDialog::startTime() const { return start_->value(); }
double ExportSegmentDialog::endTime() const { return end_->value(); }
bool ExportSegmentDialog::processed() const { return mode_->currentIndex() == 1; }

void ExportSegmentDialog::retranslateUi() {
    setWindowTitle(i18n_->text("app.export.title"));
    startLabel_->setText(i18n_->text("app.export.start"));
    endLabel_->setText(i18n_->text("app.export.end"));
    modeLabel_->setText(i18n_->text("app.signal.data"));
    start_->setAccessibleName(startLabel_->text());
    end_->setAccessibleName(endLabel_->text());
    mode_->setAccessibleName(modeLabel_->text());
    const QSignalBlocker blocker(mode_);
    mode_->setItemText(0, i18n_->text("app.signal.raw"));
    mode_->setItemText(1, i18n_->text("app.signal.processed"));
    save_->setText(i18n_->text("app.export.choose_path"));
    cancel_->setText(i18n_->text("app.record.cancel"));
    start_->setLocale(i18n_->qlocale());
    end_->setLocale(i18n_->qlocale());
    refresh();
}

void ExportSegmentDialog::refresh() {
    const bool valid = endTime() > startTime();
    save_->setEnabled(valid);
    validation_->setVisible(!valid);
    validation_->setText(i18n_->text("app.export.range_hint"));
    hint_->setText(processed()
        ? i18n_->text("app.export.processed_hint") + "\n" + processingSummary_
        : i18n_->text("app.export.raw_hint"));
}
}  // namespace bf

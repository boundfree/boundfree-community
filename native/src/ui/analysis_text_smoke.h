#pragma once
#include "ui/main_window.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QStyleOptionComboBox>
#include <QTextDocument>
#include <QTextTable>
#include <cmath>
#include <iostream>

inline int smokeAnalysisText(int argc, char** argv, const bf::VersionInfo& version, const QString& repoRoot) {
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString error;
    if (!i18n.loadLocale("en_US", &error)) return 1;
    bf::MainWindow window(version, &i18n, repoRoot);
    window.resize(1280, 800);
    bf::FileOpenResult input;
    input.session.ok = true; input.session.path = "text.csv";
    input.session.fs = 200; input.session.duration = 10; input.session.nSamples = 2000; input.session.nChannels = 2;
    input.session.chNames = {"EEG EEG1A-B", "EEG EEG2A-B"};
    input.waveform.fs = 200; input.waveform.t0 = 0; input.waveform.t1 = 10;
    input.waveform.channelNames = input.session.chNames;
    input.waveform.data.resize(2);
    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 2000; ++n)
        input.waveform.data[ch].push_back(40 * std::sin(2 * 3.141592653589793 * (8 + ch * 3) * n / 200.));
    window.showFileOpenResult(input); window.show();
    auto* tabs = window.findChild<QTabWidget*>("eegAnalysisTabs");
    const auto settle = []() { QEventLoop loop; QTimer::singleShot(40, &loop, &QEventLoop::quit); loop.exec(); };
    const auto fail = [&](const QString& reason) {
        std::cerr << "analysis-text-failed " << reason.toStdString() << '\n';
        const QString dir = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
        if (!dir.isEmpty()) { QDir().mkpath(dir); window.grab().save(QDir(dir).filePath("text-failure.png")); }
        return 1;
    };
    int checked = 0;
    int shortLocaleMinimum = 0;
    for (const QString& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW"), QString("pseudo")}) {
        if (!i18n.loadLocale(locale, &error)) return fail(error);
        for (int page = 0; page < 4; ++page) {
            tabs->setCurrentIndex(page);
            auto* status = window.findChild<QLabel*>(QString("eegAnalysisStatus_%1").arg(page));
            QElapsedTimer timeout; timeout.start();
            while (status->property("analysisState").toString() != "ready" && timeout.elapsed() < 4000) settle();
            if (status->property("analysisState").toString() != "ready") return fail("not_ready");
            for (const QSize target : {QSize(900, 560), QSize(1280, 800)}) {
                window.resize(target); settle(); window.resize(target); settle();
                auto* properties = window.findChild<QWidget*>(QString("eegAnalysisProperties_%1").arg(page));
                if (!properties || qobject_cast<QScrollArea*>(properties) || !properties->isVisible()) return fail("scrolling_properties");
                const QRect propertyBounds(properties->mapTo(&window, QPoint()), properties->size());
                if (!window.rect().contains(propertyBounds)) return fail("properties_outside_window");
                auto* inputContext = window.findChild<QLabel*>("eegAnalysisInputSummary");
                if (!inputContext || inputContext->height() < inputContext->heightForWidth(inputContext->width()))
                    return fail("input_context_clipped");
                for (const char* role : {"Status", "Legend", "Summary"}) {
                    auto* label = window.findChild<QLabel*>(QString("eegAnalysis%1_%2").arg(role).arg(page));
                    if (!label->isVisible() || label->text().isEmpty()) continue;
                    const int required = label->heightForWidth(label->width());
                    if (label->height() < required) return fail(QString("clipped:%1:%2:%3 %4x%5 required=%6")
                        .arg(locale).arg(page).arg(role).arg(label->width()).arg(label->height()).arg(required));
                    if (!properties->rect().contains(QRect(label->mapTo(properties, QPoint()), label->size()))) return fail("text_outside_properties");
                    for (QWidget* ancestor = label->parentWidget(); ancestor && ancestor != &window; ancestor = ancestor->parentWidget()) {
                        if (!ancestor->rect().contains(QRect(label->mapTo(ancestor, QPoint()), label->size())))
                            return fail(QString("ancestor_clips:%1:%2:%3:%4 label=%5x%6 ancestor=%7x%8")
                                .arg(locale).arg(page).arg(role).arg(ancestor->objectName()).arg(label->width()).arg(label->height()).arg(ancestor->width()).arg(ancestor->height())
                                + QString(" y=%1 min=%2 hint=%3 hfw=%4 parent=%5").arg(label->mapTo(ancestor,QPoint()).y()).arg(ancestor->minimumHeight()).arg(ancestor->minimumSizeHint().height()).arg(ancestor->heightForWidth(ancestor->width())).arg(ancestor->parentWidget()->objectName()));
                    }
                }
                if (page == 0 || page == 3) {
                    auto* summary = window.findChild<QLabel*>(QString("eegAnalysisSummary_%1").arg(page));
                    QTextDocument document;
                    document.setDocumentMargin(0);
                    document.setDefaultFont(summary->font()); document.setHtml(summary->text());
                    document.setTextWidth(summary->width());
                    if (summary->height() + 1 < document.size().height()) return fail("table_last_row_clipped");
                    QTextTable* table = nullptr;
                    for (auto* frame : document.rootFrame()->childFrames())
                        if ((table = qobject_cast<QTextTable*>(frame))) break;
                    if (!table || table->rows() != 6 || table->columns() != 3
                        || summary->font().pointSizeF() < window.font().pointSizeF()) return fail("band_table_readability");
                    if (window.findChild<QLabel*>(QString("eegAnalysisLegend_%1").arg(page))->isVisible()) return fail("duplicate_band_legend");
                }
                for (auto* combo : window.findChildren<QComboBox*>()) {
                    if (!combo->isVisible() || combo->currentText().isEmpty()) continue;
                    QStyleOptionComboBox option; option.initFrom(combo); option.rect = combo->rect(); option.editable = combo->isEditable();
                    const QRect field = combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
                    if (field.width() < combo->fontMetrics().horizontalAdvance(combo->currentText())) return fail("combo_clipped:" + combo->objectName() + ":" + locale);
                }
                const QString dir = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
                if (!dir.isEmpty() && page == 0 && target.width() == 1280) {
                    QDir().mkpath(dir); window.grab().save(QDir(dir).filePath("text-" + locale + ".png"));
                }
                if (locale == "zh_CN" && page == 3 && target.width() == 1280)
                    shortLocaleMinimum = window.minimumHeight();
                ++checked;
            }
        }
    }
    if (!i18n.loadLocale("zh_CN", &error)) return fail(error);
    settle(); window.resize(1280, 800); settle();
    if (window.minimumHeight() > shortLocaleMinimum) return fail("long_locale_minimum_not_released");
    std::cout << "analysis-text-ok cases=" << checked << " no_scroll=true complete=true combos=true\n";
    return 0;
}

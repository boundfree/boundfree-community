#pragma once
#include "ui/main_window.h"
#include <QAbstractItemView>
#include <QAbstractItemDelegate>
#include <QStyleOptionViewItem>
#include <QDir>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QEventLoop>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <iostream>

inline int smokeReferenceLabels(int argc, char** argv, const bf::VersionInfo& version, const QString& repoRoot) {
    QApplication app(argc, argv);
    const QString requestedStyle = qEnvironmentVariable("BF_UI_STYLE");
    if (!requestedStyle.isEmpty() && !app.setStyle(requestedStyle)) return 77;
    bf::I18nService i18n(repoRoot);
    QString error;
    if (!i18n.loadLocale("zh_CN", &error)) return 1;
    bf::MainWindow window(version, &i18n, repoRoot);
    bf::FileOpenResult result;
    result.session.ok = true; result.session.path = "reference.csv";
    result.session.fs = 200; result.session.duration = 10;
    result.session.nChannels = 2; result.session.nSamples = 2000;
    result.session.chNames = {"a", "b"};
    result.waveform.fs = 200; result.waveform.t0 = 0; result.waveform.t1 = 10;
    result.waveform.channelNames = result.session.chNames;
    result.waveform.data = {std::vector<float>(2000), std::vector<float>(2000)};
    window.showFileOpenResult(result);
    window.resize(900, 560); window.show();
    auto* combo = window.findChild<QComboBox*>("eegReferenceMode");
    auto* toolbar = window.findChild<QWidget*>("eegFilterControls");
    if (!combo || !toolbar) return 1;
    const auto settle = []() { QEventLoop loop; QTimer::singleShot(25, &loop, &QEventLoop::quit); loop.exec(); };
    const auto fail = [&](const char* reason) {
        std::cerr << "reference-labels-failed " << reason << " locale=" << i18n.locale().toStdString()
                  << " text=" << combo->currentText().toStdString() << " width=" << combo->width() << '\n';
        return 1;
    };
    for (const QString& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW"), QString("pseudo")}) {
        if (!i18n.loadLocale(locale, &error)) return fail("locale");
        settle(); window.resize(900, 560); settle();
        for (const bool dark : {false, true}) {
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == i18n.text(dark ? "app.theme.dark" : "app.theme.light")) action->trigger();
            settle();
            // Compare outer editors/buttons, excluding the preset's embedded arrow.
            for (QWidget* region : {toolbar, window.findChild<QWidget*>("eegAnalysisPanel")}) {
                if (!region) return fail("missing_control_region");
                for (QWidget* control : region->findChildren<QWidget*>()) {
                    if (!control->isVisible()) continue;
                    if (qobject_cast<QComboBox*>(control) || qobject_cast<QAbstractSpinBox*>(control)
                        || qobject_cast<QPushButton*>(control) || control->property("controlBarAction").toBool()) {
                        if (std::abs(control->height() - combo->height()) > 1) {
                            std::cerr << "control " << control->objectName().toStdString() << " ("
                                      << control->metaObject()->className() << ") height=" << control->height()
                                      << " reference=" << combo->height() << " dark=" << dark << '\n';
                            return fail("unequal_control_height");
                        }
                    }
                }
            }
            for (int mode = 0; mode < combo->count(); ++mode) {
                combo->setCurrentIndex(mode); settle();
                QStyleOptionComboBox option;
                option.initFrom(combo); option.rect = combo->rect(); option.frame = combo->hasFrame();
                option.currentText = combo->currentText();
                const QRect field = combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
                if (combo->fontMetrics().horizontalAdvance(combo->currentText()) > field.width())
                    return fail("selected_text_clipped");
                const QRect group(combo->parentWidget()->mapTo(toolbar, QPoint()), combo->parentWidget()->size());
                if (!toolbar->rect().contains(group)) return fail("reference_group_does_not_wrap");
                combo->showPopup(); settle();
                auto* view = combo->view();
                int longestText = 0;
                for (int item = 0; item < combo->count(); ++item)
                    longestText = std::max(longestText, view->fontMetrics().horizontalAdvance(combo->itemText(item)));
                // Fluent combo boxes draw their own popup instead of showing view().
                QWidget* popup = view;
                for (QWidget* candidate : QApplication::allWidgets())
                    if (candidate->isVisible() && candidate->objectName() == "ComboBoxPopup") popup = candidate;
                // Compare the content area; Fluent flyouts reserve their shadow inside the widget.
                if (popup->contentsRect().width() > std::max(combo->width(), longestText + 64) + 16) {
                    std::cerr << "popup width=" << popup->contentsRect().width() << " combo=" << combo->width()
                              << " longest=" << longestText << " popup=" << popup->metaObject()->className() << '\n';
                    return fail("popup_width_grows_after_locale_change");
                }
                for (int item = 0; item < combo->count(); ++item) {
                    const auto index = combo->model()->index(item, combo->modelColumn(), combo->rootModelIndex());
                    const QRect row = view->visualRect(index);
                    QStyleOptionViewItem popupOption;
                    popupOption.initFrom(view);
                    if (view->itemDelegateForIndex(index)->sizeHint(popupOption, index).width() > row.width())
                        return fail("popup_delegate_text_clipped");
                    if (view->fontMetrics().horizontalAdvance(combo->itemText(item)) > row.width())
                        return fail("popup_text_clipped");
                }
                if (view->textElideMode() != Qt::ElideNone) return fail("popup_text_elided");
                const QString screenshotDir = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
                if (!screenshotDir.isEmpty() && !dark && mode == 0) {
                    QDir().mkpath(screenshotDir);
                    view->window()->grab().save(screenshotDir + "/popup-" + locale + "-" + requestedStyle + ".png");
                    window.grab().save(screenshotDir + "/controls-" + locale + "-" + requestedStyle + ".png");
                }
                combo->hidePopup();
            }
        }
    }
    std::cout << "reference-labels-ok locales=5 themes=2 modes=3 popup=true wrapping=true\n";
    return 0;
}

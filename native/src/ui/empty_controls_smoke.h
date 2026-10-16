#pragma once
#include "ui/main_window.h"
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStyleOptionSpinBox>
#include <QStyleFactory>
#include <QDir>
#include <iostream>

inline int smokeEmptyControls(int argc, char** argv, const bf::VersionInfo& version, const QString& repoRoot) {
    QApplication app(argc, argv);
    const QString requestedStyle = qEnvironmentVariable("BF_UI_STYLE");
    if (!requestedStyle.isEmpty() && !app.setStyle(requestedStyle)) {
        std::cerr << "requested Qt style unavailable\n"; return 77;
    }
    bf::I18nService i18n(repoRoot);
    QString error;
    if (!i18n.loadLocale("zh_CN", &error)) return 1;
    bf::MainWindow window(version, &i18n, repoRoot);
    window.resize(900, 560); window.show();
    const auto settle = []() { QEventLoop loop; QTimer::singleShot(25, &loop, &QEventLoop::quit); loop.exec(); };
    const auto fail = [](const char* reason) { std::cerr << "empty-controls-failed " << reason << '\n'; return 1; };
    settle();
    auto* sidebar = window.findChild<QSplitter*>("eegAnalysisSplitter");
    auto* logo = window.findChild<QLabel*>("brandLogo");
    auto* workspace = window.findChild<QSplitter*>("eegWorkspaceSplitter");
    if (!sidebar || sidebar->isVisible() || !logo || !workspace) return fail("empty_sidebar");
    if (std::abs(logo->mapTo(&window, logo->rect().center()).x()
        - workspace->mapTo(&window, workspace->rect().center()).x()) > 3) return fail("welcome_center");
    for (const QString& name : {QString("Channels"), QString("Markers"), QString("Analysis"), QString("Video")})
        if (window.findChild<QAction*>("eegToggle" + name)->isEnabled()) return fail("empty_view_action");
    window.showFileOpenLoading("missing.csv");
    window.showFileOpenError(bf::makeError("reader.slice_out_of_range", {}));
    settle();
    if (sidebar->isVisible()) return fail("failed_open_sidebar");
    bf::FileOpenResult result;
    result.session.ok = true; result.session.path = "controls.csv";
    result.session.fs = 100; result.session.duration = 100;
    result.session.nChannels = 1; result.session.nSamples = 10000;
    result.session.chNames = {"ch1"};
    result.waveform.fs = 100; result.waveform.t0 = 0; result.waveform.t1 = 10;
    result.waveform.channelNames = result.session.chNames;
    result.waveform.data = {std::vector<float>(1000)};
    window.showFileOpenResult(result); settle();
    if (!sidebar->isVisible()) return fail("loaded_sidebar");
    for (const QString& name : {QString("Channels"), QString("Markers"), QString("Analysis"), QString("Video")})
        if (!window.findChild<QAction*>("eegToggle" + name)->isEnabled()) return fail("loaded_view_action");
    auto* waveform = window.findChild<bf::WaveformWidget*>();
    window.showFileOpenLoading("missing-next.csv");
    window.showFileOpenError(bf::makeError("reader.slice_out_of_range", {}));
    settle();
    if (!sidebar->isVisible() || !waveform->isVisible()
        || waveform->cachedDisplayModel().channelNames != result.session.chNames) return fail("failed_reopen_loses_session");
    auto* windowSize = window.findChild<QDoubleSpinBox*>("eegWindowSeconds");
    auto* presetButton = window.findChild<QToolButton*>("eegWindowSecondsPresetButton");
    auto* presetMenu = window.findChild<QMenu*>("eegWindowSecondsPresetMenu");
    if (!windowSize || !presetButton || !presetMenu || !presetButton->isVisible()
        || windowSize->buttonSymbols() != QAbstractSpinBox::NoButtons) return fail("window_presets");
    QMetaObject::invokeMethod(presetMenu, "aboutToShow", Qt::DirectConnection);
    if (presetMenu->actions().size() != 8) return fail("preset_choices");
    presetMenu->actions()[1]->trigger();
    if (windowSize->value() != 2) return fail("preset_selection");
    windowSize->setFocus(); windowSize->selectAll();
    QKeyEvent typed(QEvent::KeyPress, 0, Qt::NoModifier, "7");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(windowSize, &typed); QApplication::sendEvent(windowSize, &enter);
    if (windowSize->value() != 7) return fail("preset_direct_input");
    windowSize->setValue(10);
    auto* spin = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
    for (const bool dark : {false, true}) {
        for (auto* action : window.findChildren<QAction*>())
            if (action->text() == i18n.text(dark ? "app.theme.dark" : "app.theme.light")) action->trigger();
        spin->setValue(50); settle();
        QStyleOptionSpinBox option;
        option.initFrom(spin); option.rect = spin->rect(); option.frame = true;
        option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
        const QImage screenshot = spin->grab().toImage();
        const qreal dpr = screenshot.devicePixelRatio();
        for (auto control : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown}) {
            const QRect rect = spin->style()->subControlRect(QStyle::CC_SpinBox, &option, control, spin);
            if (rect.width() < 20 || rect.height() < 10 || !spin->rect().contains(rect)) return fail("spin_button_geometry");
            const QColor arrow(dark ? "#DDE0FF" : "#333333");
            int ink = 0;
            const QRect inside = rect.adjusted(3, 2, -3, -2);
            for (int y = inside.top() * dpr; y <= inside.bottom() * dpr; ++y)
                for (int x = inside.left() * dpr; x <= inside.right() * dpr; ++x)
                    if (screenshot.pixelColor(x,y).rgb() == arrow.rgb()) ++ink;
            if (ink < 10) return fail("spin_arrow_pixels");
            const double before = spin->value();
            const QPointF local(rect.center());
            QMouseEvent press(QEvent::MouseButtonPress, local, spin->mapToGlobal(rect.center()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, local, spin->mapToGlobal(rect.center()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(spin, &press); QApplication::sendEvent(spin, &release);
            if ((control == QStyle::SC_SpinBoxUp && spin->value() <= before)
                || (control == QStyle::SC_SpinBoxDown && spin->value() >= before)) return fail("spin_button_click");
        }
    }
    const QString directory = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    if (!directory.isEmpty()) { QDir().mkpath(directory); window.grab().save(QDir(directory).filePath("controls.png")); }
    std::cout << "empty-controls-ok arrows=true clicks=true\n";
    return 0;
}

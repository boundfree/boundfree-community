#include "ui/eeg_separation_workspace.h"
#include "ui/export_segment_dialog.h"
#include "ui/collapsible_panel.h"
#include "ui/fluent_controls.h"
#include <QApplication>
#include <QDialog>
#include <QTabWidget>
#include <QWheelEvent>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <iostream>

int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    // Qt's file chooser remembers its directory in shared user settings. Keep
    // this process independent of prior runs and other native UI tests.
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid())
        return 1;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    bf::VersionInfo version;
    QString error;
    if (!bf::VersionService(BF_REPO_ROOT).load(&version, &error))
        return 1;
    bf::I18nService i18n(BF_REPO_ROOT);
    if (!i18n.loadLocale("zh_CN", &error))
        return 2;
    bf::EegSeparationController controller(BF_REPO_ROOT, version);
    bf::EegSeparationWorkspace ui(&controller, &i18n);
    ui.resize(1280, 900);
    ui.show();
    ui.setDarkTheme(false);
    app.processEvents();
    auto fail = [](const char *reason) {
        std::cerr << reason << '\n';
        return 1;
    };
    auto wait = [&](const std::function<bool()> &condition, int ms = 90000) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < ms) {
            app.processEvents();
            QThread::msleep(10);
        }
        app.processEvents();
        return condition();
    };
    QString failure;
    QObject::connect(&controller, &bf::EegSeparationController::failed,
                     [&](const bf::AppError &err) { failure = err.code; });
    auto *main = ui.findChild<QPushButton *>("separationMainAction");
    auto *confirm = ui.findChild<QCheckBox *>("separationConfirmed");
    auto *play = ui.findChild<QPushButton *>("separationPlay");
    auto *restart = ui.findChild<QPushButton *>("separationRestart");
    auto *playbackPosition = ui.findChild<QLabel *>("separationPlaybackPosition");
    if (!main || main->isEnabled() || !confirm || !play || !restart || !playbackPosition ||
        play->isEnabled() || restart->isEnabled() || ui.property("previewPlaying").toBool())
        return fail("empty state allows analysis");
    if (ui.findChild<QPushButton *>("separationBack"))
        return fail("workspace still duplicates persistent navigation with a back button");
    if (ui.findChild<QPushButton *>("separationOpenTeaching") || ui.findChild<QPushButton *>("separationOpen") || ui.findChild<QWidget *>("separationAlternativeInput"))
        return fail("file inputs duplicated inside workspace");
    auto *cancel = ui.findChild<QPushButton *>("separationCancel");
    auto *rerun = ui.findChild<QPushButton *>("separationRerun");
    auto *help = ui.findChild<QDialog*>("separationHelpDialog");
    auto *helpButton = ui.findChild<QPushButton*>("separationHelpButton");
    auto *helpClose = ui.findChild<QPushButton*>("separationHelpClose");
    auto *helpTabs = ui.findChild<QTabWidget*>("separationHelpTabs");
    auto *metadataButton = ui.findChild<QPushButton*>("separationMetadataButton");
    auto *advancedPanel = ui.findChild<bf::CollapsiblePanel *>("separationAdvanced");
    auto *status = ui.findChild<QLabel *>("separationStatus");
    if (!cancel || !rerun || !help || !helpButton || !helpClose || !helpTabs || !metadataButton || !advancedPanel || !status)
        return fail("teaching entry missing");
    auto* actionBar = ui.findChild<QWidget*>("separationActionBar");
    auto* liveToggle = ui.findChild<QPushButton*>("separationLiveToggle");
    auto* nextStep = ui.findChild<QLabel*>("separationNextStep");
    if (!actionBar || !liveToggle || !nextStep) return fail("workflow action bar missing");
    for (auto* action : {main, rerun, cancel, liveToggle, ui.findChild<QPushButton*>("separationRetry")})
        if (!action || !actionBar->isAncestorOf(action)) return fail("workflow actions are scattered");
    const QString capture = qEnvironmentVariable("BF_UI_CAPTURE_DIR");
    const auto captureUi = [&](const QString& name) {
        app.processEvents();
        return capture.isEmpty() || (QDir().mkpath(capture) && ui.grab().save(QDir(capture).filePath(name)));
    };
    if (ui.findChild<QPushButton*>("separationRoleHelpButton") ||
        ui.findChild<QPushButton*>("separationPlotHelpButton") || helpTabs->count() != 5)
        return fail("duplicate guide entries or missing component review tab");
    // Exercise real pointer events as well as signal-driven clicks. Each panel
    // must open and close repeatedly without resetting the user's settings.
    auto mouseClick = [&](QWidget *widget) {
        const QPointF local = widget->rect().center();
        const QPointF global = widget->mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &release);
        app.processEvents();
    };
    if (help->isVisible() || advancedPanel->isExpanded() || status->isVisible() || rerun->isVisible())
        return fail("empty workspace shows secondary details or result actions");
    // Check rendered semantic feedback in both themes, including recovery.
    const QString errorPath = QStringLiteral("/tmp/<occupied>/result");
    for (const bool dark : {false, true}) {
        ui.setDarkTheme(dark);
        ui.showInputError(bf::makeError("analysis.destination_exists", {{"path", errorPath}}, true));
        app.processEvents();
        const auto colors = bf::surfaceColors();
        const auto image = status->grab().toImage();
        int redPixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                // Include antialiased glyph edges; the red-tinted background
                // itself has much smaller differences between RGB channels.
                if (pixel.red() - pixel.green() > 40 && pixel.red() - pixel.blue() > 35) ++redPixels;
            }
        if (!captureUi(dark ? "separation-error-dark-1280x900.png" : "separation-error-light-1280x900.png"))
            return fail("cannot capture error feedback");
        if (status->property("feedbackTone") != "error" || redPixels < 10 ||
            status->palette().color(QPalette::WindowText) != colors.systemCritical ||
            status->textFormat() != Qt::PlainText ||
            status->accessibleDescription() != status->text()) {
            std::cerr << "feedback dark=" << dark << " red_pixels=" << redPixels << " expected=" << colors.systemCritical.name().toStdString()
                << " actual=" << status->palette().color(QPalette::WindowText).name().toStdString()
                << " text=" << status->text().toStdString() << '\n';
            return fail("failure is not clearly rendered in red");
        }
        ui.setInputPending(true);
        if (status->property("feedbackTone") != "info" || !status->toolTip().isEmpty() ||
            !cancel->property("primaryAction").toBool()) return fail("recovery retains stale error feedback");
        ui.setInputPending(false);
        if (status->isVisible()) return fail("recovery retains error notice");
    }
    ui.setDarkTheme(false);
    for (int attempt = 0; attempt < 2; ++attempt) {
        mouseClick(helpButton);
        if (!help->isVisible() || help->isModal() || QApplication::activeModalWidget() || helpTabs->currentIndex() != 0)
            return fail("help is not conveniently available without blocking the workspace");
        mouseClick(helpClose);
        if (help->isVisible()) return fail("help cannot be closed repeatedly");
        ui.openTeachingSource();
        if (!controller.isBusy() || !cancel->isEnabled() || ui.canChangeInput())
            return fail("inspection does not expose a usable cancel action");
        cancel->click();
        if (controller.isBusy() || !ui.canChangeInput() || main->isEnabled())
            return fail("cancelled inspection leaves actions disabled");
        if (status->property("feedbackTone") == "error") return fail("user cancellation is presented as a failure");
    }
    ui.openTeachingSource();
    if (!wait([&] { return !controller.isBusy(); }) || controller.source().channels.size() != 5 ||
        !failure.isEmpty()) {
        std::cerr << failure.toStdString() << '\n';
        return fail("teaching source open failed");
    }
    ui.openTeachingSource();
    if (!wait([&] { return !controller.isBusy(); }) || !main->isEnabled() || !failure.isEmpty())
        return fail("teaching record cannot be opened a second time");
    const auto readySettings = controller.settings();
    auto excluded = readySettings;
    std::fill(excluded.roles.begin(), excluded.roles.end(), 4);
    controller.setSettings(excluded);
    if (main->isEnabled() || nextStep->property("feedbackTone") != "warning" ||
        !nextStep->text().contains(i18n.text("app.separation.next_observations")))
        return fail("observation requirement is not explained as a warning");
    if (!captureUi("separation-validation-1280x900.png")) return fail("cannot capture validation guidance");
    controller.setSettings(readySettings);
    if (!main->isEnabled() || !main->property("primaryAction").toBool() || nextStep->property("feedbackTone") != "info")
        return fail("ready model action has no primary emphasis");
    if (!captureUi("separation-ready-1280x900.png")) return fail("cannot capture model action");
    for (int attempt = 0; attempt < 2; ++attempt) {
        mouseClick(advancedPanel->headerButton());
        if (!advancedPanel->isExpanded()) return fail("settings cannot be opened repeatedly");
        mouseClick(advancedPanel->headerButton());
        if (advancedPanel->isExpanded()) return fail("settings cannot be closed repeatedly");
        main->click();
        if (!controller.isBusy() || !cancel->isEnabled() || main->isEnabled())
            return fail("running analysis has incorrect action availability");
        cancel->click();
        if (controller.isBusy() || !main->isEnabled() || controller.output())
            return fail("cancelled analysis cannot be started again");
    }
    auto *table = ui.findChild<QTableWidget *>("separationChannels");
    auto *bulk = ui.findChild<QComboBox *>("separationBulkRole");
    auto *bulkActions = ui.findChild<QWidget *>("separationBulkActions");
    auto *bulkCount = ui.findChild<QLabel *>("separationBulkSelection");
    auto *applyRole = ui.findChild<QPushButton *>("separationApplyRole");
    auto *role = table ? qobject_cast<QComboBox *>(table->cellWidget(0, 1)) : nullptr;
    if (!table || !role || !bulk || !bulkActions || !bulkCount || !applyRole || !main->isEnabled())
        return fail("teaching roles not prefilled");
    for (int index = 0; index < 5; ++index)
        if (role->itemData(index, Qt::ToolTipRole).toString().isEmpty() ||
            role->itemData(index, Qt::AccessibleDescriptionRole).toString().isEmpty())
            return fail("role choices have no contextual explanation");
    if (!role->toolTip().contains(i18n.text("app.separation.role_help_1")))
        return fail("selected role tooltip does not explain its effect");
    mouseClick(helpButton);
    helpTabs->setCurrentIndex(1);
    if (!help->isVisible() || helpTabs->currentIndex() != 1) return fail("role guide entry");
    mouseClick(metadataButton);
    auto* acquisitionSummary = ui.findChild<QLabel*>("separationAcquisitionSummary");
    if (helpTabs->currentIndex() != 3 || !acquisitionSummary ||
        !acquisitionSummary->text().contains("500") || !acquisitionSummary->text().contains("teaching.h5"))
        return fail("acquisition information does not describe the active input");
    mouseClick(helpClose);
    table->clearSelection();
    if (bulkActions->isVisible() || bulk->isEnabled() || applyRole->isEnabled())
        return fail("batch actions shown without multiple selected channels");
    role->setCurrentIndex(0);
    if (main->isEnabled())
        return fail("unconfirmed role accepted");
    table->selectRow(0);
    if (bulkActions->isVisible() || bulk->isEnabled() || applyRole->isEnabled())
        return fail("single channel duplicates inline role editor with batch actions");
    role->setCurrentIndex(1);
    table->selectionModel()->select(table->model()->index(1, 0),
                                   QItemSelectionModel::Select | QItemSelectionModel::Rows);
    app.processEvents();
    if (!bulkActions->isVisible() || !bulk->isEnabled() || !applyRole->isEnabled() ||
        bulkCount->text().isEmpty() || !bulkCount->text().contains("2"))
        return fail("multiple selection does not reveal contextual batch assignment");
    bulk->setCurrentIndex(4);
    applyRole->click();
    if (controller.settings().roles[0] != 4 || controller.settings().roles[1] != 4 ||
        controller.settings().roles[2] != 1 || main->isEnabled())
        return fail("batch assignment does not affect exactly selected channels");
    bulk->setCurrentIndex(1);
    applyRole->click();
    if (!main->isEnabled())
        return fail("batch role assignment failed");
    table->clearSelection();
    if (bulkActions->isVisible() || bulk->isEnabled() || applyRole->isEnabled())
        return fail("batch actions remain after clearing channel selection");
    auto *splitter = ui.findChild<QSplitter *>("separationSplitter");
    if (!splitter || splitter->handleWidth() < 12 || splitter->count() != 3)
        return fail("resizable panes lack a discoverable drag handle");
    const auto initialPaneSizes = splitter->sizes();
    splitter->setSizes({390, 480, 340});
    app.processEvents();
    if (splitter->widget(0)->maximumWidth() <= 345 || splitter->widget(0)->width() <= 345)
        return fail("input pane still cannot expand past its former width cap");
    auto *handle = splitter->handle(1);
    if (!handle || !handle->isVisible() || handle->width() < 12 ||
        handle->cursor().shape() != Qt::SplitHCursor || handle->toolTip().isEmpty() ||
        handle->accessibleName().isEmpty())
        return fail("splitter handle lacks visual or accessible drag affordance");
    const int beforeDrag = splitter->sizes()[0];
    const QPointF local = handle->rect().center();
    const QPointF global = handle->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(handle, &press);
    QMouseEvent move(QEvent::MouseMove, local + QPointF(35, 0), global + QPointF(35, 0),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, local + QPointF(35, 0), global + QPointF(35, 0),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &release);
    app.processEvents();
    if (splitter->sizes()[0] <= beforeDrag)
        return fail("dragging visible splitter handle does not resize panes");
    const int beforeKeyboard = splitter->sizes()[0];
    handle->setFocus();
    QKeyEvent rightKey(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(handle, &rightKey);
    app.processEvents();
    if (splitter->sizes()[0] <= beforeKeyboard)
        return fail("focused splitter handle cannot resize panes with arrow keys");
    splitter->setSizes(initialPaneSizes);
    auto *start = ui.findChild<QDoubleSpinBox *>("separationStart");
    start->setValue(15.0);
    if (main->isEnabled())
        return fail("interval extending past source is accepted");
    start->setValue(0.0);
    main->click();
    if (!wait([&] { return !controller.isBusy(); }) || !controller.output() || !failure.isEmpty()) {
        std::cerr << failure.toStdString() << '\n';
        return fail("real separation failed");
    }
    if (main->isEnabled() || confirm->isChecked())
        return fail("export bypasses manual confirmation");
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!rerun->isVisible() || !rerun->isEnabled())
            return fail("completed analysis cannot be run again");
        const auto previousOutput = controller.output();
        confirm->setChecked(true);
        rerun->click();
        if (!controller.isBusy() || rerun->isEnabled() || confirm->isChecked())
            return fail("rerun does not clear old confirmation and disable in-flight actions");
        if (!wait([&] { return !controller.isBusy(); }) || !controller.output() ||
            controller.output() == previousOutput || !failure.isEmpty() ||
            !rerun->isEnabled() || main->isEnabled())
            return fail("repeated analysis does not produce a fresh reviewable result");
    }
    auto *componentTable = ui.findChild<QTableWidget *>("separationComponents");
    auto *labels = ui.findChild<QRadioButton*>("separationClassify0");
    if (!labels || controller.labels()[0] != 3 || !componentTable->findChildren<QComboBox*>().isEmpty())
        return fail("initial classification or dropdown-free list");
    const auto chooseLabel = [&](int group) {
        componentTable->setCurrentCell(0, 0);
        mouseClick(ui.findChild<QRadioButton*>(QString("separationClassify%1").arg(group)));
    };
    for (int group = 0; group < 4; ++group) {
        auto* button = ui.findChild<QRadioButton*>(QString("separationClassify%1").arg(group));
        if (!button || button->toolTip() != i18n.text(QString("app.separation.group_help_%1").arg(group)) ||
            button->accessibleDescription().isEmpty()) return fail("classification button lacks explanation");
    }
    auto* categories = ui.findChild<QButtonGroup*>("separationClassificationChoices");
    if (!categories || !categories->exclusive() || categories->checkedId()!=3)
        return fail("classification choices are not an exclusive radio group");
    const auto reviewOutput = controller.output();
    const auto reviewLabels = controller.labels();
    componentTable->setCurrentCell(1, 0);
    auto* selectedTrace = ui.findChild<QWidget*>("separationComponentTrace");
    mouseClick(helpButton);
    auto* reviewGuide = ui.findChild<QLabel*>("separationReviewHelp");
    auto* reviewHint = ui.findChild<QLabel*>("separationReviewHint");
    if (!selectedTrace || selectedTrace->property("selectedComponent").toInt() != 1 ||
        !selectedTrace->accessibleName().contains("IC 2") || !reviewGuide || !reviewHint ||
        !reviewHint->isVisible() || !reviewGuide->text().contains(i18n.text("app.separation.group_help_3")) ||
        helpTabs->currentIndex() != 4 || help->isModal() || controller.output() != reviewOutput ||
        controller.labels() != reviewLabels)
        return fail("component selection or contextual review guide changes analysis");
    mouseClick(helpClose);
    componentTable->setCurrentCell(0, 0);
    const auto before = controller.output();
    play->click();
    chooseLabel(0);
    if (ui.property("previewPlaying").toBool())
        return fail("component label editing leaves preview playback running");
    if (ui.findChild<QRadioButton*>("separationClassify0") != labels)
        return fail("active label editor was destroyed");
    if (controller.output() != before || controller.labels()[0] != 0 || categories->checkedId()!=0 || confirm->isChecked())
        return fail("label changes refit or fail");
    ui.findChild<QPushButton *>("separationUndo")->click();
    if (controller.labels()[0] != 3)
        return fail("undo failed");
    confirm->setChecked(true);
    if (!main->isEnabled())
        return fail("unresolved cannot be confirmed");
    chooseLabel(1);
    if (confirm->isChecked() || main->isEnabled())
        return fail("label edit does not revoke confirmation");
    auto *exampleLabels = ui.findChild<QPushButton *>("separationTeachingLabels");
    const auto beforeExample = controller.labels();
    exampleLabels->click();
    const auto example = controller.labels();
    exampleLabels->click();
    if (controller.labels() != example || controller.output() != before)
        return fail("reapplying teaching labels refits or changes the demonstrated grouping");
    ui.findChild<QPushButton *>("separationUndo")->click();
    if (controller.labels() != beforeExample)
        return fail("repeated example application adds a redundant undo step");
    exampleLabels->click();
    auto* componentViews = ui.findChild<QTabWidget*>("separationComponentViews");
    if (!componentViews || componentViews->count() != 3 || componentViews->currentIndex() != 0)
        return fail("component preview tabs missing");
    for (int tab : {1,2,0}) { componentViews->setCurrentIndex(tab); app.processEvents(); }
    auto* nextIc = ui.findChild<QPushButton*>("separationNextComponent");
    auto* previousIc = ui.findChild<QPushButton*>("separationPreviousComponent");
    mouseClick(nextIc);
    if (componentTable->currentRow() != 1 || selectedTrace->property("selectedComponent").toInt() != 1)
        return fail("next IC does not change preview");
    const auto componentCapture = qEnvironmentVariable("BF_UI_CAPTURE_DIR");
    if (!componentCapture.isEmpty()) QDir().mkpath(componentCapture);
    if (!componentCapture.isEmpty() && !ui.grab().save(QDir(componentCapture).filePath("component-ic2.png")))
        return fail("IC2 screenshot");
    mouseClick(nextIc);
    if (componentTable->currentRow() != 2 || selectedTrace->property("selectedComponent").toInt() != 2 || nextIc->isEnabled())
        return fail("last IC navigation or boundary");
    if (!componentCapture.isEmpty() && !ui.grab().save(QDir(componentCapture).filePath("component-ic3.png")))
        return fail("IC3 screenshot");
    mouseClick(previousIc);
    mouseClick(previousIc);
    if (componentTable->currentRow() != 0 || selectedTrace->property("selectedComponent").toInt() != 0)
        return fail("previous IC does not change preview");
    confirm->setChecked(true);
    auto *amplitudeMode = ui.findChild<QComboBox *>("separationAmplitudeMode");
    auto *amplitudeZoom = ui.findChild<QComboBox *>("separationAmplitudeZoom");
    auto *groupTraces = ui.findChild<QWidget *>("separationGroupTraces");
    auto *previewChannel = ui.findChild<QComboBox *>("separationPreviewChannel");
    auto *previewPosition = ui.findChild<QSlider *>("separationPreviewPosition");
    if (!amplitudeMode || !amplitudeZoom || !groupTraces || !previewChannel || !previewPosition ||
        amplitudeMode->count() != 2 || amplitudeMode->currentIndex() != 0 ||
        amplitudeZoom->count() < 4 || amplitudeZoom->currentData().toInt() != 1)
        return fail("amplitude controls missing or defaults do not preserve shared comparison");
    for (const int gain : {1, 2, 5, 10})
        if (amplitudeZoom->findData(gain) < 0)
            return fail("amplitude control lacks required zoom factor");
    restart->click();
    const auto displayOutput = controller.output();
    const auto displaySettings = controller.settings();
    const auto displayLabels = controller.labels();
    const auto displayState = controller.state();
    auto displayIsNonDestructive = [&] {
        const auto &settings = controller.settings();
        return controller.output() == displayOutput && controller.labels() == displayLabels &&
               controller.isConfirmed() && confirm->isChecked() && controller.state() == displayState &&
               settings.roles == displaySettings.roles && settings.start == displaySettings.start &&
               settings.duration == displaySettings.duration && settings.outputRate == displaySettings.outputRate &&
               settings.notchHz == displaySettings.notchHz;
    };
    // Independently measure the exact samples visible in the default 10 s window.
    // Comparing exported properties to the controller arrays catches accidental
    // use of all-record extrema, source IC units, or screen-scaled amplitudes.
    auto visiblePeakToPeakMatches = [&] {
        app.processEvents();
        groupTraces->repaint();
        const auto values = groupTraces->property("visiblePeakToPeak").toList();
        const auto small = groupTraces->property("smallWaveGroups").toList();
        const auto clipped = groupTraces->property("clippedGroups").toList();
        const auto scales = groupTraces->property("visibleScaleLimits").toList();
        if (values.size() != 5 || small.size() != 5 || clipped.size() != 5 || scales.size() != 5)
            return false;
        const int channel = previewChannel->currentIndex();
        const auto &input = displayOutput->analysis[channel];
        const double full = input.size() / displayOutput->fs;
        const double view = std::min(10.0, full);
        const double cursor = ui.property("previewPositionSeconds").toDouble();
        const double offset = std::clamp(cursor - view / 2, 0.0, std::max(0.0, full - view));
        if (std::abs(groupTraces->property("previewOffsetSeconds").toDouble() - offset) > 1e-9 ||
            std::abs(groupTraces->property("previewCursorSeconds").toDouble() - cursor) > 1e-9)
            return false;
        const auto begin = static_cast<std::size_t>(offset * displayOutput->fs);
        const auto end = std::min(input.size(), begin + static_cast<std::size_t>(view * displayOutput->fs));
        std::array<double, 5> maxima{}, fullMaxima{}, ranges{};
        for (int group = 0; group < 5; ++group) {
            const auto &matrix = group == 0 ? displayOutput->analysis : controller.projection(group - 1);
            if (channel < static_cast<int>(matrix.size()) && end > begin) {
                const auto limits = std::minmax_element(matrix[channel].begin() + begin,
                                                       matrix[channel].begin() + end);
                ranges[group] = *limits.second - *limits.first;
                maxima[group] = std::max(std::abs(*limits.first), std::abs(*limits.second));
                const auto fullLimits = std::minmax_element(matrix[channel].begin(), matrix[channel].end());
                fullMaxima[group] = std::max(std::abs(*fullLimits.first), std::abs(*fullLimits.second));
            }
            const double expected = ranges[group];
            if (std::abs(values[group].toDouble() - expected) > 1e-10 * std::max(1.0, expected))
                return false;
        }
        const double common = *std::max_element(fullMaxima.begin(), fullMaxima.end());
        for (int group = 0; group < 5; ++group) {
            const bool empty = group > 0 &&
                               std::find(displayLabels.begin(), displayLabels.end(), group - 1) == displayLabels.end();
            const double maximum = amplitudeMode->currentIndex() == 0 ? common : fullMaxima[group];
            const double scale = (maximum < 1e-12 ? 1.0 : maximum) / amplitudeZoom->currentData().toDouble();
            const bool expectedSmall = !empty && maxima[group] > 1e-12 && ranges[group] / (2 * scale) < .02;
            const bool expectedClip = !empty && maxima[group] > scale * (1 + 1e-9);
            if (std::abs(scales[group].toDouble() - scale) > 1e-10 * std::max(1.0, scale) ||
                small[group].toBool() != expectedSmall || clipped[group].toBool() != expectedClip)
                return false;
        }
        return true;
    };
    if (!visiblePeakToPeakMatches())
        return fail("group peak-to-peak differs from visible physical microvolt samples");
    for (const int position : {0, 250, 500, 750, 1000}) {
        previewPosition->setValue(position);
        for (const int channel : {0, 1}) {
            previewChannel->setCurrentIndex(channel);
            for (const int mode : {0, 1}) {
                amplitudeMode->setCurrentIndex(mode);
                for (const int gain : {1, 2, 5, 10}) {
                    amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(gain));
                    const double expectedCursor = displayOutput->analysis[0].size() / displayOutput->fs * position / 1000.;
                    if (!visiblePeakToPeakMatches() || !displayIsNonDestructive() ||
                        std::abs(ui.property("previewPositionSeconds").toDouble() - expectedCursor) > 1e-9) {
                        std::cerr << "display validation: position=" << position << " channel=" << channel
                                  << " mode=" << mode << " gain=" << gain << " cursor="
                                  << ui.property("previewPositionSeconds").toDouble()
                                  << " expected_cursor=" << expectedCursor << '\n';
                        return fail("display mode/zoom changes analysis or reports scaled peak-to-peak");
                    }
                }
            }
        }
    }
    amplitudeMode->setCurrentIndex(0);
    amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(10));
    app.processEvents();
    groupTraces->repaint();
    const auto clipped = groupTraces->property("clippedGroups").toList();
    if (std::none_of(clipped.begin(), clipped.end(), [](const QVariant &value) { return value.toBool(); }))
        return fail("zoomed shared scale fails to warn about clipped waveforms");
    amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(1));
    previewChannel->setCurrentIndex(0);
    previewPosition->setValue(0);
    if (!visiblePeakToPeakMatches() || !displayIsNonDestructive())
        return fail("resetting display controls changes reviewed analysis");
    // Exercise the real plot events; display operations must retain the reviewed model and export settings.
    auto* displayWindow = ui.findChild<QComboBox*>("separationDisplayWindow");
    const auto keyOnPlot = [&](int key) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(groupTraces, &event); app.processEvents();
    };
    const auto wheelOnPlot = [&](int delta, Qt::KeyboardModifiers modifiers) {
        groupTraces->grab();
        const auto area = groupTraces->property("plotArea").toRectF();
        const QPointF at(area.left() + area.width() * .6, groupTraces->height() * .4);
        QWheelEvent event(at, groupTraces->mapToGlobal(at.toPoint()), QPoint(), QPoint(0,delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(groupTraces, &event); app.processEvents();
    };
    const int windowItems = displayWindow->count(), gainItems = amplitudeZoom->count();
    const double anchorBefore = groupTraces->property("previewOffsetSeconds").toDouble() + displayWindow->currentData().toDouble() * .6;
    wheelOnPlot(120, Qt::ControlModifier);
    const double anchorAfter = groupTraces->property("previewOffsetSeconds").toDouble() + displayWindow->currentData().toDouble() * .6;
    if (displayWindow->currentData().toDouble() >= 10 || std::abs(anchorBefore-anchorAfter) > 1e-6)
        return fail("plot time zoom does not preserve the pointer anchor");
    wheelOnPlot(120, Qt::ShiftModifier);
    if (std::abs(amplitudeZoom->currentData().toDouble() - 1.25) > 1e-9 || !displayIsNonDestructive())
        return fail("plot amplitude zoom changes the reviewed analysis");
    for (int i=0;i<6;++i) wheelOnPlot(480,Qt::ControlModifier);
    if (displayWindow->currentData().toDouble() < .2 - 1e-9) return fail("time zoom lower bound");
    for (int i=0;i<10;++i) wheelOnPlot(-480,Qt::ShiftModifier);
    if (amplitudeZoom->currentData().toDouble() < .25 || displayWindow->count()>windowItems+1 || amplitudeZoom->count()>gainItems+1)
        return fail("zoom bounds or custom-value growth");
    keyOnPlot(Qt::Key_0);
    keyOnPlot(Qt::Key_Right);
    if (groupTraces->property("previewOffsetSeconds").toDouble() <= 0) return fail("plot keyboard pan");
    keyOnPlot(Qt::Key_0);
    const QPointF dragAt(groupTraces->width()*.6,groupTraces->height()*.5);
    const QPointF dragGlobal=groupTraces->mapToGlobal(dragAt.toPoint());
    QMouseEvent dragPress(QEvent::MouseButtonPress,dragAt,dragGlobal,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dragMove(QEvent::MouseMove,dragAt-QPointF(40,0),dragGlobal-QPointF(40,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dragRelease(QEvent::MouseButtonRelease,dragAt-QPointF(40,0),dragGlobal-QPointF(40,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(groupTraces,&dragPress);QApplication::sendEvent(groupTraces,&dragMove);QApplication::sendEvent(groupTraces,&dragRelease);
    if (groupTraces->property("previewOffsetSeconds").toDouble()<=0 || !displayIsNonDestructive()) return fail("plot drag pan");
    keyOnPlot(Qt::Key_0);restart->click();
    if (displayWindow->currentData().toDouble()!=10 || amplitudeZoom->currentData().toDouble()!=1 || !displayIsNonDestructive())
        return fail("plot reset changes analysis");
    const auto guidanceCapture = qEnvironmentVariable("BF_UI_CAPTURE_DIR");
    if (!guidanceCapture.isEmpty()) {
        QDir().mkpath(guidanceCapture);
        for (int tab : {1,2,3,4}) {
            helpTabs->setCurrentIndex(tab); mouseClick(helpButton);helpTabs->setCurrentIndex(tab);app.processEvents();
            if (!help->grab().save(QDir(guidanceCapture).filePath(QString("guidance-%1-zhCN.png").arg(tab)))) return fail("guidance screenshot");
            mouseClick(helpClose);
        }
    }
    auto *componentTrace = ui.findChild<QWidget *>("separationComponentTrace");
    auto *spectrum = ui.findChild<QWidget *>("separationSpectrum");
    auto *channelLabel = ui.findChild<QLabel *>("separationPreviewChannelLabel");
    auto *windowLabel = ui.findChild<QLabel *>("separationDisplayWindowLabel");
    auto *zoomLabel = ui.findChild<QLabel *>("separationWaveformZoomLabel");
    if (!componentTrace || !spectrum || !componentTable || !channelLabel || !windowLabel || !zoomLabel ||
        !play->isEnabled() || !restart->isEnabled() || playbackPosition->text().isEmpty())
        return fail("result preview transport, explicit labels, or synchronized traces are absent");
    // IC wheel events share time with the groups and retain a separate amplitude gain.
    auto* views = ui.findChild<QTabWidget*>("separationComponentViews");
    auto wheelOnComponent = [&](int delta, Qt::KeyboardModifiers modifiers) {
        componentTrace->grab();
        const auto area = componentTrace->property("plotArea").toRectF();
        const QPointF at(area.left() + area.width() * .6, area.center().y());
        QWheelEvent event(at, componentTrace->mapToGlobal(at.toPoint()), QPoint(), QPoint(0, delta),
                          Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(componentTrace, &event); app.processEvents(); componentTrace->grab(); groupTraces->grab();
    };
    componentTrace->grab();
    const auto initialTime = componentTrace->property("axisXRange").toList();
    const double icAnchor = initialTime[0].toDouble() + .6 * (initialTime[1].toDouble() - initialTime[0].toDouble());
    const auto initialIcScale = componentTrace->property("visibleScaleLimits").toList()[0].toDouble();
    const double groupGain = amplitudeZoom->currentData().toDouble();
    wheelOnComponent(120, Qt::ControlModifier);
    const auto zoomTime = componentTrace->property("axisXRange").toList();
    if (std::abs(zoomTime[0].toDouble() + .6 * (zoomTime[1].toDouble() - zoomTime[0].toDouble()) - icAnchor)>1e-6 ||
        displayWindow->currentData().toDouble() >= 10 ||
        componentTrace->property("axisXRange") != groupTraces->property("axisXRange"))
        return fail("IC wheel does not synchronize time axes");
    wheelOnComponent(120, Qt::ShiftModifier);
    if (std::abs(componentTrace->property("visibleScaleLimits").toList()[0].toDouble() * 1.25 - initialIcScale) > 1e-9 ||
        amplitudeZoom->currentData().toDouble() != groupGain || !displayIsNonDestructive())
        return fail("IC amplitude wheel changes group gain or scientific output");
    const auto timeTicks = componentTrace->property("axisXTickValues").toList();
    const auto yRange = componentTrace->property("axisYRanges").toList()[0].toList();
    if (timeTicks.size() < 2 || timeTicks.first().toDouble() >= timeTicks.last().toDouble() ||
        yRange[0].toDouble() >= 0 || yRange[1].toDouble() <= 0 ||
        !componentTrace->property("axisYLabel").toString().contains("a.u.") ||
        !componentTrace->property("axisXLabel").toString().contains("s"))
        return fail("IC axes omit coordinates or arbitrary units");
    for (int i=0;i<10;++i) wheelOnComponent(480,Qt::ShiftModifier);
    if (std::abs(componentTrace->property("visibleScaleLimits").toList()[0].toDouble() * 16 - initialIcScale) > 1e-9)
        return fail("IC amplitude upper bound");
    for (int i=0;i<15;++i) wheelOnComponent(-480,Qt::ShiftModifier);
    if (std::abs(componentTrace->property("visibleScaleLimits").toList()[0].toDouble() * .25 - initialIcScale) > 1e-9)
        return fail("IC amplitude lower bound");
    QKeyEvent resetIc(QEvent::KeyPress, Qt::Key_0, Qt::NoModifier);
    QApplication::sendEvent(componentTrace,&resetIc);app.processEvents();componentTrace->grab();
    if (displayWindow->currentData().toDouble()!=10 ||
        std::abs(componentTrace->property("visibleScaleLimits").toList()[0].toDouble() - initialIcScale)>1e-9 ||
        !displayIsNonDestructive()) return fail("IC reset changes reviewed analysis");
    views->setCurrentIndex(1);app.processEvents();spectrum->grab();
    const auto psdRange = spectrum->property("axisYRanges").toList()[0].toList();
    const auto frequencyRange = spectrum->property("axisXRange").toList();
    if (psdRange[0].toDouble()!=0 || psdRange[1].toDouble()<=0 || frequencyRange[0].toDouble()!=0 ||
        frequencyRange[1].toDouble()!=100 || !spectrum->property("axisYLabel").toString().contains("a.u.²/Hz"))
        return fail("PSD axes must show frequency and nonnegative power density");
    const auto axesCapture=qEnvironmentVariable("BF_UI_CAPTURE_DIR");
    if (!axesCapture.isEmpty()) spectrum->grab().save(QDir(axesCapture).filePath("component-spectrum-axes.png"));
    views->setCurrentIndex(0);app.processEvents();
    if (!axesCapture.isEmpty()) {
        componentTrace->grab().save(QDir(axesCapture).filePath("component-waveform-axes.png"));
        componentTable->parentWidget()->grab().save(QDir(axesCapture).filePath("component-panel-options-axes.png"));
    }
    auto precisePosition = [&] { return ui.property("previewPositionSeconds").toDouble(); };
    auto isPlaying = [&] { return ui.property("previewPlaying").toBool(); };
    auto synchronizedCursors = [&] {
        app.processEvents();
        return std::abs(groupTraces->property("previewCursorSeconds").toDouble() - precisePosition()) < 1e-9 &&
               std::abs(componentTrace->property("previewCursorSeconds").toDouble() - precisePosition()) < 1e-9 &&
               std::abs(componentTrace->property("previewOffsetSeconds").toDouble() -
                        groupTraces->property("previewOffsetSeconds").toDouble()) < 1e-9;
    };
    // A stalled UI must catch up to real elapsed time rather than advancing by
    // one nominal timer interval. This distinguishes true 1x playback from
    // counting delivered QTimer events under repaint or window-manager load.
    restart->click();
    if (precisePosition() != 0 || isPlaying())
        return fail("restart does not pause at the beginning");
    const auto captions = groupTraces->property("groupDescriptions").toStringList();
    if (captions.size() != 5) return fail("group captions missing");
    for (int group = 0; group < 4; ++group) {
        if (!captions[group + 1].contains(previewChannel->currentText())) return fail("group caption omits observation channel");
        for (int ic = 0; ic < int(controller.labels().size()); ++ic)
            if (controller.labels()[ic] == group && !captions[group + 1].contains(QString("IC %1").arg(ic + 1)))
                return fail("group caption omits contributing IC");
    }
    const auto spectrumBeforePlayback = spectrum->grab().toImage();
    QElapsedTimer playbackWall;
    playbackWall.start();
    play->click();
    if (!isPlaying())
        return fail("play action does not start result preview");
    QThread::msleep(240);
    app.processEvents();
    const double advanced = precisePosition();
    const double elapsed = playbackWall.elapsed() / 1000.;
    if (advanced < .18 || advanced > elapsed + .08 || advanced < elapsed - .12 ||
        !synchronizedCursors() || !displayIsNonDestructive())
        return fail("preview is not synchronized real-time 1x playback");
    play->click();
    const double pausedAt = precisePosition();
    QElapsedTimer pauseWall;
    pauseWall.start();
    if (!wait([&] { return pauseWall.elapsed() >= 120; }, 1000) || isPlaying() ||
        precisePosition() != pausedAt || spectrum->grab().toImage() != spectrumBeforePlayback)
        return fail("pause advances the cursor or playback changes full-interval component spectrum");
    play->click();
    ui.pausePreview();
    if (isPlaying() || !displayIsNonDestructive())
        return fail("public preview pause changes the reviewed analysis");
    previewPosition->setValue(750);
    if (std::abs(precisePosition() - 12.) > 1e-9 ||
        std::abs(groupTraces->property("previewOffsetSeconds").toDouble() - 6.) > 1e-9)
        return fail("seeking addresses cursor time instead of the centered visible window");
    play->click();
    QKeyEvent seekRight(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(previewPosition, &seekRight);
    if (isPlaying() || !synchronizedCursors())
        return fail("user seek does not immediately pause synchronized playback");
    for (auto *combo : {previewChannel, amplitudeMode, amplitudeZoom}) {
        play->click();
        combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
        if (isPlaying() || !displayIsNonDestructive())
            return fail("display adjustment does not pause for non-destructive inspection");
    }
    play->click();
    componentTable->setCurrentCell((componentTable->currentRow() + 1) % componentTable->rowCount(), 0);
    if (isPlaying() || !displayIsNonDestructive())
        return fail("selecting another component does not pause for inspection");
    play->click();
    const auto beforeHide = controller.output();
    ui.hide();
    app.processEvents();
    if (isPlaying() || controller.output() != beforeHide)
        return fail("hiding the workspace leaves playback active or discards its result");
    ui.show();
    app.processEvents();
    if (isPlaying() || !displayIsNonDestructive())
        return fail("showing the workspace unexpectedly resumes or recomputes analysis");
    previewPosition->setValue(999);
    play->click();
    if (!wait([&] { return !isPlaying(); }, 1500) || std::abs(precisePosition() - 16.) > 1e-9 ||
        previewPosition->value() != 1000 || !synchronizedCursors() ||
        std::abs(groupTraces->property("previewOffsetSeconds").toDouble() - 6.) > 1e-9)
        return fail("end of playback does not pause on the final visible window");
    play->click();
    if (!isPlaying() || precisePosition() > .2)
        return fail("playing at the end does not restart from the beginning");
    restart->click();
    amplitudeMode->setCurrentIndex(0);
    amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(1));
    previewChannel->setCurrentIndex(0);
    componentTable->setCurrentCell(0, 0);
    if (isPlaying() || precisePosition() != 0 || !displayIsNonDestructive())
        return fail("restart changes reviewed data or retains playback state");
    for (const auto &locale : QStringList{"zh_CN", "en_US", "ja_JP", "zh_TW", "pseudo"}) {
        if (!i18n.loadLocale(locale, &error))
            return fail("locale unavailable");
        ui.setDarkTheme(locale == "pseudo");
        app.processEvents();
        if (ui.width() != 1280 || ui.height() != 900)
            return fail("Qt expanded requested 1280x900 window");
        for (int row = 0; row < table->rowCount(); ++row) {
            auto *editor = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
            const auto expected =
                i18n.text(QString("app.separation.role_%1").arg(controller.settings().roles[row]));
            if (!editor || editor->currentText() != expected)
                return fail("channel role translation stale");
        }
        for (int row = 0; row < componentTable->rowCount(); ++row) {
            auto *editor = componentTable->item(row, 1);
            const auto expected = i18n.text(QString("app.separation.group_%1").arg(controller.labels()[row]));
            if (!editor || editor->text() != expected)
                return fail("component source translation stale");
        }
        for (auto *widget :
             std::vector<QWidget *>{main, rerun, confirm, amplitudeMode, amplitudeZoom, groupTraces,
                                    play, restart, playbackPosition, channelLabel, windowLabel, zoomLabel}) {
            const QRect bounds(widget->mapTo(&ui, QPoint()), widget->size());
            if (!widget->isVisible() || !ui.rect().contains(bounds))
                return fail("essential action outside 1280x900");
        }
        if (main->text().startsWith("app.") || rerun->text().startsWith("app.") ||
            help->windowTitle().startsWith("app."))
            return fail("untranslated main action");
        if (amplitudeMode->accessibleName().isEmpty() || amplitudeZoom->accessibleName().isEmpty() ||
            amplitudeMode->currentText().startsWith("app.") || handle->toolTip().startsWith("app.") ||
            !displayIsNonDestructive())
            return fail("display controls lose accessibility, translation, or reviewed analysis on language change");
        for (auto *label : {channelLabel, windowLabel, zoomLabel})
            if (label->text().isEmpty() || label->text().startsWith("app."))
                return fail("preview control has no translated visible label");
    }
    i18n.loadLocale("zh_CN", &error);
    ui.setDarkTheme(false);
    app.processEvents();
    for (const auto& size : {QSize(1000, 780), QSize(1870, 1000), QSize(1280, 900)}) {
        ui.resize(size);
        app.processEvents();
        if (ui.size() != size) return fail("workspace exceeds requested desktop size");
        const auto* panes = ui.findChild<QSplitter*>("separationSplitter");
        for (auto* action : {main, rerun}) {
            const QRect bounds(action->mapTo(&ui, QPoint()), action->size());
            if (!ui.rect().contains(bounds) || bounds.bottom() >= panes->mapTo(&ui, QPoint()).y())
                return fail("workflow action moves out of the top action bar");
        }
        if (!captureUi(QString("separation-layout-%1x%2.png").arg(size.width()).arg(size.height())))
            return fail("cannot capture responsive action layout");
    }
    if (!capture.isEmpty()) {
        QDir().mkpath(capture);
        auto captureState = [&](const char *filename) {
            app.processEvents();
            return ui.grab().save(QDir(capture).filePath(QString::fromLatin1(filename)));
        };
        if (!captureState("separation-review-1280x900.png"))
            return fail("cannot capture default review layout");
        ui.setDarkTheme(true);
        if (!captureState("separation-dark-1280x900.png"))
            return fail("cannot capture dark review layout");
        ui.setDarkTheme(false);
        amplitudeMode->setCurrentIndex(1);
        if (!captureState("separation-adapted-groups-1280x900.png"))
            return fail("cannot capture adapted group scales");
        amplitudeMode->setCurrentIndex(0);
        amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(10));
        if (!captureState("separation-shared-10x-1280x900.png"))
            return fail("cannot capture zoomed shared scales");
        amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(1));
        table->selectRow(0);
        table->selectionModel()->select(table->model()->index(1, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!captureState("separation-batch-selected-1280x900.png"))
            return fail("cannot capture contextual batch actions");
        table->clearSelection();
        previewPosition->setValue(500);
        play->click();
        if (!captureState("separation-playing-1280x900.png"))
            return fail("cannot capture synchronized playback controls and cursors");
        restart->click();
    }
    QTemporaryDir destination;
    QString output = QDir(destination.path()).filePath("reviewed-analysis");
    bool fileDialogConfigured = false;
    bool fileDialogAccepted = false;
    bool dialogTimedOut = false;
    bool playbackDuringExport = false;
    QString actualDirectory;
    QStringList actualSelectedFiles;
    QStringList modalTitles;
    QTimer dialogs;
    dialogs.setInterval(25);
    QObject::connect(&dialogs, &QTimer::timeout, [&] {
        auto *active = QApplication::activeModalWidget();
        if (!active)
            return;
        playbackDuringExport = playbackDuringExport || isPlaying();
        if (!modalTitles.contains(active->windowTitle()))
            modalTitles.append(active->windowTitle());
        if (auto *chooser = qobject_cast<QFileDialog *>(active)) {
            if (fileDialogConfigured)
                return;
            fileDialogConfigured = true;
            // The shared Windows source may reside on a slow filesystem. Wait
            // for QFileSystemModel to finish the requested directory instead
            // of racing its asynchronous selection update against a fixed delay.
            QPointer<QFileDialog> guardedChooser(chooser);
            auto submit = [&, guardedChooser] {
                if (!guardedChooser || fileDialogAccepted)
                    return;
                // Directory loading can restore the dialog's initial filename.
                // Set the requested filename only after that update, including
                // the editable text from which QFileDialog::accept reads.
                const auto name = QFileInfo(output).fileName();
                guardedChooser->selectFile(name);
                if (auto *editor = guardedChooser->findChild<QLineEdit *>("fileNameEdit"))
                    editor->setText(name);
                actualDirectory = guardedChooser->directory().absolutePath();
                actualSelectedFiles = guardedChooser->selectedFiles();
                if (actualSelectedFiles.size() != 1 ||
                    QFileInfo(actualSelectedFiles.front()).absoluteFilePath() !=
                        QFileInfo(output).absoluteFilePath()) {
                    guardedChooser->reject();
                    return;
                }
                fileDialogAccepted = true;
                QMetaObject::invokeMethod(guardedChooser, "accept", Qt::QueuedConnection);
            };
            auto *model = chooser->findChild<QFileSystemModel *>();
            if (!model) {
                chooser->reject();
                return;
            }
            QObject::connect(model, &QFileSystemModel::directoryLoaded, chooser,
                             [&, guardedChooser, submit](const QString &path) {
                if (QDir::cleanPath(path) == QDir::cleanPath(destination.path()) && !fileDialogAccepted)
                    QTimer::singleShot(0, guardedChooser, submit);
            });
            const auto targetIndex = model->index(destination.path());
            const bool alreadyLoaded = chooser->directory().absolutePath() == destination.path()
                                       && model->rowCount(targetIndex) > 0 && !model->canFetchMore(targetIndex);
            chooser->setDirectory(destination.path());
            if (alreadyLoaded)
                QTimer::singleShot(0, chooser, submit);
        }
    });
    QTimer dialogTimeout;
    dialogTimeout.setSingleShot(true);
    QObject::connect(&dialogTimeout, &QTimer::timeout, [&] {
        dialogTimedOut = true;
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    const bool enabledBeforeExport = main->isEnabled();
    play->click();
    dialogs.start();
    dialogTimeout.start(20000);
    main->click();
    dialogs.stop();
    dialogTimeout.stop();
    const bool completed = wait([&] { return !controller.isBusy(); });
    if (!completed || !QFileInfo::exists(QDir(output).filePath("analysis.json"))) {
        const auto &settings = controller.settings();
        std::cerr << "UI export diagnostics: error=" << failure.toStdString()
                  << " enabled_before=" << enabledBeforeExport << " enabled_now=" << main->isEnabled()
                  << " confirmed=" << controller.isConfirmed() << " checkbox=" << confirm->isChecked()
                  << " state=" << static_cast<int>(controller.state()) << " busy=" << controller.isBusy()
                  << " source=" << controller.source().path.toStdString() << " fs=" << controller.source().fs
                  << " start=" << settings.start << " duration=" << settings.duration
                  << " output_rate=" << settings.outputRate << " notch=" << settings.notchHz
                  << " has_output=" << bool(controller.output()) << " expected=" << output.toStdString()
                  << " exported=" << controller.exportedPath().toStdString()
                  << " chooser_configured=" << fileDialogConfigured
                  << " chooser_accepted=" << fileDialogAccepted << " dialog_timeout=" << dialogTimedOut
                  << " chooser_directory=" << actualDirectory.toStdString()
                  << " chooser_files=" << actualSelectedFiles.join(" | ").toStdString()
                  << " modal_titles=" << modalTitles.join(" | ").toStdString() << '\n';
        return fail("UI export failed");
    }
    if (QDir(output).entryList(QDir::Files).size() != 8)
        return fail("incomplete export manifest");
    if (playbackDuringExport || isPlaying())
        return fail("opening export does not pause result playback");
    // A second export uses the same reviewed arrays, and cancellation or a
    // refused destination must leave the action usable for the next attempt.
    auto rejectChooser = [&] {
        auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (chooser) chooser->reject();
    };
    const auto exportedOutput = controller.output();
    for (int attempt = 0; attempt < 2; ++attempt) {
        QTimer::singleShot(50, &ui, rejectChooser);
        main->click();
        if (!main->isEnabled() || controller.isBusy() || !controller.isConfirmed() ||
            controller.output() != exportedOutput)
            return fail("cancelled export cannot be repeated with the reviewed result");
        QTimer::singleShot(50, &ui, rejectChooser);
        ui.chooseSource();
        if (!ui.canChangeInput() || controller.output() != exportedOutput)
            return fail("cancelling open recording loses the result or disables the action");
    }
    const QString firstOutput = output;
    // A save chooser navigates into existing directories. Use an existing
    // regular file to exercise the UI's destination-conflict guard; the
    // controller suite separately checks existing result directories.
    output = QDir(destination.path()).filePath("occupied-result");
    QFile occupied(output);
    if (!occupied.open(QIODevice::WriteOnly) || occupied.write("preserve") != 8)
        return fail("cannot prepare occupied destination");
    occupied.close();
    fileDialogConfigured = fileDialogAccepted = false;
    dialogs.start();
    dialogTimeout.start(20000);
    main->click();
    dialogs.stop();
    dialogTimeout.stop();
    if (!fileDialogAccepted || dialogTimedOut || !failure.isEmpty() || !status->isVisible() ||
        status->text() != i18n.text("app.separation.feedback_error", {{"message", i18n.text("error.analysis.destination_exists", {{"path", output}})}})
                              .section(QStringLiteral("\n\n"), 0, 0) ||
        !main->isEnabled() || !controller.isConfirmed())
        return fail("existing destination is not rejected recoverably");
    failure.clear();
    // Change only the destination and retry through the same UI action.
    auto *retry = ui.findChild<QPushButton *>("separationRetry");
    if (!retry || !retry->isVisible() || !retry->isEnabled())
        return fail("recoverable export failure offers no retry action");
    if (!retry->property("primaryAction").toBool() || main->isVisible() || status->property("feedbackTone") != "error")
        return fail("export recovery has ambiguous primary actions or lacks error feedback");
    if (!captureUi("separation-export-error-1280x900.png")) return fail("cannot capture export recovery");
    confirm->setChecked(false);
    if (retry->isEnabled() || main->isEnabled())
        return fail("export retry bypasses revoked review confirmation");
    confirm->setChecked(true);
    if (!retry->isEnabled())
        return fail("export retry cannot recover after confirmation");
    chooseLabel((controller.labels()[0] + 1) % 4);
    if (retry->isVisible() || status->isVisible() || controller.isConfirmed())
        return fail("label changes retain a stale export error and unusable retry action");
    confirm->setChecked(true);
    fileDialogConfigured = fileDialogAccepted = false;
    dialogs.start();
    dialogTimeout.start(20000);
    main->click();
    dialogs.stop();
    dialogTimeout.stop();
    if (dialogTimedOut || !retry->isVisible() || !retry->isEnabled())
        return fail("repeated destination conflict loses the retry action");
    output = QDir(destination.path()).filePath("reviewed-analysis-again");
    fileDialogConfigured = fileDialogAccepted = false;
    dialogs.start();
    dialogTimeout.start(20000);
    retry->click();
    dialogs.stop();
    dialogTimeout.stop();
    if (!wait([&] { return !controller.isBusy(); }) || !failure.isEmpty() ||
        QDir(output).entryList(QDir::Files).size() != 8 ||
        QDir(firstOutput).entryList(QDir::Files).size() != 8 || !main->isEnabled())
        return fail("second export failed after a refused destination");
    if (!occupied.open(QIODevice::ReadOnly) || occupied.readAll() != "preserve")
        return fail("export retry changed the occupied destination");
    occupied.close();
    chooseLabel((controller.labels()[0] + 1) % 4);
    if (!controller.exportedPath().isEmpty() || status->isVisible() || main->isEnabled() ||
        !rerun->isEnabled())
        return fail("editing labels retains a stale exported status or blocks rerun");
    confirm->setChecked(true);
    controller.exportBundle(QDir(destination.path()).filePath("after-label-change"));
    if (!wait([&] { return !controller.isBusy(); }) || controller.exportedPath().isEmpty())
        return fail("export after label changes failed");
    ui.findChild<QPushButton *>("separationUndo")->click();
    if (!controller.exportedPath().isEmpty() || status->isVisible() || controller.isConfirmed())
        return fail("undo retains a stale exported status or confirmation");
    auto *outputRate = ui.findChild<QComboBox *>("separationOutputRate");
    auto *notch = ui.findChild<QComboBox *>("separationNotch");
    for (int attempt = 0; attempt < 2; ++attempt) {
        outputRate->setCurrentIndex(outputRate->findData(250));
        if (controller.settings().outputRate != 250 || controller.output() ||
            controller.isConfirmed() || !main->isEnabled())
            return fail("output rate change leaves stale results or prevents analysis");
        outputRate->setCurrentIndex(outputRate->findData(500));
        for (int hz : {50, 60, 0}) {
            notch->setCurrentIndex(notch->findData(hz));
            if (controller.settings().notchHz != hz || !main->isEnabled())
                return fail("notch selection cannot be changed repeatedly");
        }
    }
    // An optional local review fixture reproduces the user's high-amplitude
    // recording. Normal CI remains self-contained and uses the bundled HDF5.
    const QString reviewSource = qEnvironmentVariable("BF_UI_REVIEW_SOURCE");
    if (!reviewSource.isEmpty()) {
        failure.clear();
        play->click();
        controller.openSource(reviewSource);
        if (isPlaying())
            return fail("opening another recording leaves previous preview playing");
        if (!wait([&] { return !controller.isBusy(); }) || !failure.isEmpty() ||
            controller.source().channels.size() != 5)
            return fail("optional user recording could not be opened");
        const std::array<int, 5> reviewRoles{1, 1, 1, 2, 3};
        for (int row = 0; row < 5; ++row) {
            auto *editor = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
            if (!editor)
                return fail("optional user recording channel role editor is absent");
            editor->setCurrentIndex(reviewRoles[row]);
        }
        auto reviewSettings = controller.settings();
        reviewSettings.roles = {1, 1, 1, 2, 3};
        reviewSettings.start = 0;
        reviewSettings.duration = 16;
        reviewSettings.outputRate = 500;
        reviewSettings.notchHz = 0;
        controller.setSettings(reviewSettings);
        controller.start();
        if (!wait([&] { return !controller.isBusy(); }) || !failure.isEmpty() ||
            !controller.output() || controller.labels().size() != 3)
            return fail("optional user recording separation failed");
        controller.setLabel(0, 1);
        controller.setLabel(1, 2);
        controller.setLabel(2, 0);
        controller.setConfirmed(true);
        amplitudeMode->setCurrentIndex(0);
        amplitudeZoom->setCurrentIndex(amplitudeZoom->findData(1));
        previewChannel->setCurrentIndex(0);
        previewPosition->setValue(0);
        app.processEvents();
        groupTraces->repaint();
        const auto reviewRanges = groupTraces->property("visiblePeakToPeak").toList();
        const auto reviewScales = groupTraces->property("visibleScaleLimits").toList();
        const auto reviewSmall = groupTraces->property("smallWaveGroups").toList();
        if (reviewRanges.size() != 5 || reviewScales.size() != 5 || reviewSmall.size() != 5 ||
            reviewRanges[1].toDouble() <= 1e-12 ||
            reviewRanges[1].toDouble() / (2 * reviewScales[1].toDouble()) >= .02 ||
            !reviewSmall[1].toBool())
            return fail("small EEG group in user recording lacks a shared-scale warning");
        auto* categories = ui.findChild<QButtonGroup*>("separationClassificationChoices");
    if (!categories || !categories->exclusive() || categories->checkedId()!=3)
        return fail("classification choices are not an exclusive radio group");
    const auto reviewOutput = controller.output();
        const auto reviewLabels = controller.labels();
        if (!capture.isEmpty() &&
            !ui.grab().save(QDir(capture).filePath("separation-user-rhd-shared-1280x900.png")))
            return fail("cannot capture user recording shared scales");
        amplitudeMode->setCurrentIndex(1);
        app.processEvents();
        groupTraces->repaint();
        if (groupTraces->property("smallWaveGroups").toList()[1].toBool() ||
            controller.output() != reviewOutput || controller.labels() != reviewLabels ||
            !controller.isConfirmed())
            return fail("fitting small EEG group to its scale changes analysis or retains stale warning");
        if (!capture.isEmpty() &&
            !ui.grab().save(QDir(capture).filePath("separation-user-rhd-adapted-1280x900.png")))
            return fail("cannot capture user recording adapted scales");
        amplitudeMode->setCurrentIndex(0);
        app.processEvents();
        std::cout << "user-recording-review-ok small_eeg_warning=true adapted_group_visible=true\n";
    }
    play->click();
    auto changedSettings = controller.settings();
    changedSettings.duration -= 1;
    controller.setSettings(changedSettings);
    if (isPlaying() || controller.output() || play->isEnabled() || restart->isEnabled())
        return fail("analysis input invalidation retains playback or stale transport controls");

    // Actual editors must not round source bounds outward for either export mode.
    for (double end : {12.288, 24577. / 2000., 3073. / 256., 12583. / 1024.}) {
        for (bool processed : {false, true}) {
            bf::ExportSegmentDialog dialog(&i18n, end, 0, end, processed, {});
            auto *endEditor = dialog.findChild<QDoubleSpinBox *>("eegExportEnd");
            if (!endEditor || dialog.startTime() != 0 || dialog.endTime() > end ||
                std::abs(dialog.endTime() - end) > 1e-6 || dialog.processed() != processed)
                return fail("fractional full-file export rounds beyond the source bound");
            endEditor->setValue(endEditor->maximum());
            if (dialog.endTime() > end)
                return fail("export maximum bypasses the exact source bound");
        }
    }

    // Sources ending between milliseconds expose time-editor round trips and
    // resampled playback tails. Use real sidecar I/O with independent known sources.
    QTemporaryDir precisionSources;
    if (!precisionSources.isValid())
        return fail("cannot create fractional-duration fixtures");
    const QString fixtureScript = QStringLiteral(R"PY(
import sys, h5py, numpy as np
from pathlib import Path
for fs, count in [(2000,24576),(2000,24577),(1024,12583),(256,3073)]:
    t=np.arange(count)/fs
    eye=18*np.sin(2*np.pi*.18*t)
    for blink in [2.2,5.1,9.3,11.6]: eye+=100*np.exp(-((t-blink)/.12)**2)
    sources=np.array([12*np.sin(2*np.pi*10*t)+4*np.sin(2*np.pi*63*t),eye,
                      (np.random.default_rng(12138).random(count)-.5)*30])
    data=np.array([[1,.5,.3],[.3,1.2,.1],[1,-.2,1.1]])@sources
    with h5py.File(Path(sys.argv[1])/f'{fs}-{count}.h5','w') as f:
        eeg=f.create_dataset('eeg',data=data)
        eeg.attrs['fs']=fs; eeg.attrs['unit']='uV'
        eeg.attrs['ch_names']=np.array(['obs0','obs1','obs2'],dtype='S')
)PY");
    QProcess createPrecision;
    createPrecision.start(qEnvironmentVariable("BF_PYTHON_EXECUTABLE", "python3"),
                          {"-c", fixtureScript, precisionSources.path()});
    if (!createPrecision.waitForStarted(10000) || !createPrecision.waitForFinished(30000) ||
        createPrecision.exitCode() != 0)
        return fail("cannot generate fractional-duration source data");
    for (const auto pair : {std::pair<int,int>{2000,24576}, {2000,24577}, {1024,12583}, {256,3073}}) {
        failure.clear();
        controller.openSource(QDir(precisionSources.path()).filePath(QStringLiteral("%1-%2.h5").arg(pair.first).arg(pair.second)));
        if (!wait([&] { return !controller.isBusy(); }) || !failure.isEmpty())
            return fail("fractional-duration source cannot be opened");
        for (int row = 0; row < 3; ++row)
            qobject_cast<QComboBox *>(table->cellWidget(row, 1))->setCurrentIndex(1);
        auto *duration = ui.findChild<QDoubleSpinBox *>("separationDuration");
        const double sourceEnd = pair.second / double(pair.first);
        if (!duration || !main->isEnabled() || duration->value() > sourceEnd ||
            std::llround(duration->value() * pair.first) != pair.second) {
            std::cerr << "Fractional interval: fs=" << pair.first << " samples=" << pair.second
                      << " editor=" << (duration ? duration->value() : -1)
                      << " settings=" << controller.settings().duration
                      << " enabled=" << main->isEnabled() << '\n';
            return fail("default fractional duration loses samples or exceeds EOF");
        }
        // Editing one field copies the other editors back into controller settings.
        start->setValue(.001);
        start->setValue(0);
        if (!main->isEnabled() || controller.settings().duration > sourceEnd ||
            std::llround(controller.settings().duration * pair.first) != pair.second)
            return fail("time-editor round trip changes the default sample endpoint");
        main->click();
        if (!wait([&] { return !controller.isBusy(); }) || !failure.isEmpty() || !controller.output() ||
            controller.output()->manifest.value("sample_count").toInteger() != pair.second)
            return fail("fractional full-file analysis does not preserve the source sample count");
        previewPosition->setValue(1000);
        const double lastPosition = ui.property("previewPositionSeconds").toDouble();
        if (lastPosition > sourceEnd + 1e-12 || std::abs(lastPosition - sourceEnd) > .0041)
            return fail("resampled preview playback ends past the analyzed source interval");
    }
    std::cout << "eeg-separation-ui-ok real_signal=true roles=true labels=true undo=true confirmation=true "
                 "export=true contextual_batch=true splitter_drag=true amplitude_controls=true "
                 "visible_peak_to_peak=true fixed_interval_scales=true preview_playback_1x=true "
                 "preview_pause_seek_end=true synchronized_cursor=true display_preserves_analysis=true "
                 "fractional_ranges=true export_bounds=true playback_tail_bounds=true "
                 "locales=5 compact=1280x900\n";
    return 0;
}

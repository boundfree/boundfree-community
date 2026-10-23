#include "ui/eeg_separation_workspace.h"
#include "ui/export_segment_dialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
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
    auto *teaching = ui.findChild<QPushButton *>("separationOpenTeaching");
    if (!teaching)
        return fail("teaching entry missing");
    teaching->click();
    if (!wait([&] { return !controller.isBusy(); }) || controller.source().channels.size() != 5 ||
        !failure.isEmpty()) {
        std::cerr << failure.toStdString() << '\n';
        return fail("teaching source open failed");
    }
    auto *table = ui.findChild<QTableWidget *>("separationChannels");
    auto *bulk = ui.findChild<QComboBox *>("separationBulkRole");
    auto *bulkActions = ui.findChild<QWidget *>("separationBulkActions");
    auto *bulkCount = ui.findChild<QLabel *>("separationBulkSelection");
    auto *applyRole = ui.findChild<QPushButton *>("separationApplyRole");
    auto *role = ui.findChild<QComboBox *>("separationRole0");
    if (!table || !role || !bulk || !bulkActions || !bulkCount || !applyRole || !main->isEnabled())
        return fail("teaching roles not prefilled");
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
    auto *labels = ui.findChild<QComboBox *>("separationLabel0");
    if (!labels || labels->currentIndex() != 3)
        return fail("initial source not unresolved");
    const auto before = controller.output();
    play->click();
    labels->setCurrentIndex(0);
    if (ui.property("previewPlaying").toBool())
        return fail("component label editing leaves preview playback running");
    if (ui.findChild<QComboBox *>("separationLabel0") != labels)
        return fail("active label editor was destroyed");
    if (controller.output() != before || controller.labels()[0] != 0)
        return fail("label changes refit or fail");
    ui.findChild<QPushButton *>("separationUndo")->click();
    if (controller.labels()[0] != 3)
        return fail("undo failed");
    confirm->setChecked(true);
    if (!main->isEnabled())
        return fail("unresolved cannot be confirmed");
    ui.findChild<QComboBox *>("separationLabel0")->setCurrentIndex(1);
    if (confirm->isChecked() || main->isEnabled())
        return fail("label edit does not revoke confirmation");
    ui.findChild<QPushButton *>("separationTeachingLabels")->click();
    confirm->setChecked(true);
    auto *amplitudeMode = ui.findChild<QComboBox *>("separationAmplitudeMode");
    auto *amplitudeZoom = ui.findChild<QComboBox *>("separationAmplitudeZoom");
    auto *groupTraces = ui.findChild<QWidget *>("separationGroupTraces");
    auto *previewChannel = ui.findChild<QComboBox *>("separationPreviewChannel");
    auto *previewPosition = ui.findChild<QSlider *>("separationPreviewPosition");
    if (!amplitudeMode || !amplitudeZoom || !groupTraces || !previewChannel || !previewPosition ||
        amplitudeMode->count() != 2 || amplitudeMode->currentIndex() != 0 ||
        amplitudeZoom->count() != 4 || amplitudeZoom->currentData().toInt() != 1)
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
    auto *componentTrace = ui.findChild<QWidget *>("separationComponentTrace");
    auto *spectrum = ui.findChild<QWidget *>("separationSpectrum");
    auto *componentTable = ui.findChild<QTableWidget *>("separationComponents");
    auto *channelLabel = ui.findChild<QLabel *>("separationPreviewChannelLabel");
    auto *windowLabel = ui.findChild<QLabel *>("separationDisplayWindowLabel");
    auto *zoomLabel = ui.findChild<QLabel *>("separationWaveformZoomLabel");
    if (!componentTrace || !spectrum || !componentTable || !channelLabel || !windowLabel || !zoomLabel ||
        !play->isEnabled() || !restart->isEnabled() || playbackPosition->text().isEmpty())
        return fail("result preview transport, explicit labels, or synchronized traces are absent");
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
            auto *editor = qobject_cast<QComboBox *>(componentTable->cellWidget(row, 1));
            const auto expected = i18n.text(QString("app.separation.group_%1").arg(controller.labels()[row]));
            if (!editor || editor->currentText() != expected)
                return fail("component source translation stale");
        }
        for (auto *widget :
             std::vector<QWidget *>{main, confirm, amplitudeMode, amplitudeZoom, groupTraces,
                                    play, restart, playbackPosition, channelLabel, windowLabel, zoomLabel}) {
            const QRect bounds(widget->mapTo(&ui, QPoint()), widget->size());
            if (!widget->isVisible() || !ui.rect().contains(bounds))
                return fail("essential action outside 1280x900");
        }
        if (main->text().startsWith("app."))
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
    const QString capture = qEnvironmentVariable("BF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QDir().mkpath(capture);
        auto captureState = [&](const char *filename) {
            app.processEvents();
            return ui.grab().save(QDir(capture).filePath(QString::fromLatin1(filename)));
        };
        if (!captureState("separation-review-1280x900.png"))
            return fail("cannot capture default review layout");
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
    const QString output = QDir(destination.path()).filePath("reviewed-analysis");
    bool manifestAccepted = false;
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
            chooser->setDirectory(destination.path());
            // QFileSystemModel applies directory changes asynchronously. Allow one
            // event-loop turn, then submit exactly once instead of queueing a new
            // accept call on every timer tick.
            QPointer<QFileDialog> guardedChooser(chooser);
            QTimer::singleShot(150, chooser, [&, guardedChooser] {
                if (!guardedChooser)
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
            });
        } else if (!manifestAccepted && active->windowTitle() == i18n.text("app.separation.export_title")) {
            if (auto *buttons = active->findChild<QDialogButtonBox *>()) {
                if (auto *accept = buttons->button(QDialogButtonBox::Ok)) {
                    manifestAccepted = true;
                    accept->click();
                }
            }
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
                  << " manifest_accepted=" << manifestAccepted
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

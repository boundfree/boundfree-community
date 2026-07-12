#include "ui/main_window.h"

#include "render/waveform_slice.h"

#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

namespace bf {

MainWindow::MainWindow(const VersionInfo& version, I18nService* i18n, QWidget* parent)
    : QMainWindow(parent), version_(version), i18n_(i18n) {
    centralStack_ = new QStackedWidget(this);
    centralLabel_ = new QLabel(this);
    centralLabel_->setAlignment(Qt::AlignCenter);
    waveformWidget_ = new WaveformWidget(this);
    centralStack_->addWidget(centralLabel_);
    centralStack_->addWidget(waveformWidget_);
    setCentralWidget(centralStack_);
    buildLanguageMenu();
    buildBrowserControls();
    resize(900, 560);
    retranslateUi();
}

void MainWindow::buildLanguageMenu() {
    languageMenu_ = menuBar()->addMenu(QString());
    const QStringList locales = i18n_->supportedLocales();
    for (const QString& locale : locales) {
        auto* action = languageMenu_->addAction(QString());
        action->setCheckable(true);
        action->setData(locale);
        QObject::connect(action, &QAction::triggered, this, [this, locale]() {
            QString error;
            if (i18n_->loadLocale(locale, &error)) {
                retranslateUi();
            }
        });
        languageActions_.append(action);
    }
}

void MainWindow::buildBrowserControls() {
    browserToolbar_ = addToolBar("Browser");
    timeWindowSpin_ = new QDoubleSpinBox(this);
    timeWindowSpin_->setRange(1.0, 120.0);
    timeWindowSpin_->setDecimals(0);
    timeWindowSpin_->setValue(viewState_.windowSeconds);

    amplitudeSpin_ = new QDoubleSpinBox(this);
    amplitudeSpin_->setRange(50.0, 5000.0);
    amplitudeSpin_->setDecimals(0);
    amplitudeSpin_->setValue(viewState_.amplitudeUv);

    browserToolbar_->addWidget(timeWindowSpin_);
    browserToolbar_->addWidget(amplitudeSpin_);

    connect(timeWindowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        viewState_.setWindowSeconds(value);
        applyBrowserStateToWaveform();
    });
    connect(amplitudeSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        viewState_.setAmplitudeUv(value);
        applyBrowserStateToWaveform();
    });
}

void MainWindow::refreshBrowserControls() {
    if (timeWindowSpin_ != nullptr) {
        timeWindowSpin_->setValue(viewState_.windowSeconds);
    }
    if (amplitudeSpin_ != nullptr) {
        amplitudeSpin_->setValue(viewState_.amplitudeUv);
    }
}

void MainWindow::retranslateUi() {
    setWindowTitle(i18n_->text("app.window_title", {{"product_name", version_.productName}, {"version", version_.version}}));
    centralLabel_->setText(i18n_->text(
        "app.main.empty_state",
        {{"product_name", version_.productName}, {"version", version_.version}}));
    languageMenu_->setTitle(i18n_->text("app.menu.language"));
    for (QAction* action : languageActions_) {
        const QString locale = action->data().toString();
        action->setText(i18n_->text(QString("app.language.%1").arg(locale)));
        action->setChecked(locale == i18n_->locale());
    }

    if (statusKey_ == "app.status.sidecar_failed") {
        statusBar()->showMessage(i18n_->text(
            statusKey_,
            {{"error", i18n_->text(QString("error.%1").arg(lastError_.code), lastError_.params)}}));
        return;
    }
    statusBar()->showMessage(i18n_->text(statusKey_, statusParams_));
}

void MainWindow::setSidecarConnecting() {
    statusKey_ = "app.status.sidecar_connecting";
    statusParams_ = {};
    retranslateUi();
}

void MainWindow::setSidecarConnected() {
    statusKey_ = "app.status.sidecar_connected";
    statusParams_ = {};
    retranslateUi();
}

void MainWindow::setSidecarFailed(const AppError& error) {
    statusKey_ = "app.status.sidecar_failed";
    lastError_ = error;
    statusParams_ = {};
    retranslateUi();
}

void MainWindow::showFileOpenLoading(const QString& path) {
    statusKey_ = "app.status.file_opening";
    statusParams_ = {{"path", path}};
    retranslateUi();
}

void MainWindow::showFileOpenResult(const FileOpenResult& result) {
    currentFile_ = result;
    hasCurrentFile_ = true;
    waveformWidget_->setSlice(result.waveform);
    centralStack_->setCurrentWidget(waveformWidget_);
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

void MainWindow::showFileOpenError(const AppError& error) {
    setSidecarFailed(error);
}

void MainWindow::setBrowserViewState(const WaveformViewState& state) {
    viewState_ = state;
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

void MainWindow::applyBrowserStateToWaveform() {
    waveformWidget_->setViewState(viewState_);
    if (!hasCurrentFile_) {
        return;
    }

    const WaveformDisplayModel model = waveformDisplayModel(currentFile_.waveform, viewState_, 32);
    statusKey_ = "app.status.file_window";
    statusParams_ = {
        {"format", currentFile_.session.format},
        {"t0", currentFile_.waveform.t0},
        {"t1", currentFile_.waveform.t1},
        {"amplitude", viewState_.amplitudeUv},
        {"n_visible", static_cast<int>(model.data.size())},
    };
    retranslateUi();
}

QString MainWindow::browserStatusText() const {
    return statusBar()->currentMessage();
}

}  // namespace bf

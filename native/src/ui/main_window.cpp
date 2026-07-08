#include "ui/main_window.h"

#include <QJsonObject>
#include <QMenuBar>
#include <QStatusBar>

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
    waveformWidget_->setSlice(result.waveform);
    centralStack_->setCurrentWidget(waveformWidget_);
    statusKey_ = "app.status.file_opened";
    statusParams_ = {
        {"format", result.session.format},
        {"fs", result.session.fs},
        {"n_channels", result.session.nChannels},
    };
    retranslateUi();
}

void MainWindow::showFileOpenError(const AppError& error) {
    setSidecarFailed(error);
}

}  // namespace bf

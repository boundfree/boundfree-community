#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "i18n/i18n_service.h"

#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QVector>

namespace bf {

class MainWindow : public QMainWindow {
public:
    MainWindow(const VersionInfo& version, I18nService* i18n, QWidget* parent = nullptr);

    void retranslateUi();
    void setSidecarConnecting();
    void setSidecarConnected();
    void setSidecarFailed(const AppError& error);

private:
    void buildLanguageMenu();

    VersionInfo version_;
    I18nService* i18n_ = nullptr;
    QLabel* centralLabel_ = nullptr;
    QMenu* languageMenu_ = nullptr;
    QVector<QAction*> languageActions_;
    QString statusKey_ = "app.status.ready";
    AppError lastError_;
};

}  // namespace bf

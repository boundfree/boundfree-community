#include "ui/activation_dialog.h"

#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "license/machine_id.h"
#include "license/online_activation_client.h"

#include <QApplication>
#include <QClipboard>
#include <QNetworkProxy>
#include <QtGlobal>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

namespace bf {

namespace {

QString statusText(I18nService* i18n, const QString& status) {
    if (status == QStringLiteral("pending_approval")) {
        return i18n->text("app.activation.status_pending_approval");
    }
    if (status == QStringLiteral("active")) {
        return i18n->text("app.activation.status_active");
    }
    if (status == QStringLiteral("expired")) {
        return i18n->text("app.activation.status_expired");
    }
    if (status == QStringLiteral("revoked")) {
        return i18n->text("app.activation.status_revoked");
    }
    if (status == QStringLiteral("rejected")) {
        return i18n->text("app.activation.status_rejected");
    }
    if (status == QStringLiteral("not_found")) {
        return i18n->text("app.activation.status_not_found");
    }
    return i18n->text("app.activation.request_failed");
}

QString activationErrorText(I18nService* i18n, const QString& errorCode,
                            const QJsonObject& params) {
    if (errorCode == QStringLiteral("activation.expired_or_revoked")) {
        return statusText(i18n, params.value("status").toString());
    }
    if (errorCode == QStringLiteral("activation.machine_already_bound")) {
        return i18n->text("app.activation.status_machine_already_bound");
    }
    if (errorCode == QStringLiteral("activation.already_exists")) {
        const QString status = params.value("current_status").toString();
        return i18n->text("app.activation.error_already_exists", {{"status", statusText(i18n, status)}});
    }
    if (errorCode == QStringLiteral("activation.invalid_invite_code")) {
        return i18n->text("app.activation.error_invalid_invite_code");
    }
    if (errorCode == QStringLiteral("activation.invite_revoked")) {
        return i18n->text("app.activation.error_invite_revoked");
    }
    if (errorCode == QStringLiteral("activation.invite_already_used")) {
        return i18n->text("app.activation.error_invite_already_used");
    }
    if (errorCode == QStringLiteral("activation.signing_failed")) {
        return i18n->text("app.activation.error_signing_failed");
    }
    return i18n->text("app.activation.error_detail", {{"code", errorCode}});
}

}  // namespace

ActivationDialog::ActivationDialog(LicenseManager* manager, const VersionInfo& version,
                                   I18nService* i18n, QWidget* parent)
    : QDialog(parent),
      manager_(manager),
      i18n_(i18n),
      productVersion_(version.version),
      canonicalMachineId_(machineIdCanonical(readMachineGuid())),
      displayMachineId_(currentMachineId()),
      machineIdEdit_(nullptr),
      copyBtn_(nullptr),
      requestBtn_(nullptr),
      inviteBtn_(nullptr),
      offlineKeyEdit_(nullptr),
      offlineActivateBtn_(nullptr),
      statusLabel_(nullptr),
      refreshBtn_(nullptr),
      nam_(new QNetworkAccessManager(this)),
      client_(new OnlineActivationClient(
          nam_, QUrl(qEnvironmentVariable("BF_ACTIVATION_SERVER_URL", "https://license-activate.unitc.cn")), this)),
      pollTimer_(new QTimer(this)) {
    // 激活网络请求必须直连，绕过用户本机的系统代理（Clash/VPN等）
    // 否则 FlClash fake-ip / HTTP代理规则会导致连接失败
    nam_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));

    connect(client_, &OnlineActivationClient::registrationSubmitted,
            this, &ActivationDialog::onRegistrationSubmitted);
    connect(client_, &OnlineActivationClient::registrationAlreadyExists,
            this, &ActivationDialog::onRegistrationAlreadyExists);
    connect(client_, &OnlineActivationClient::activationSuccess,
            this, &ActivationDialog::onActivationSuccess);
    connect(client_, &OnlineActivationClient::activationFailed,
            this, &ActivationDialog::onActivationFailed);
    connect(client_, &OnlineActivationClient::statusReceived,
            this, &ActivationDialog::onStatusReceived);
    connect(client_, &OnlineActivationClient::networkError,
            this, &ActivationDialog::onNetworkError);

    buildUi();
    pollTimer_->setInterval(30'000);
    connect(pollTimer_, &QTimer::timeout, this, &ActivationDialog::onRefreshStatus);
}

void ActivationDialog::buildUi() {
    setWindowTitle(i18n_->text("app.activation.title"));
    setMinimumSize(520, 480);
    resize(540, 520);

    auto* layout = new QVBoxLayout(this);
    auto* machineLayout = new QHBoxLayout;
    auto* machineLabel = new QLabel(i18n_->text("app.activation.machine_id_label"), this);
    machineIdEdit_ = new QLineEdit(displayMachineId_, this);
    machineIdEdit_->setReadOnly(true);
    copyBtn_ = new QPushButton(i18n_->text("app.activation.copy"), this);
    machineLayout->addWidget(machineLabel);
    machineLayout->addWidget(machineIdEdit_, 1);
    machineLayout->addWidget(copyBtn_);
    layout->addLayout(machineLayout);

    auto* onlineButtons = new QHBoxLayout;
    auto* requestLayout = new QVBoxLayout;
    requestBtn_ = new QPushButton(i18n_->text("app.activation.request"), this);
    requestBtn_->setMinimumHeight(60);
    auto* requestHint = new QLabel(i18n_->text("app.activation.request_hint"), this);
    requestHint->setAlignment(Qt::AlignHCenter);
    requestLayout->addWidget(requestBtn_);
    requestLayout->addWidget(requestHint);

    auto* inviteLayout = new QVBoxLayout;
    inviteBtn_ = new QPushButton(i18n_->text("app.activation.invite_code"), this);
    inviteBtn_->setMinimumHeight(60);
    auto* inviteHint = new QLabel(i18n_->text("app.activation.invite_code_hint"), this);
    inviteHint->setAlignment(Qt::AlignHCenter);
    inviteLayout->addWidget(inviteBtn_);
    inviteLayout->addWidget(inviteHint);

    onlineButtons->addLayout(requestLayout);
    onlineButtons->addLayout(inviteLayout);
    layout->addLayout(onlineButtons);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);
    layout->addWidget(new QLabel(i18n_->text("app.activation.offline_section"), this));

    offlineKeyEdit_ = new QPlainTextEdit(this);
    offlineKeyEdit_->setPlaceholderText(i18n_->text("app.activation.paste_placeholder"));
    layout->addWidget(offlineKeyEdit_);
    offlineActivateBtn_ = new QPushButton(i18n_->text("app.activation.activate_btn"), this);
    layout->addWidget(offlineActivateBtn_, 0, Qt::AlignRight);

    auto* statusLayout = new QHBoxLayout;
    statusLabel_ = new QLabel(this);
    refreshBtn_ = new QPushButton(i18n_->text("app.activation.status_refresh"), this);
    statusLayout->addWidget(statusLabel_, 1);
    statusLayout->addWidget(refreshBtn_);
    layout->addLayout(statusLayout);
    statusLabel_->hide();
    refreshBtn_->hide();

    connect(copyBtn_, &QPushButton::clicked, this, &ActivationDialog::onCopyMachineId);
    connect(requestBtn_, &QPushButton::clicked, this, &ActivationDialog::onRequestActivation);
    connect(inviteBtn_, &QPushButton::clicked, this, &ActivationDialog::onInviteCode);
    connect(offlineActivateBtn_, &QPushButton::clicked, this, &ActivationDialog::onOfflineActivate);
    connect(refreshBtn_, &QPushButton::clicked, this, &ActivationDialog::onRefreshStatus);
}

void ActivationDialog::setState(const QString& stateLabel, bool showStatusBar) {
    statusLabel_->setText(stateLabel);
    statusLabel_->setVisible(showStatusBar);
    refreshBtn_->setVisible(showStatusBar);

    const bool onlineRequestPending = state_ == REGISTERING || state_ == ACTIVATING;
    const bool waitingApproval = state_ == WAITING_APPROVAL;
    requestBtn_->setEnabled(!onlineRequestPending && !waitingApproval);
    inviteBtn_->setEnabled(!onlineRequestPending && !waitingApproval);
    refreshBtn_->setEnabled(!onlineRequestPending);
    offlineActivateBtn_->setEnabled(state_ != VERIFYING);
}

void ActivationDialog::startPolling() {
    if (!pollTimer_->isActive()) {
        pollTimer_->start();
    }
}

void ActivationDialog::stopPolling() {
    pollTimer_->stop();
}

void ActivationDialog::onRequestActivation() {
    state_ = REGISTERING;
    setState(i18n_->text("app.activation.registering"), true);
    client_->registerDevice(canonicalMachineId_, productVersion_);
}

void ActivationDialog::onInviteCode() {
    bool accepted = false;
    const QString inviteCode = QInputDialog::getText(
        this,
        i18n_->text("app.activation.title"),
        i18n_->text("app.activation.invite_code_prompt"),
        QLineEdit::Normal,
        QString(),
        &accepted).trimmed();
    if (!accepted || inviteCode.isEmpty()) {
        return;
    }

    state_ = ACTIVATING;
    setState(i18n_->text("app.activation.activating"), true);
    client_->activateWithCode(canonicalMachineId_, inviteCode, productVersion_);
}

void ActivationDialog::onOfflineActivate() {
    const QString key = offlineKeyEdit_->toPlainText().trimmed();
    if (key.isEmpty()) {
        QMessageBox::warning(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.invalid_key"));
        return;
    }

    state_ = VERIFYING;
    setState(i18n_->text("app.activation.verifying"), true);
    const LicenseResult result = manager_->verifyKey(key);
    if (!result.ok()) {
        state_ = IDLE;
        setState(i18n_->text("app.activation.invalid_key"), true);
        return;
    }
    if (!manager_->saveLicense(key)) {
        state_ = IDLE;
        setState(i18n_->text("app.activation.save_failed"), true);
        return;
    }

    stopPolling();
    QMessageBox::information(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.activated"));
    accept();
}

void ActivationDialog::onCopyMachineId() {
    QApplication::clipboard()->setText(displayMachineId_);
    setState(i18n_->text("app.activation.copied"), true);
}

void ActivationDialog::onRefreshStatus() {
    client_->checkStatus(canonicalMachineId_);
}

void ActivationDialog::onRegistrationSubmitted(const QString& requestId) {
    Q_UNUSED(requestId);
    state_ = WAITING_APPROVAL;
    setState(i18n_->text("app.activation.status_waiting"), true);
    startPolling();
}
void ActivationDialog::onRegistrationAlreadyExists() {
    state_ = WAITING_APPROVAL;
    setState(i18n_->text("app.activation.status_waiting"), true);
    startPolling();
    onRefreshStatus();
}

void ActivationDialog::onActivationSuccess(const QString& licenseKey, const QDate& expiresAt) {
    Q_UNUSED(expiresAt);
    const LicenseResult result = manager_->verifyKey(licenseKey);
    if (!result.ok()) {
        state_ = IDLE;
        setState(i18n_->text("app.activation.invalid_key"), true);
        return;
    }
    if (!manager_->saveLicense(licenseKey)) {
        state_ = IDLE;
        setState(i18n_->text("app.activation.save_failed"), true);
        return;
    }

    stopPolling();
    QMessageBox::information(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.activated"));
    accept();
}

void ActivationDialog::onActivationFailed(const QString& errorCode, const QJsonObject& params) {
    stopPolling();
    state_ = IDLE;
    setState(activationErrorText(i18n_, errorCode, params), true);
}

void ActivationDialog::onStatusReceived(const QString& status, const QString& message, const QDate& expiresAt) {
    Q_UNUSED(expiresAt);
    if (status == QStringLiteral("active")) {
        state_ = ACTIVATING;
        setState(i18n_->text("app.activation.activating"), true);
        client_->fetchLicense(canonicalMachineId_, productVersion_);
        return;
    }

    if (status == QStringLiteral("pending_approval")) {
        state_ = WAITING_APPROVAL;
        setState(i18n_->text("app.activation.status_waiting"), true);
        startPolling();
        return;
    }

    state_ = IDLE;
    stopPolling();
    setState(message.isEmpty() ? statusText(i18n_, status) : message, true);
}

void ActivationDialog::onNetworkError(const QString& detail) {
    stopPolling();
    state_ = IDLE;
    Q_UNUSED(detail);
    setState(i18n_->text("app.activation.network_unavailable"), true);
}

}  // namespace bf

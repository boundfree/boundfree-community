#include "ui/activation_dialog.h"

#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "license/machine_id.h"
#include "license/online_activation_client.h"

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QFile>
#include <QNetworkProxy>
#include <QtGlobal>
#include <QFrame>
#include <QHBoxLayout>
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
    if (errorCode == QStringLiteral("activation.machine_id_required") ||
        errorCode == QStringLiteral("activation.invalid_machine_id")) {
        return i18n->text("app.activation.error_invalid_machine_id");
    }
    if (errorCode == QStringLiteral("activation.not_found")) {
        return i18n->text("app.activation.status_not_found");
    }
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
    if (errorCode == QStringLiteral("rate_limit.exceeded")) {
        return i18n->text("app.activation.error_rate_limited", {
            {"retry_after", QString::number(params.value("retry_after").toInt())},
        });
    }
    return i18n->text("app.activation.request_failed");
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
      inviteCodeEdit_(nullptr),
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

bool ActivationDialog::checkAndHandleRevocation(
    LicenseManager* manager, const VersionInfo& version, I18nService* i18n) {
    Q_UNUSED(version);
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    OnlineActivationClient client(
        &nam,
        QUrl(qEnvironmentVariable("BF_ACTIVATION_SERVER_URL",
                                  "https://license-activate.unitc.cn")));

    const QString machineId = machineIdCanonical(readMachineGuid());
    bool invalidated = false;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(8000); // 8 秒超时 → fail-open

    QObject::connect(&client, &OnlineActivationClient::statusReceived,
        [&](const QString& status, const QDate&) {
            if (status == QLatin1String("revoked") ||
                status == QLatin1String("rejected") ||
                status == QLatin1String("not_found")) {
                invalidated = true;
            }
            loop.quit();
        });
    QObject::connect(&client, &OnlineActivationClient::networkError,
        [&](const QString&) { loop.quit(); }); // fail-open
    QObject::connect(&client, &OnlineActivationClient::activationFailed,
        [&](const QString&, const QJsonObject&) { loop.quit(); }); // fail-open
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    client.checkStatus(machineId);
    timeout.start();
    loop.exec();

    if (invalidated) {
        QFile::remove(manager->licenseFilePath());
        QMessageBox::warning(nullptr,
            i18n->text("app.activation.title"),
            i18n->text("app.activation.revoked_on_server"));
        return false;
    }
    return true;
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

    requestBtn_ = new QPushButton(i18n_->text("app.activation.request"), this);
    requestBtn_->setMinimumHeight(60);
    layout->addWidget(requestBtn_);
    auto* requestHint = new QLabel(i18n_->text("app.activation.request_hint"), this);
    requestHint->setAlignment(Qt::AlignHCenter);
    layout->addWidget(requestHint);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    layout->addWidget(new QLabel(i18n_->text("app.activation.invite_code_section"), this));

    auto* inviteRow = new QHBoxLayout;
    inviteCodeEdit_ = new QLineEdit(this);
    inviteCodeEdit_->setPlaceholderText(i18n_->text("app.activation.invite_code_placeholder"));
    inviteBtn_ = new QPushButton(i18n_->text("app.activation.invite_code"), this);
    inviteRow->addWidget(inviteCodeEdit_, 1);
    inviteRow->addWidget(inviteBtn_);
    layout->addLayout(inviteRow);

    auto* separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::HLine);
    separator2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator2);
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
    inviteCodeEdit_->setEnabled(!onlineRequestPending && !waitingApproval);
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
    const QString inviteCode = inviteCodeEdit_->text().trimmed();
    if (inviteCode.isEmpty()) {
        setState(i18n_->text("app.activation.invalid_key"), true);
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
    inviteCodeEdit_->clear();
    accept();
}

void ActivationDialog::onActivationFailed(const QString& errorCode, const QJsonObject& params) {
    stopPolling();
    state_ = IDLE;
    setState(activationErrorText(i18n_, errorCode, params), true);
}

void ActivationDialog::onStatusReceived(const QString& status, const QDate& expiresAt) {
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
    setState(statusText(i18n_, status), true);
}

void ActivationDialog::onNetworkError(const QString& detail) {
    stopPolling();
    state_ = IDLE;
    const QString base = i18n_->text("app.activation.network_unavailable");
    const QString text = detail.isEmpty() ? base
                                          : base + QStringLiteral(" (") + detail + QStringLiteral(")");
    setState(text, true);
}

}  // namespace bf

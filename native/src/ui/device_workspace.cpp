#include "ui/fluent_controls.h"
#include "ui/device_workspace.h"
#include "controller/acquisition_view.h"
#include "ui/waveform_widget.h"
#include "ui/control_bar.h"

#include <QAbstractSocket>
#include <QComboBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkInterface>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QToolTip>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>

namespace bf {

namespace {
QString profilePath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("device-profiles.json"));
}

bool validIpv4(const QString& value) {
    QHostAddress address;
    return address.setAddress(value) && address.protocol() == QAbstractSocket::IPv4Protocol &&
        !address.isNull() && address != QHostAddress::AnyIPv4 && address != QHostAddress::Broadcast && !address.isMulticast();
}

QString networkFingerprint() {
    QStringList addresses;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        for (const auto& entry : iface.addressEntries())
            addresses.append(iface.name() + QLatin1Char('=') + entry.ip().toString());
    }
    addresses.sort();
    return addresses.join(QLatin1Char('|'));
}
} // namespace

DeviceWorkspace::DeviceWorkspace(AcquisitionController* controller, I18nService* i18n, QWidget* parent, WaveformWidget* sharedWaveform)
    : QWidget(parent), controller_(controller), i18n_(i18n) {
    setObjectName("deviceWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    auto headingFont = font(); headingFont.setBold(true); headingFont.setPointSizeF(headingFont.pointSizeF() + 1);
    auto bold = font(); bold.setBold(true);
    // The connection center owns navigation and configuration. The live view
    // shares the controller, so browsing here never stops acquisition.
    managerHeader_ = new QFrame(this); managerHeader_->setObjectName("deviceManagerHeader");
    auto* header = new QVBoxLayout(managerHeader_); header->setContentsMargins(12, 10, 12, 6); header->setSpacing(6);
    auto* headerTop = new QHBoxLayout;
    managerTitle_ = new QLabel(managerHeader_); managerTitle_->setFont(headingFont);
    managerState_ = new QLabel(managerHeader_); managerState_->setObjectName("deviceManagerState");
    headerTop->addWidget(managerTitle_); headerTop->addStretch(); headerTop->addWidget(managerState_);
    managerIntro_ = new QLabel(managerHeader_); managerIntro_->setWordWrap(true); managerIntro_->setProperty("deviceMuted", true);
    auto* connectionRow = new QHBoxLayout;
    managerSummary_ = new QLabel(managerHeader_); managerSummary_->setObjectName("deviceManagerSummary");
    managerSummary_->setWordWrap(true); managerSummary_->setTextFormat(Qt::PlainText);
    listCurrent_ = new ActionButton(managerHeader_); listCurrent_->setObjectName("deviceViewCurrent"); listCurrent_->setProperty("devicePrimary", true);
    managerDisconnect_ = new ActionButton(managerHeader_); managerDisconnect_->setObjectName("deviceManagerDisconnect");
    connectionRow->addWidget(managerSummary_, 1); connectionRow->addWidget(managerDisconnect_); connectionRow->addWidget(listCurrent_);
    header->addLayout(headerTop); managerIntro_->hide(); header->addLayout(connectionRow);
    root->addWidget(managerHeader_);
    auto* center = new QHBoxLayout; center->setContentsMargins(0, 0, 0, 0); center->setSpacing(0);
    pages_ = new QStackedWidget(this); pages_->setObjectName("devicePages");
    // Hidden live controls must not impose their minimum width on the center.
    pages_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); center->addWidget(pages_, 1);
    root->addLayout(center, 1);

    // A bounded content column keeps every device's action beside its details.
    const auto contentPage = [this](QWidget* page, QWidget*& content) {
        auto* scroll = new QScrollArea(page); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* canvas = new QWidget(scroll); canvas->setObjectName("deviceManagerCanvas");
        auto* column = new QHBoxLayout(canvas); column->setContentsMargins(12, 10, 12, 10);
        content = new QWidget(canvas); content->setMaximumWidth(920);
        auto* layout = new QVBoxLayout(content); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(8);
        column->addWidget(content, 1); column->addStretch(0); scroll->setWidget(canvas);
        auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->addWidget(scroll);
        return layout;
    };
    const auto sectionTitle = [&](QWidget* parent) {
        auto* label = new QLabel(parent); auto f = bold; f.setPointSizeF(f.pointSizeF() + 1); label->setFont(f); label->setWordWrap(true); return label;
    };
    const auto panel = [](QWidget* parent) {
        auto* frame = new QFrame(parent); frame->setFrameShape(QFrame::StyledPanel);
        auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(6); return frame;
    };
    listPage_ = new QWidget(pages_); listPage_->setObjectName("deviceListPage");
    QWidget* listContent; auto* list = contentPage(listPage_, listContent);
    listTitle_ = sectionTitle(listContent);
    listIntro_ = new QLabel(listContent); listIntro_->setWordWrap(true); listIntro_->setProperty("deviceMuted", true);
    addHeading_ = sectionTitle(listContent);
    auto* rtPanel = panel(listContent); auto* rtLayout = qobject_cast<QVBoxLayout*>(rtPanel->layout());
    auto* rtRow = new QHBoxLayout; rtRow->setSpacing(20);
    auto* rtInfo = new QVBoxLayout; rtInfo->setSpacing(6);
    rtTitle_ = sectionTitle(rtPanel);
    rtDescription_ = new QLabel(rtPanel); rtDescription_->setWordWrap(true); rtDescription_->setProperty("deviceMuted", true);
    rtInfo->addWidget(rtTitle_); rtInfo->addWidget(rtDescription_); rtRow->addLayout(rtInfo, 1);
    listWifi_ = new ActionButton(rtPanel); listWifi_->setObjectName("deviceSelectRtBci"); listWifi_->setProperty("devicePrimary", true); rtRow->addWidget(listWifi_);
    rtLayout->addLayout(rtRow);
    listSimulator_ = new ActionButton(listContent); listSimulator_->setObjectName("deviceSelectSimulator");
    listTcp_ = new ActionButton(listContent); listTcp_->setObjectName("deviceSelectTcp");
    listNotice_ = new QLabel(listContent); listNotice_->setWordWrap(true); listNotice_->setProperty("deviceNotice", true);
    recentHeading_ = sectionTitle(listContent);
    profileNotice_ = new QLabel(listContent); profileNotice_->setWordWrap(true); profileNotice_->setObjectName("deviceProfileNotice"); profileNotice_->setProperty("deviceNotice", true);
    recentLayout_ = new QVBoxLayout; recentLayout_->setSpacing(10);
    listTitle_->hide(); listIntro_->hide(); addHeading_->hide(); list->addWidget(rtPanel);
    connectionAdvancedToggle_ = new QToolButton(listContent); connectionAdvancedToggle_->setObjectName("deviceConnectionAdvanced");
    connectionAdvancedToggle_->setCheckable(true); connectionAdvancedToggle_->setAutoRaise(true); connectionAdvancedToggle_->setFocusPolicy(Qt::StrongFocus); connectionAdvancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); connectionAdvancedToggle_->setArrowType(Qt::RightArrow);
    connectionAdvanced_ = new QWidget(listContent); connectionAdvanced_->setObjectName("deviceConnectionAlternatives"); connectionAdvanced_->hide();
    auto* alternatives = new QHBoxLayout(connectionAdvanced_); alternatives->setContentsMargins(0,0,0,0);
    simulatorProfileLabel_ = new QLabel(connectionAdvanced_);
    simulatorProfile_ = new ChoiceBox(connectionAdvanced_); simulatorProfile_->setObjectName("deviceSimulatorProfile");
    simulatorProfile_->addItem(QString(), true); simulatorProfile_->addItem(QString(), false);
    connect(simulatorProfile_, &QComboBox::currentIndexChanged, this, [this] {
        simulatorProfile_->setToolTip(simulatorProfile_->currentData(Qt::ToolTipRole).toString());
        simulatorProfile_->setAccessibleDescription(simulatorProfile_->toolTip());
    });
    simulatorProfileLabel_->setBuddy(simulatorProfile_);
    alternatives->addWidget(listTcp_); alternatives->addWidget(simulatorProfileLabel_);
    alternatives->addWidget(simulatorProfile_); alternatives->addWidget(listSimulator_); alternatives->addStretch();
    connect(connectionAdvancedToggle_, &QToolButton::toggled, this, [this](bool open) {
        connectionAdvanced_->setVisible(open); connectionAdvancedToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    list->addWidget(connectionAdvancedToggle_, 0, Qt::AlignLeft); list->addWidget(connectionAdvanced_); list->addWidget(listNotice_); list->addSpacing(16);
    list->addWidget(recentHeading_); list->addWidget(profileNotice_); list->addLayout(recentLayout_); list->addStretch();
    pages_->addWidget(listPage_);

    wifiPage_ = new QWidget(pages_); wifiPage_->setObjectName("deviceWifiPage");
    QWidget* wifiContent; auto* wifi = contentPage(wifiPage_, wifiContent);
    wifiBack_ = new ActionButton(wifiContent); wifiBack_->setObjectName("deviceWifiBack");
    wifiTitle_ = sectionTitle(wifiContent);
    wifiNetworkHeading_ = sectionTitle(wifiContent);
    auto* networkPanel = panel(wifiContent); auto* network = qobject_cast<QVBoxLayout*>(networkPanel->layout());
    wifiStatus_ = new QLabel(networkPanel); wifiStatus_->setFont(bold); wifiStatus_->setWordWrap(true); wifiStatus_->setObjectName("deviceWifiStatus");
    wifiDetail_ = new QLabel(networkPanel); wifiDetail_->setWordWrap(true); wifiDetail_->setObjectName("deviceWifiDetail");
    wifiDetail_->setTextFormat(Qt::PlainText); wifiDetail_->setProperty("deviceMuted", true);
    wifiSettings_ = new ActionButton(networkPanel); wifiSettings_->setObjectName("deviceWifiSettings");
    wifiRetry_ = new ActionButton(networkPanel); wifiRetry_->setObjectName("deviceWifiRetry");
    auto* networkActions = new QHBoxLayout; networkActions->addWidget(wifiSettings_); networkActions->addWidget(wifiRetry_); networkActions->addStretch();
    network->addWidget(wifiStatus_); network->addWidget(wifiDetail_); network->addLayout(networkActions);
    wifiGainNotice_ = new QLabel(wifiContent); wifiGainNotice_->setWordWrap(true); wifiGainNotice_->setProperty("deviceMuted", true);
    wifiError_ = new QLabel(wifiContent); wifiError_->setWordWrap(true); wifiError_->setObjectName("deviceWifiError"); wifiError_->setProperty("deviceNotice", true);
    wifiError_->setTextFormat(Qt::PlainText);
    wifiError_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    wifiCopyDiagnostics_ = new ActionButton(wifiContent); wifiCopyDiagnostics_->setObjectName("deviceWifiCopyDiagnostics");
    wifiCopyDiagnostics_->hide();
    wifiParametersHeading_ = sectionTitle(wifiContent);
    wifiNameLabel_ = new QLabel(wifiContent); wifiName_ = new TextInput(wifiContent); wifiName_->setObjectName("deviceWifiName"); wifiName_->setMaxLength(80);
    wifiChannelsLabel_ = new QLabel(wifiContent); wifiChannels_ = new ChoiceBox(wifiContent); wifiChannels_->setObjectName("deviceWifiChannels");
    wifiChannels_->addItem(QString(), 0); wifiChannels_->addItem(QStringLiteral("8"), 8); wifiChannels_->addItem(QStringLiteral("16"), 16);
    wifiRateLabel_ = new QLabel(wifiContent); wifiRate_ = new ChoiceBox(wifiContent); wifiRate_->setObjectName("deviceWifiRate");
    for (int value : {250, 500, 1000}) wifiRate_->addItem(QString::number(value) + QStringLiteral(" Hz"), value);
    auto* basic = new QFormLayout; basic->setHorizontalSpacing(24); basic->setVerticalSpacing(14); basic->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); basic->setRowWrapPolicy(QFormLayout::WrapLongRows);
    basic->addRow(wifiChannelsLabel_, wifiChannels_);
    wifiAdvancedToggle_ = new QToolButton(wifiContent); wifiAdvancedToggle_->setObjectName("deviceWifiAdvanced");
    wifiAdvancedToggle_->setCheckable(true); wifiAdvancedToggle_->setAutoRaise(true); wifiAdvancedToggle_->setFocusPolicy(Qt::StrongFocus); wifiAdvancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); wifiAdvancedToggle_->setArrowType(Qt::RightArrow);
    wifiAdvanced_ = new QWidget(wifiContent); wifiAdvanced_->setVisible(false);
    auto* advanced = new QFormLayout(wifiAdvanced_); advanced->setContentsMargins(0,12,0,0); advanced->setHorizontalSpacing(24); advanced->setVerticalSpacing(12); advanced->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); advanced->setRowWrapPolicy(QFormLayout::WrapLongRows);
    wifiHostLabel_ = new QLabel(wifiAdvanced_); wifiHost_ = new TextInput(QStringLiteral("192.168.4.1"), wifiAdvanced_); wifiHost_->setObjectName("deviceWifiHost");
    wifiHttpLabel_ = new QLabel(wifiAdvanced_); wifiHttpPort_ = new QSpinBox(wifiAdvanced_); wifiHttpPort_->setObjectName("deviceWifiHttpPort"); wifiHttpPort_->setRange(1,65535); wifiHttpPort_->setValue(80);
    wifiListenLabel_ = new QLabel(wifiAdvanced_); wifiListenPort_ = new QSpinBox(wifiAdvanced_); wifiListenPort_->setObjectName("deviceWifiListenPort"); wifiListenPort_->setRange(1,65535); wifiListenPort_->setValue(RtBciWifiOptions{}.listenPort);
    wifiLocalLabel_ = new QLabel(wifiAdvanced_); wifiLocal_ = new TextInput(wifiAdvanced_); wifiLocal_->setObjectName("deviceWifiLocalAddress");
    wifiIdLabel_ = new QLabel(wifiAdvanced_); wifiExpectedId_ = new TextInput(wifiAdvanced_); wifiExpectedId_->setObjectName("deviceWifiExpectedId");
    wifiHost_->setMaxLength(64); wifiLocal_->setMaxLength(64); wifiExpectedId_->setMaxLength(256);
    wifiGainLabel_ = new QLabel(wifiAdvanced_); wifiGain_ = new ChoiceBox(wifiAdvanced_); wifiGain_->setObjectName("deviceWifiGain");
    wifiGain_->addItem(QString(), 0); for (int value : {1,2,4,6,8,12,24}) wifiGain_->addItem(QString::number(value), value);
    wifiReferenceLabel_ = new QLabel(wifiAdvanced_); wifiReference_ = new QDoubleSpinBox(wifiAdvanced_); wifiReference_->setObjectName("deviceWifiReference");
    wifiReference_->setRange(0.01, 10.0); wifiReference_->setDecimals(3); wifiReference_->setValue(4.5); wifiReference_->setSuffix(QStringLiteral(" V"));
    advanced->addRow(wifiNameLabel_, wifiName_); advanced->addRow(wifiRateLabel_, wifiRate_);
    advanced->addRow(wifiHostLabel_, wifiHost_); advanced->addRow(wifiHttpLabel_, wifiHttpPort_); advanced->addRow(wifiListenLabel_, wifiListenPort_);
    advanced->addRow(wifiLocalLabel_, wifiLocal_); advanced->addRow(wifiIdLabel_, wifiExpectedId_); advanced->addRow(wifiGainLabel_, wifiGain_); advanced->addRow(wifiReferenceLabel_, wifiReference_);

    wifiConnect_ = new ActionButton(wifiContent); wifiConnect_->setObjectName("deviceWifiConnect"); wifiConnect_->setProperty("devicePrimary", true);
    wifiNext_ = new QLabel(wifiContent); wifiNext_->setWordWrap(true); wifiNext_->setProperty("deviceMuted", true); wifiNext_->setObjectName("deviceWifiNext");
    auto* wifiFooter = new QFrame(wifiPage_); wifiFooter->setObjectName("deviceConnectionFooter");
    auto* wifiActions = new QHBoxLayout(wifiFooter); wifiActions->setContentsMargins(12, 8, 12, 8); wifiActions->setSpacing(16);
    wifiActions->addWidget(wifiNext_, 1); wifiActions->addWidget(wifiConnect_);
    qobject_cast<QVBoxLayout*>(wifiPage_->layout())->addWidget(wifiFooter);
    headerTop->insertWidget(0, wifiBack_);
    auto* wifiHeading = new QHBoxLayout; wifiHeading->addWidget(wifiTitle_, 1);
    wifi->addLayout(wifiHeading); wifi->addSpacing(6);
    wifiNetworkHeading_->hide(); wifiParametersHeading_->hide();
    wifi->addWidget(networkPanel); wifi->addWidget(wifiError_);
    wifi->addWidget(wifiCopyDiagnostics_, 0, Qt::AlignLeft); wifi->addLayout(basic);
    advanced->addRow(wifiGainNotice_);
    wifi->addWidget(wifiAdvancedToggle_, 0, Qt::AlignLeft); wifi->addWidget(wifiAdvanced_); wifi->addStretch();
    pages_->addWidget(wifiPage_);

    livePage_ = new QWidget(pages_); livePage_->setObjectName("deviceLivePage"); pages_->addWidget(livePage_);
    auto* live = new QVBoxLayout(livePage_); live->setContentsMargins(0,0,0,0); live->setSpacing(6);
    auto* top = new ControlBar(livePage_); top->setWrappedMinimum(true);
    liveList_ = new ActionButton(livePage_); liveList_->setObjectName("deviceLiveList");
    title_ = new QLabel(livePage_); title_->setFont(bold);
    status_ = new QLabel(livePage_); status_->setWordWrap(true);
    acquire_ = new ActionButton(livePage_); acquire_->setObjectName("deviceAcquire");
    record_ = new ActionButton(livePage_); record_->setObjectName("deviceRecord");
    openLogs_ = new ActionButton(livePage_); openLogs_->setObjectName("deviceOpenLogs");
    connect(openLogs_, &QPushButton::clicked, controller_, &AcquisitionController::requestOpenLogs);
    freeze_ = new ActionButton(livePage_); freeze_->setObjectName("deviceFreeze");
    liveAdvancedToggle_ = new QToolButton(livePage_); liveAdvancedToggle_->setObjectName("deviceLiveAdvanced");
    liveAdvancedToggle_->setCheckable(true); liveAdvancedToggle_->setAutoRaise(true); liveAdvancedToggle_->setFocusPolicy(Qt::StrongFocus); liveAdvancedToggle_->setArrowType(Qt::RightArrow); liveAdvancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    top->addWidget(title_); top->addWidget(status_); top->addSeparator();
    if (!sharedWaveform) top->addWidget(liveList_); else liveList_->hide();
    top->addWidget(acquire_); top->addWidget(record_); top->addWidget(openLogs_); top->addWidget(liveAdvancedToggle_);
    live->addWidget(top);
    // Viewing controls stay next to the signal and never configure the device.
    auto* viewing = new ControlBar(livePage_); viewing->setWrappedMinimum(true);
    viewing->setObjectName("deviceViewControls");
    viewWindowLabel_ = new QLabel(livePage_);
    viewWindow_ = new ChoiceBox(livePage_); viewWindow_->setObjectName("deviceViewWindow");
    viewWindowLabel_->setBuddy(viewWindow_);
    for (double seconds : {.25, .5, 1., 2., 5., 10.}) viewWindow_->addItem(QString(), seconds);
    viewWindow_->setCurrentIndex(3);
    viewChannelLabel_ = new QLabel(livePage_);
    viewChannel_ = new ChoiceBox(livePage_); viewChannel_->setObjectName("deviceViewChannel");
    viewChannelLabel_->setBuddy(viewChannel_);
    viewChannel_->addItem(QString(), -1); viewChannel_->setMinimumContentsLength(8);
    viewChannel_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    freeze_->setCheckable(true);
    resetView_ = new ActionButton(livePage_); resetView_->setObjectName("deviceResetView");
    viewHelp_ = new QToolButton(livePage_); viewHelp_->setObjectName("deviceViewHelp");
    viewHelp_->setAutoRaise(false); viewHelp_->setFocusPolicy(Qt::StrongFocus);
    viewHelp_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(viewHelp_, &QToolButton::clicked, this, [this] {
        if (viewHelpOverride && viewHelpOverride()) return;
        QToolTip::showText(viewHelp_->mapToGlobal(viewHelp_->rect().bottomLeft()), text("app.device.view_only_hint"), viewHelp_);
    });
    viewStatus_ = new QLabel(livePage_); viewStatus_->setObjectName("deviceViewStatus");
    viewing->addPair(viewWindowLabel_, viewWindow_); viewing->addPair(viewChannelLabel_, viewChannel_);
    viewing->addWidget(freeze_); viewing->addWidget(resetView_); viewing->addWidget(viewHelp_); viewing->addWidget(viewStatus_);
    live->addWidget(viewing);
    if (sharedWaveform) {
        // The shared channel list and waveform toolbar already provide these choices.
        viewWindow_->parentWidget()->hide();
        viewChannel_->parentWidget()->hide();
        for (auto* control : std::vector<QWidget*>{freeze_, resetView_, viewHelp_, viewStatus_}) top->addWidget(control);
        viewing->hide();
    }
    livePage_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    liveAdvanced_ = new QWidget(livePage_); liveAdvanced_->setObjectName("deviceLiveDetails"); liveAdvanced_->hide();
    auto* details = new QVBoxLayout(liveAdvanced_); details->setContentsMargins(8, 6, 8, 6); details->setSpacing(6);
    deviceHeading_ = new QLabel(liveAdvanced_); deviceHeading_->setFont(bold);
    sourceLabel_ = new QLabel(liveAdvanced_); sourceLabel_->hide();
    source_ = new ChoiceBox(liveAdvanced_); source_->setObjectName("deviceSource"); source_->addItem(QString(), false); source_->addItem(QString(), true); source_->hide();
    hostLabel_ = new QLabel(this); host_ = new TextInput(QStringLiteral("127.0.0.1"), this); host_->setObjectName("deviceTcpHost");
    portLabel_ = new QLabel(this); port_ = new QSpinBox(this); port_->setObjectName("deviceTcpPort"); port_->setRange(1, 65535); port_->setValue(45120);
    identityLabel_ = new QLabel(this); expectedIdentity_ = new TextInput(this); expectedIdentity_->setObjectName("deviceTcpExpectedId");
    tcpNotice_ = new QLabel(this); tcpNotice_->setWordWrap(true);
    device_ = new QLabel(liveAdvanced_); device_->setWordWrap(true);
    connect_ = new ActionButton(liveAdvanced_); connect_->setObjectName("deviceConnect");
    unsupported_ = new QLabel(liveAdvanced_); unsupported_->hide();
    settingsHeading_ = new QLabel(liveAdvanced_); settingsHeading_->setFont(bold);
    rateLabel_ = new QLabel(liveAdvanced_);
    rate_ = new ChoiceBox(liveAdvanced_); rate_->setObjectName("deviceSampleRate");
    for (int rate : {250, 500, 1000}) rate_->addItem(QString::number(rate) + " Hz", rate);
    apply_ = new ActionButton(liveAdvanced_); apply_->setObjectName("deviceApply");
    hint_ = new QLabel(liveAdvanced_); hint_->setObjectName("deviceSettingsHint"); hint_->setWordWrap(true);
    quality_ = new QLabel(liveAdvanced_); quality_->setWordWrap(true);
    channelLimit_ = new QLabel(liveAdvanced_); channelLimit_->setWordWrap(true);
    scaleNotice_ = new QLabel(liveAdvanced_); scaleNotice_->setWordWrap(true);
    auto* configuration = new ControlBar(liveAdvanced_); configuration->setWrappedMinimum(true);
    configuration->addPair(rateLabel_, rate_); configuration->addWidget(apply_); configuration->addWidget(connect_);
    details->addWidget(device_); details->addWidget(configuration); details->addWidget(hint_); details->addWidget(quality_);
    details->addWidget(channelLimit_); details->addWidget(scaleNotice_);
    deviceHeading_->hide(); settingsHeading_->hide();
    live->addWidget(liveAdvanced_);
    connect(liveAdvancedToggle_, &QToolButton::toggled, this, [this](bool open) {
        liveAdvanced_->setVisible(open); liveAdvancedToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    sharedWaveform_ = sharedWaveform != nullptr;
    traces_ = sharedWaveform ? sharedWaveform : new WaveformWidget(livePage_);
    if (!sharedWaveform_) traces_->setViewInteractionsEnabled(true);
    if (!sharedWaveform_) { traces_->setObjectName("deviceTraceView"); traces_->setMinimumSize(320,180); live->addWidget(traces_, 1); }
    recordStatus_ = new QLabel(livePage_); recordStatus_->setWordWrap(true); recordStatus_->setObjectName("deviceRecordStatus");
    auto* recordingNotice = new QWidget(livePage_);
    auto* recordingLayout = new QHBoxLayout(recordingNotice);
    recordingLayout->setContentsMargins(0, 0, 0, 0);
    recordStatus_->setMinimumWidth(0);
    recordStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    recordingNotice_ = recordingNotice;
    openRecording_ = new ActionButton(livePage_); openRecording_->setObjectName("deviceOpenRecording");
    recordingLayout->addWidget(recordStatus_, 1);
    if (sharedWaveform_) top->addWidget(openRecording_);
    else recordingLayout->addWidget(openRecording_);
    live->addWidget(recordingNotice);
    connect(openRecording_, &QPushButton::clicked, this, [this] {
        if (captureOpenRequested) captureOpenRequested(controller_->snapshot().recording.path);
    });

    tcpPage_ = new QWidget(pages_); tcpPage_->setObjectName("deviceTcpPage");
    QWidget* tcpContent; auto* tcpLayout = contentPage(tcpPage_, tcpContent);
    tcpTitle_ = sectionTitle(tcpContent);
    tcpIntro_ = new QLabel(tcpContent); tcpIntro_->setWordWrap(true); tcpIntro_->setProperty("deviceMuted", true);
    auto* tcpPanel = panel(tcpContent); auto* tcpPanelLayout = qobject_cast<QVBoxLayout*>(tcpPanel->layout());
    auto* tcpForm = new QFormLayout; tcpForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    tcpForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); tcpForm->setHorizontalSpacing(24); tcpForm->setVerticalSpacing(14);
    tcpForm->addRow(hostLabel_, host_); tcpForm->addRow(portLabel_, port_); tcpForm->addRow(identityLabel_, expectedIdentity_);
    tcpPanelLayout->addLayout(tcpForm); tcpNotice_->setParent(tcpPanel); tcpNotice_->setProperty("deviceMuted", true); tcpPanelLayout->addWidget(tcpNotice_);
    tcpConnect_ = new ActionButton(tcpContent); tcpConnect_->setObjectName("deviceTcpConnect"); tcpConnect_->setProperty("devicePrimary", true);
    tcpError_ = new QLabel(tcpContent); tcpError_->setObjectName("deviceTcpError"); tcpError_->setTextFormat(Qt::PlainText); tcpError_->setWordWrap(true); tcpError_->setProperty("deviceNotice", true);
    tcpLayout->addWidget(tcpTitle_); tcpLayout->addWidget(tcpIntro_); tcpLayout->addSpacing(8); tcpLayout->addWidget(tcpPanel); tcpLayout->addWidget(tcpError_);
    tcpLayout->addWidget(tcpConnect_, 0, Qt::AlignLeft); tcpLayout->addStretch(); pages_->addWidget(tcpPage_);

    connect(pages_, &QStackedWidget::currentChanged, this, [this] { updateManagerNavigation(); });
    connect(managerDisconnect_, &QPushButton::clicked, this, [this] {
        if (!switchBlocked()) controller_->disconnectDevice();
    });
    connect(tcpConnect_, &QPushButton::clicked, this, [this] {
        if (controller_->snapshot().connection == DeviceConnectionState::Connected && adapter_ == Adapter::Tcp) {
            showLive(); return;
        }
        if (controller_->snapshot().connection == DeviceConnectionState::Connecting && adapter_ == Adapter::Tcp) {
            controller_->disconnectDevice(); return;
        }
        if (switchBlocked() || host_->text().trimmed().isEmpty()) return;
        pendingAdapter_ = Adapter::Tcp;
        if (controller_->snapshot().connection == DeviceConnectionState::Connected) {
            waitingForDisconnect_ = true; controller_->disconnectDevice();
        } else handlePendingConnection();
    });

    probe_ = new RtBciWifiProbe(this); probe_->setObjectName("deviceWifiProbe");
    probeTimer_ = new QTimer(this); probeTimer_->setSingleShot(true);
    networkTimer_ = new QTimer(this); networkTimer_->setInterval(2000);
    connect(probeTimer_, &QTimer::timeout, this, [this] {
        if (probeInFlight_) { probe_->cancel(); probeInFlight_ = false; probeGeneration_ = 0; }
        probeOnce();
    });
    connect(networkTimer_, &QTimer::timeout, this, [this] {
        if (!isVisible() || pages_->currentWidget() != wifiPage_ || switchBlocked() || wifiConnecting_) return;
        const auto current = networkFingerprint();
        if (current != networkFingerprint_) { networkFingerprint_ = current; startProbeRound(); }
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && isVisible() && pages_->currentWidget() == wifiPage_ && !wifiConnecting_)
            startProbeRound();
    });
    connect(probe_, &RtBciWifiProbe::finished, this, [this](quint64 generation, const RtBciProbeResult& result) {
        if (!probeInFlight_ || generation != probeGeneration_ || !isVisible() || pages_->currentWidget() != wifiPage_ || switchBlocked()) return;
        probeTimer_->stop(); probeInFlight_ = false;
        probeCompatible_ = result.compatible && result.error.code.isEmpty(); probeError_ = result.error;
        detectedDeviceId_ = result.deviceId; detectedModel_ = result.model; detectedLocalAddress_ = result.localAddress;
        if (probeCompatible_ && wifiChannels_->currentData().toInt() == 0 && (result.channels == 8 || result.channels == 16))
            wifiChannels_->setCurrentIndex(wifiChannels_->findData(result.channels));
        if (!probeCompatible_ && probesRemaining_ > 0) probeTimer_->start(2500);
        refreshConnectionPage();
    });
    connect(listWifi_, &QPushButton::clicked, this, [this] { if (!switchBlocked()) showWifi(); });
    connect(listCurrent_, &QPushButton::clicked, this, [this] { showLive(); });
    connect(listSimulator_, &QPushButton::clicked, this, [this] {
        if (switchBlocked()) return;
        pendingSimulatorIca_ = simulatorProfile_->currentData().toBool();
        pendingAdapter_ = Adapter::Simulator;
        if (controller_->snapshot().connection == DeviceConnectionState::Connected) {
            waitingForDisconnect_ = true; controller_->disconnectDevice();
        } else handlePendingConnection();
    });
    connect(listTcp_, &QPushButton::clicked, this, [this] { showTcp(); });
    connect(liveList_, &QPushButton::clicked, this, [this] {
        if (connectionsRequested) connectionsRequested(); else showDeviceList();
    });
    connect(wifiBack_, &QPushButton::clicked, this, [this] { showDeviceList(); });
    connect(wifiSettings_, &QPushButton::clicked, this, [this] { openWifiSettings(); });
    connect(wifiRetry_, &QPushButton::clicked, this, [this] { startProbeRound(); });
    connect(wifiCopyDiagnostics_, &QPushButton::clicked, this, [this] {
        const auto view = controller_->snapshot();
        const auto error = !wifiConnectionError_.code.isEmpty() ? wifiConnectionError_ :
            !probeError_.code.isEmpty() ? probeError_ : view.error;
        const QJsonObject diagnostic{{"schema", 1}, {"adapter", "rt-bci.wifi"},
            {"app_version", property("appVersion").toString()},
            {"code", error.code}, {"params", error.params}};
        QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(diagnostic).toJson(QJsonDocument::Indented)));
    });
    connect(wifiConnect_, &QPushButton::clicked, this, [this] {
        if (wifiConnecting_) {
            const bool cancelConnection = wifiOpenStarted_;
            wifiConnecting_ = false; wifiOpenStarted_ = false; waitingForDisconnect_ = false; pendingAdapter_ = Adapter::None;
            reprobeWhenIdle_ = true;
            if (cancelConnection) controller_->disconnectDevice();
            handlePendingConnection();
        } else beginWifiConnection();
    });
    connect(wifiAdvancedToggle_, &QToolButton::toggled, this, [this](bool open) {
        wifiAdvanced_->setVisible(open); wifiAdvancedToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(wifiHost_, &QLineEdit::textChanged, this, [this] { if (pages_->currentWidget() == wifiPage_) startProbeRound(); });
    connect(wifiHttpPort_, &QSpinBox::valueChanged, this, [this] { if (pages_->currentWidget() == wifiPage_) startProbeRound(); });
    connect(wifiChannels_, &QComboBox::currentIndexChanged, this, [this] { refreshConnectionPage(); });
    connect(wifiGain_, &QComboBox::currentIndexChanged, this, [this] { refreshConnectionPage(); });
    networkFingerprint_ = networkFingerprint(); networkTimer_->start();
    loadProfiles();

    timer_ = new QTimer(this); timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (isVisible() || liveViewActive_ || livePage_->isVisible()) refresh();
    });
    connect(controller_, &AcquisitionController::stateChanged, this, [this] { handlePendingConnection(); refresh(true); });
    connect(rate_, &QComboBox::currentIndexChanged, this, [this] { refresh(true); });
    connect(source_, &QComboBox::currentIndexChanged, this, [this] {
        const QSignalBlocker blocker(rate_);
        rate_->setCurrentIndex(-1); lastAppliedRate_ = -1;
        refresh(true);
    });
    connect(host_, &QLineEdit::textChanged, this, [this] { refresh(true); });
    connect(connect_, &QPushButton::clicked, this, [this] {
        const auto state = controller_->snapshot();
        if (state.connection == DeviceConnectionState::Connected || state.connection == DeviceConnectionState::Connecting) {
            autoStartPending_ = false; ++autoStartGeneration_;
            controller_->disconnectDevice();
        }
        else if (adapter_ == Adapter::Wifi) {
            const auto it = std::find_if(profiles_.cbegin(), profiles_.cend(), [this](const Profile& p) { return p.id == selectedProfileId_; });
            showWifi(it == profiles_.cend() ? nullptr : &*it);
        }
        else if (source_->currentData().toBool()) {
            autoStartPending_ = true;
            controller_->connectTcp(host_->text(), static_cast<quint16>(port_->value()), expectedIdentity_->text());
        } else { autoStartPending_ = true; controller_->connectSimulator(simulatorProfile_->currentData().toBool()); }
    });
    connect(apply_, &QPushButton::clicked, this, [this] { controller_->configureSampleRate(rate_->currentData().toInt()); });
    connect(acquire_, &QPushButton::clicked, this, [this] {
        const auto state = controller_->snapshot();
        if (state.acquisition == DeviceAcquisitionState::Streaming || state.acquisition == DeviceAcquisitionState::Starting)
            controller_->stopAcquisition();
        else { frozen_ = false; frozenTraces_.clear(); viewEndOffset_ = 0; controller_->startAcquisition(); }
    });
    connect(record_, &QPushButton::clicked, this, [this] {
        const auto state = controller_->snapshot().recording.state;
        if (state == CaptureRecordState::Recording || state == CaptureRecordState::Opening) controller_->stopRecording();
        else chooseRecordingDirectory();
    });
    connect(freeze_, &QPushButton::clicked, this, [this] {
        if (pauseViewOverride && pauseViewOverride()) { refresh(true); return; }
        frozen_ = !frozen_;
        viewEndOffset_ = 0;
        if (frozen_) frozenTraces_ = controller_->snapshot().traces;
        else frozenTraces_.clear();
        refresh(true);
    });
    connect(viewWindow_, &QComboBox::currentIndexChanged, this, [this] { refresh(true); });
    connect(viewChannel_, &QComboBox::currentIndexChanged, this, [this] { refresh(true); });
    // Shared-renderer gestures belong to MainWindow; this panel owns only the
    // live timeline. Standalone device windows dispatch their own gestures.
    if (!sharedWaveform_) {
        connect(traces_, &WaveformWidget::pauseViewRequested, this, &DeviceWorkspace::toggleViewPause);
        connect(traces_, &WaveformWidget::timeZoomRequested, this, &DeviceWorkspace::zoomViewTime);
        connect(traces_, &WaveformWidget::viewPanRequested, this, &DeviceWorkspace::panView);
        connect(traces_, &WaveformWidget::resetViewRequested, this, &DeviceWorkspace::resetView);
    }
    connect(traces_, &WaveformWidget::amplitudeZoomChanged, this, [this] {
        if (!sharedWaveform_ || liveViewActive_) refresh(true);
    });
    connect(resetView_, &QPushButton::clicked, this, [this] {
        if (resetViewOverride && resetViewOverride()) { refresh(true); return; }
        const QSignalBlocker windowBlocker(viewWindow_), channelBlocker(viewChannel_);
        viewWindow_->setCurrentIndex(viewWindow_->findData(2.)); viewChannel_->setCurrentIndex(0);
        viewEndOffset_ = 0; appliedViewChannel_ = -2; traces_->setAmplitudeZoom(1); refresh(true);
    });
    timer_->start();
    setDarkTheme(false);
    retranslateUi();
    if (!sharedWaveform_) setViewTabOrder();
    updateManagerNavigation();
}

bool DeviceWorkspace::switchBlocked() const {
    const auto view = controller_->snapshot();
    return controller_->hasActiveTasks() || view.connection == DeviceConnectionState::Connecting || view.configurationInProgress;
}

QWidget* DeviceWorkspace::takeLiveControls() {
    pages_->removeWidget(livePage_);
    livePage_->setParent(nullptr);
    return livePage_;
}

void DeviceWorkspace::setLiveViewActive(bool active) {
    if (!sharedWaveform_ && active && !liveViewActive_) {
        traces_->clearTimeRange(); traces_->setViewState(WaveformViewState{});
        appliedViewChannel_ = -2;
        traces_->setMarkers({}); traces_->setMarkerModeEnabled(false);
        traces_->setCursorTime(std::numeric_limits<double>::quiet_NaN());
        traces_->setAutoScaleEnabled(true);
    }
    liveViewActive_ = active;
    if (!sharedWaveform_) {
        traces_->setViewInteractionsEnabled(active);
        traces_->setToolTip(active ? text("app.device.view_only_hint") : QString());
    }
    if (active) setViewTabOrder();
    if (active) updateWaveform();
    refresh(true);
}

void DeviceWorkspace::setViewTabOrder() {
    // The shared renderer is in a different window until live controls are
    // reparented into the browser. Set the chain once they share that window.
    const std::vector<QWidget*> controls{acquire_, record_, openLogs_, liveAdvancedToggle_, viewWindow_, viewChannel_,
                                       freeze_, resetView_, viewHelp_, traces_};
    for (std::size_t i = 1; i < controls.size(); ++i) QWidget::setTabOrder(controls[i-1], controls[i]);
}

void DeviceWorkspace::openConnections() {
    connectionAdvancedToggle_->setChecked(false);
    showDeviceList();
}

void DeviceWorkspace::updateWaveform() {
    const auto view = controller_->snapshot();
    if (signalViewRequested) {
        if(signalChannelNames){const auto names=signalChannelNames();if(names!=viewChannelNames_){
            const QSignalBlocker block(viewChannel_);viewChannelNames_=names;viewChannel_->clear();viewChannel_->addItem(text("app.device.view_all_channels"),-1);
            for(int i=0;i<names.size();++i)viewChannel_->addItem(names[i],i);
        }}
        const auto range=acquisitionTraceTimeRange(frozen_?frozenTraces_:view.traces);
        const double seconds=viewWindow_->currentData().toDouble();
        const double last=range?range->last:seconds, first=range?range->first:0;
        viewEndOffset_=frozen_?std::clamp(viewEndOffset_,0.,std::max(0.,last-first-seconds)):0.;
        const double end=last-viewEndOffset_;
        signalViewRequested(std::max(first,end-seconds),end,frozen_,viewChannel_->currentData().toInt(),seconds);
        return;
    }
    auto slice = acquisitionWaveformSlice(frozen_ ? frozenTraces_ : view.traces);
    for (auto& unit : slice.channelUnits) if (unit.isEmpty()) unit = text("app.device.unknown_unit");
    if (viewChannelNames_ != slice.channelNames) {
        viewChannelNames_ = slice.channelNames;
        const QSignalBlocker blocker(viewChannel_);
        viewChannel_->clear(); viewChannel_->addItem(text("app.device.view_all_channels"), -1);
        for (int i = 0; i < viewChannelNames_.size(); ++i) viewChannel_->addItem(viewChannelNames_[i], i);
        appliedViewChannel_ = -2;
    }
    viewChannel_->setEnabled(!slice.channelNames.isEmpty());
    const int selectedChannel = viewChannel_->currentData().toInt();
    if (appliedViewChannel_ != selectedChannel) {
        WaveformViewState display;
        if (selectedChannel >= 0)
            for (int i = 0; i < slice.channelNames.size(); ++i) if (i != selectedChannel) display.setChannelVisible(i, false);
        traces_->setViewState(display); appliedViewChannel_ = selectedChannel;
    }
    traces_->setSlice(slice);
    const double seconds = viewWindow_->currentData().toDouble();
    viewEndOffset_ = frozen_ ? std::clamp(viewEndOffset_, 0., std::max(0., slice.t1 - slice.t0 - seconds)) : 0.;
    const double end = slice.t1 - viewEndOffset_;
    const double start = std::max(slice.t0, end - seconds);
    if (std::all_of(slice.data.begin(), slice.data.end(), [](const auto& values) { return values.empty(); }))
        traces_->setTimeRange(0, seconds);
    else traces_->setTimeRange(start, end);
    traces_->setViewPanningEnabled(frozen_);
    traces_->setAxisLabels(text("app.analysis.time"), text("app.device.signal_amplitude"));
}

void DeviceWorkspace::completeConnection() {
    if (!autoStartPending_) return;
    autoStartPending_ = false; frozen_ = false; frozenTraces_.clear();
    viewEndOffset_ = 0;
    if (!sharedWaveform_) traces_->setAmplitudeZoom(1);
    liveAdvancedToggle_->setChecked(false);
    const auto generation = ++autoStartGeneration_;
    showLive();
    // The connection operation has completed before acquisition is requested.
    // A cancellation or source change invalidates this deferred request.
    QTimer::singleShot(0, this, [this, generation] {
        const auto view = controller_->snapshot();
        if (generation == autoStartGeneration_ && view.connection == DeviceConnectionState::Connected &&
            view.acquisition == DeviceAcquisitionState::Idle && !view.configurationInProgress)
            controller_->startAcquisition();
    });
}

void DeviceWorkspace::setViewSeconds(double seconds) {
    if (!std::isfinite(seconds)) return;
    seconds = std::clamp(seconds, .25, 10.);
    int index = viewWindow_->findData(seconds);
    if (index < 0) {
        // The shared numeric editor accepts values between the live presets.
        // Reuse one custom entry instead of silently ignoring those values.
        index = viewWindow_->findData(true, Qt::UserRole + 1);
        if (index < 0) { index = viewWindow_->count(); viewWindow_->addItem(QString()); }
        const QSignalBlocker blocker(viewWindow_);
        viewWindow_->setItemData(index, seconds);
        viewWindow_->setItemData(index, true, Qt::UserRole + 1);
        viewWindow_->setItemText(index, text("app.device.view_seconds", {{"seconds", i18n_->qlocale().toString(seconds, 'g', 3)}}));
    }
    viewWindow_->setCurrentIndex(index);
    refresh(true);
}

void DeviceWorkspace::zoomViewTime(int steps, double anchor) {
    const std::vector<double> presets{.25, .5, 1., 2., 5., 10.};
    double seconds = viewWindow_->currentData().toDouble();
    for (int i = 0; i < std::abs(steps); ++i) {
        if (steps > 0) {
            const auto it = std::lower_bound(presets.begin(), presets.end(), seconds);
            seconds = it == presets.begin() ? presets.front() : *std::prev(it);
        } else {
            const auto it = std::upper_bound(presets.begin(), presets.end(), seconds);
            seconds = it == presets.end() ? presets.back() : *it;
        }
    }
    if (seconds == viewWindow_->currentData().toDouble()) return;
    if (frozen_) {
        const auto old = traces_->timeRange();
        if (const auto full = acquisitionTraceTimeRange(frozenTraces_)) {
            const double point = old.first + anchor * (old.second - old.first);
            viewEndOffset_ = std::max(0., full->last - point - (1 - anchor) * seconds);
        }
    }
    setViewSeconds(seconds);
}

void DeviceWorkspace::panView(double seconds) {
    if (!frozen_) return;
    viewEndOffset_ += seconds; appliedViewChannel_ = -2; refresh(true);
}

void DeviceWorkspace::toggleViewPause() { freeze_->click(); }
void DeviceWorkspace::resetView() { resetView_->click(); }

void DeviceWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (pages_->currentWidget() == wifiPage_ && !wifiConnecting_) startProbeRound();
    refresh(true);
}

void DeviceWorkspace::hideEvent(QHideEvent* event) {
    cancelProbe();
    QWidget::hideEvent(event);
}

void DeviceWorkspace::showDeviceList() {
    cancelWifiConnection();
    pages_->setCurrentWidget(listPage_);
    rebuildRecent();
    refresh(true);
}

void DeviceWorkspace::cancelWifiConnection() {
    cancelProbe();
    autoStartPending_ = false; ++autoStartGeneration_;
    const bool cancelConnection = wifiConnecting_ && wifiOpenStarted_;
    wifiConnecting_ = wifiOpenStarted_ = waitingForDisconnect_ = reprobeWhenIdle_ = false;
    pendingAdapter_ = Adapter::None;
    if (cancelConnection) controller_->disconnectDevice();
}

void DeviceWorkspace::showTcp() {
    if (switchBlocked()) return;
    cancelWifiConnection();
    pages_->setCurrentWidget(tcpPage_);
    refresh(true);
}

void DeviceWorkspace::updateManagerNavigation() {
    managerHeader_->setVisible(pages_->currentWidget() != livePage_);
    wifiBack_->setVisible(pages_->currentWidget() != listPage_);
}

void DeviceWorkspace::showLive() {
    cancelProbe();
    if (liveRequested) liveRequested(); else pages_->setCurrentWidget(livePage_);
    refresh(true);
}

void DeviceWorkspace::showWifi(const Profile* profile) {
    if (switchBlocked()) return;
    cancelProbe();
    wifiConnecting_ = wifiOpenStarted_ = waitingForDisconnect_ = false;
    reprobeWhenIdle_ = false;
    wifiConnectionError_ = {}; networkSettingsError_ = false;
    selectedProfileId_ = profile ? profile->id : QString();
    const RtBciWifiOptions options = profile ? profile->options : RtBciWifiOptions{};
    wifiName_->setText(profile ? profile->name : QStringLiteral("RT-BCI"));
    wifiHost_->setText(options.host); wifiHttpPort_->setValue(options.httpPort);
    wifiListenPort_->setValue(options.listenPort); wifiLocal_->setText(options.localAddress);
    wifiExpectedId_->setText(options.expectedDeviceId);
    wifiChannels_->setCurrentIndex(wifiChannels_->findData(options.channels));
    wifiRate_->setCurrentIndex(wifiRate_->findData(options.sampleRate));
    wifiGain_->setCurrentIndex(wifiGain_->findData(options.gain)); wifiReference_->setValue(options.referenceVolts);
    wifiAdvancedToggle_->setChecked(false);
    pages_->setCurrentWidget(wifiPage_);
    startProbeRound();
    refresh(true);
}

void DeviceWorkspace::cancelProbe() {
    probeTimer_->stop();
    probe_->cancel();
    probeGeneration_ = 0; probeInFlight_ = false; probesRemaining_ = 0;
}

void DeviceWorkspace::startProbeRound() {
    cancelProbe();
    probeCompatible_ = false; probeError_ = {};
    detectedDeviceId_.clear(); detectedModel_.clear(); detectedLocalAddress_.clear();
    if (!isVisible() || pages_->currentWidget() != wifiPage_ || wifiConnecting_ || switchBlocked() || wifiHost_->text().trimmed().isEmpty()) {
        refreshConnectionPage(); return;
    }
    probesRemaining_ = 4;
    probeTimer_->start(350); // Debounce address edits and interface changes.
    refreshConnectionPage();
}

void DeviceWorkspace::probeOnce() {
    if (!isVisible() || pages_->currentWidget() != wifiPage_ || wifiConnecting_ || switchBlocked() || probesRemaining_ <= 0) {
        refreshConnectionPage(); return;
    }
    --probesRemaining_;
    probeInFlight_ = true;
    probeGeneration_ = probe_->probe(wifiHost_->text().trimmed(), static_cast<quint16>(wifiHttpPort_->value()));
    probe_->setProperty("activeGeneration", QVariant::fromValue(probeGeneration_));
    probeTimer_->start(11000); // Let the reference ten-second HTTP deadline report first.
    refreshConnectionPage();
}

QString DeviceWorkspace::errorText(const AppError& error) const {
    const QString key = QStringLiteral("error.") + error.code;
    const QString translated = i18n_->text(key, error.params);
    QString result = translated == key ? text("app.device.error_detail", {{"code", error.code}}) : translated;
    const auto& params = error.params;
    if (params.value("reason") == "channel_mismatch") {
        result += QLatin1Char('\n') + text("app.device.channel_conflict_detail", {
            {"requested", QString::number(params.value("requested_channels").toInt())},
            {"reported", QString::number(params.value("reported_channels").toInt())}});
    } else if (params.contains("path")) {
        const int status = params.value("http_status").toInt();
        const auto reason = params.value("reason").toString();
        const QString detail = reason == "timeout" ? text("app.device.request_timeout") :
            reason == "rejected" ? text("app.device.request_rejected") :
            status ? QStringLiteral("HTTP %1").arg(status) : text("app.device.request_transport");
        result += QLatin1Char('\n') + text("app.device.request_detail", {{"method", params.value("method")},
            {"path", params.value("path")}, {"result", detail}});
    }
    if (!params.value("response_error").toString().isEmpty())
        result += QLatin1Char('\n') + text("app.device.response_detail", {{"detail", params.value("response_error")}});
    if (error.code == "device.rt_bci_callback" && !params.value("local_address").toString().isEmpty())
        result += QLatin1Char('\n') + text("app.device.callback_detail", {{"address", params.value("local_address")},
            {"port", QString::number(params.value("listen_port").toInt())}});
    return result;
}

void DeviceWorkspace::openWifiSettings() {
    if (switchBlocked() || wifiConnecting_) return;
    bool opened = false;
#ifdef Q_OS_WIN
    opened = QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:network-wifi")));
#elif defined(Q_OS_LINUX)
    const QString gnome = QStandardPaths::findExecutable(QStringLiteral("gnome-control-center"));
    const QString kde = QStandardPaths::findExecutable(QStringLiteral("kcmshell6"));
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nm-connection-editor"));
    if (!gnome.isEmpty()) opened = QProcess::startDetached(gnome, {QStringLiteral("wifi")});
    else if (!kde.isEmpty()) opened = QProcess::startDetached(kde, {QStringLiteral("kcm_networkmanagement")});
    else if (!nm.isEmpty()) opened = QProcess::startDetached(nm, {});
    else opened = QDesktopServices::openUrl(QUrl(QStringLiteral("settings://wifi")));
#else
    opened = QDesktopServices::openUrl(QUrl(QStringLiteral("settings://wifi")));
#endif
    networkSettingsError_ = !opened;
    refreshConnectionPage();
}

void DeviceWorkspace::beginWifiConnection() {
    if (switchBlocked() || !probeCompatible_ || wifiConnecting_ || wifiChannels_->currentData().toInt() == 0) return;
    const QString host = wifiHost_->text().trimmed();
    if (!validIpv4(host) || (!wifiLocal_->text().trimmed().isEmpty() && !validIpv4(wifiLocal_->text().trimmed()))) {
        wifiConnectionError_ = makeError(QStringLiteral("device.invalid_endpoint"));
        wifiAdvancedToggle_->setChecked(true); refreshConnectionPage(); return;
    }
    pendingWifiOptions_.host = host;
    pendingWifiOptions_.httpPort = static_cast<quint16>(wifiHttpPort_->value());
    pendingWifiOptions_.listenPort = static_cast<quint16>(wifiListenPort_->value());
    pendingWifiOptions_.localAddress = wifiLocal_->text().trimmed();
    pendingWifiOptions_.channels = wifiChannels_->currentData().toInt();
    pendingWifiOptions_.sampleRate = wifiRate_->currentData().toInt();
    pendingWifiOptions_.gain = wifiGain_->currentData().toInt();
    pendingWifiOptions_.referenceVolts = wifiReference_->value();
    pendingWifiOptions_.expectedDeviceId = wifiExpectedId_->text().trimmed();
    if (pendingWifiOptions_.expectedDeviceId.isEmpty()) pendingWifiOptions_.expectedDeviceId = detectedDeviceId_;
    wifiConnectionError_ = {}; wifiConnecting_ = true; wifiOpenStarted_ = false; reprobeWhenIdle_ = false;
    pendingAdapter_ = Adapter::Wifi; cancelProbe();
    if (controller_->snapshot().connection == DeviceConnectionState::Connected) {
        waitingForDisconnect_ = true; controller_->disconnectDevice();
    } else handlePendingConnection();
    refreshConnectionPage();
}

void DeviceWorkspace::handlePendingConnection() {
    const auto view = controller_->snapshot();
    if (reprobeWhenIdle_ && view.connection != DeviceConnectionState::Connecting && !controller_->hasActiveTasks()) {
        reprobeWhenIdle_ = false;
        QTimer::singleShot(0, this, [this] {
            if (pages_->currentWidget() == wifiPage_ && !wifiConnecting_) startProbeRound();
        });
    }
    if (wifiConnecting_ && wifiOpenStarted_) {
        if (view.connection == DeviceConnectionState::Connected) {
            wifiConnecting_ = wifiOpenStarted_ = false; adapter_ = Adapter::Wifi;
            saveProfile(); completeConnection();
        } else if (view.connection == DeviceConnectionState::Faulted) {
            wifiConnecting_ = wifiOpenStarted_ = false;
            wifiConnectionError_ = view.error;
            if (wifiConnectionError_.code == QLatin1String("device.rt_bci_callback") ||
                wifiConnectionError_.code.contains(QStringLiteral("bind")) || wifiConnectionError_.code.contains(QStringLiteral("route")))
                wifiAdvancedToggle_->setChecked(true);
            refreshConnectionPage();
        }
        return;
    }
    if (autoStartPending_ && view.connection == DeviceConnectionState::Connected && pendingAdapter_ == Adapter::None) completeConnection();
    if (view.connection == DeviceConnectionState::Faulted) autoStartPending_ = false;
    if (pendingAdapter_ == Adapter::None) return;
    if (waitingForDisconnect_) {
        if (view.connection != DeviceConnectionState::Disconnected && view.connection != DeviceConnectionState::Faulted) return;
        waitingForDisconnect_ = false;
    } else if (view.connection == DeviceConnectionState::Connected || view.connection == DeviceConnectionState::Connecting) return;
    const auto next = pendingAdapter_; pendingAdapter_ = Adapter::None;
    // Defer the next command so a source's terminal state signal has returned.
    QTimer::singleShot(0, this, [this, next] {
        if (next == Adapter::Wifi) {
            if (!wifiConnecting_ || pages_->currentWidget() != wifiPage_) return;
            wifiOpenStarted_ = true; autoStartPending_ = true; controller_->connectWifi(pendingWifiOptions_);
        } else if ((pages_->currentWidget() == listPage_ || pages_->currentWidget() == tcpPage_) && !controller_->hasActiveTasks()) {
            adapter_ = next; source_->setCurrentIndex(next == Adapter::Tcp ? 1 : 0);
            autoStartPending_ = true;
            if (next == Adapter::Simulator) controller_->connectSimulator(pendingSimulatorIca_);
            else controller_->connectTcp(host_->text(), static_cast<quint16>(port_->value()), expectedIdentity_->text());
        }
    });
}

void DeviceWorkspace::refreshConnectionPage() {
    if (!wifiStatus_) return;
    const bool blocked = switchBlocked();
    const auto view = controller_->snapshot();
    QString status;
    if (wifiConnecting_) {
        const QString key = QStringLiteral("app.device.wifi_phase_") + view.connectionPhase;
        status = view.connectionPhase.isEmpty() ? text("app.device.connecting") : i18n_->text(key);
        if (status == key) status = text("app.device.connecting");
    } else if (probeCompatible_) status = text("app.device.wifi_response");
    else if (probeInFlight_ || probesRemaining_ > 0) status = text("app.device.wifi_probing");
    else status = text("app.device.wifi_waiting");
    wifiStatus_->setText(status);
    QString details = probeCompatible_ ? text("app.device.wifi_response_detail") : text("app.device.wifi_hotspot_help");
    if (probeCompatible_ && (!detectedModel_.isEmpty() || !detectedDeviceId_.isEmpty())) {
        QStringList info;
        if (!detectedModel_.isEmpty()) info << detectedModel_;
        if (!detectedDeviceId_.isEmpty()) info << detectedDeviceId_;
        details += QStringLiteral("\n") + info.join(QStringLiteral(" · "));
    }
    if (probeCompatible_ && !detectedLocalAddress_.isEmpty())
        details += QStringLiteral("\n") + text("app.device.wifi_route_detected", {{"address", detectedLocalAddress_}});
    wifiDetail_->setText(details);
    wifiGainNotice_->setText(text(wifiGain_->currentData().toInt() == 0 ? "app.device.wifi_raw_gain" : "app.device.wifi_declared_gain"));
    QString error;
    if (networkSettingsError_) error = text("app.device.wifi_settings_failed");
    if (!wifiConnectionError_.code.isEmpty()) error = errorText(wifiConnectionError_);
    else if (!probeError_.code.isEmpty()) error = errorText(probeError_);
    wifiError_->setText(error); wifiError_->setVisible(!error.isEmpty());
    wifiCopyDiagnostics_->setVisible(!wifiConnectionError_.code.isEmpty() || !probeError_.code.isEmpty());
    const bool editable = !wifiConnecting_ && !blocked;
    for (QWidget* widget : std::initializer_list<QWidget*>{wifiName_, wifiChannels_, wifiRate_, wifiAdvancedToggle_, wifiAdvanced_}) widget->setEnabled(editable);
    wifiSettings_->setEnabled(editable);
    wifiRetry_->setVisible(!probeCompatible_ && !probeInFlight_ && probesRemaining_ == 0 && !wifiConnecting_);
    wifiRetry_->setEnabled(editable);
    wifiConnect_->setText(text(wifiConnecting_ ? "app.device.cancel_connection" : "app.device.wifi_connect"));
    wifiConnect_->setEnabled(wifiConnecting_ || (editable && probeCompatible_ && wifiChannels_->currentData().toInt() != 0));
    wifiNext_->setText(text(!probeCompatible_ ? "app.device.next_network" :
        wifiChannels_->currentData().toInt() == 0 ? "app.device.next_channels" : "app.device.next_connect"));
}

void DeviceWorkspace::loadProfiles() {
    profiles_.clear(); profilesWarning_ = false; profilesWritable_ = true;
    QFile file(profilePath());
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) {
        profilesWarning_ = true; profilesWritable_ = false; return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || root.value("schema").toInt(-1) != 1 || !root.value("profiles").isArray()) {
        profilesWarning_ = true; profilesWritable_ = false; return;
    }
    const auto records = root.value("profiles").toArray();
    if (records.size() > 16) profilesWarning_ = true;
    for (const auto& value : records) {
        if (profiles_.size() >= 16) break;
        const auto item = value.toObject();
        Profile p;
        p.id = item.value("id").toString(); p.name = item.value("name").toString();
        p.deviceId = item.value("device_id").toString(); p.model = item.value("model").toString();
        p.lastVerified = item.value("last_verified").toString();
        p.options.host = item.value("host").toString();
        const int http = item.value("http_port").toInt(-1), listen = item.value("listen_port").toInt(-1);
        p.options.localAddress = item.value("local_address").toString();
        p.options.channels = item.value("channels").toInt(-1); p.options.sampleRate = item.value("sample_rate").toInt(-1);
        p.options.gain = item.value("gain").toInt(-1); p.options.referenceVolts = item.value("reference_volts").toDouble(-1);
        p.options.expectedDeviceId = item.value("expected_device_id").toString();
        const auto gain = p.options.gain;
        const bool valid = value.isObject() && item.value("adapter").toString() == QLatin1String("rt-bci.wifi") &&
            !p.id.isEmpty() && p.id.size() <= 80 && !p.name.trimmed().isEmpty() && p.name.size() <= 80 &&
            p.deviceId.size() <= 256 && p.model.size() <= 256 && p.options.expectedDeviceId.size() <= 256 &&
            validIpv4(p.options.host) && (p.options.localAddress.isEmpty() || validIpv4(p.options.localAddress)) &&
            http > 0 && http <= 65535 && listen > 0 && listen <= 65535 &&
            (p.options.channels == 8 || p.options.channels == 16) &&
            (p.options.sampleRate == 250 || p.options.sampleRate == 500 || p.options.sampleRate == 1000) &&
            (gain == 0 || gain == 1 || gain == 2 || gain == 4 || gain == 6 || gain == 8 || gain == 12 || gain == 24) &&
            std::isfinite(p.options.referenceVolts) && p.options.referenceVolts >= 0.01 && p.options.referenceVolts <= 10.0 &&
            QDateTime::fromString(p.lastVerified, Qt::ISODate).isValid() &&
            std::none_of(profiles_.begin(), profiles_.end(), [&](const Profile& existing) { return existing.id == p.id; });
        if (!valid) { profilesWarning_ = true; continue; }
        p.options.httpPort = static_cast<quint16>(http); p.options.listenPort = static_cast<quint16>(listen);
        profiles_.push_back(std::move(p));
    }
}

void DeviceWorkspace::saveProfile() {
    if (!profilesWritable_) { profilesWarning_ = true; return; }
    Profile p;
    p.id = selectedProfileId_.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : selectedProfileId_;
    p.name = wifiName_->text().trimmed(); if (p.name.isEmpty()) p.name = QStringLiteral("RT-BCI");
    p.deviceId = detectedDeviceId_.left(256); p.model = detectedModel_.left(256);
    p.lastVerified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    p.options = pendingWifiOptions_;
    if (p.options.expectedDeviceId.isEmpty()) p.options.expectedDeviceId = p.deviceId;
    profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const Profile& old) { return old.id == p.id; }), profiles_.end());
    profiles_.insert(profiles_.begin(), p);
    if (profiles_.size() > 16) profiles_.resize(16);
    if (!persistProfiles()) profilesWarning_ = true;
    selectedProfileId_ = p.id;
}

bool DeviceWorkspace::persistProfiles() {
    if (!profilesWritable_) return false;
    QJsonArray records;
    for (const auto& profile : profiles_) {
        const auto& o = profile.options;
        records.append(QJsonObject{{"id", profile.id}, {"name", profile.name}, {"adapter", "rt-bci.wifi"},
            {"device_id", profile.deviceId}, {"model", profile.model}, {"last_verified", profile.lastVerified},
            {"host", o.host}, {"http_port", o.httpPort}, {"listen_port", o.listenPort}, {"local_address", o.localAddress},
            {"channels", o.channels}, {"sample_rate", o.sampleRate}, {"gain", o.gain},
            {"reference_volts", o.referenceVolts}, {"expected_device_id", o.expectedDeviceId}});
    }
    const QString path = profilePath();
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly)) return false;
    const QByteArray data = QJsonDocument(QJsonObject{{"schema", 1}, {"profiles", records}}).toJson(QJsonDocument::Indented);
    return file.write(data) == data.size() && file.commit();
}

void DeviceWorkspace::rebuildRecent() {
    while (auto* item = recentLayout_->takeAt(0)) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    if (profiles_.empty()) {
        auto* empty = new QLabel(text("app.device.recent_empty"), listPage_); empty->setWordWrap(true);
        empty->setProperty("deviceMuted", true); empty->setContentsMargins(4, 12, 4, 20);
        recentLayout_->addWidget(empty);
    }
    for (const auto& profile : profiles_) {
        auto* row = new QFrame(listPage_); row->setProperty("devicePanel", true);
        auto* layout = new QHBoxLayout(row); layout->setContentsMargins(8, 6, 8, 6); layout->setSpacing(12);
        auto* info = new QVBoxLayout;
        auto* name = new TextInput(profile.name, row); name->setObjectName("deviceRecentName"); name->setMaxLength(80);
        name->setAccessibleName(text("app.device.wifi_name"));
        connect(name, &QLineEdit::editingFinished, this, [this, name, id = profile.id] {
            auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.id == id; });
            if (it == profiles_.end()) return;
            const QString edited = name->text().trimmed();
            if (edited.isEmpty()) { name->setText(it->name); return; }
            if (edited == it->name) return;
            const QString previous = it->name; it->name = edited;
            if (!persistProfiles()) {
                it->name = previous; name->setText(previous); profilesWarning_ = true;
                profileNotice_->setText(text("app.device.profile_unavailable")); profileNotice_->setVisible(true);
            }
        });
        const auto when = QDateTime::fromString(profile.lastVerified, Qt::ISODate).toLocalTime();
        auto* detail = new QLabel(text("app.device.recent_detail", {{"channels", profile.options.channels},
            {"date", i18n_->qlocale().toString(when, QLocale::ShortFormat)}}), row);
        detail->setWordWrap(true); detail->setTextFormat(Qt::PlainText);
        info->addWidget(name); info->addWidget(detail); layout->addLayout(info, 1);
        auto* reconnect = new ActionButton(text("app.device.reconnect"), row); reconnect->setObjectName("deviceRecentConnect");
        reconnect->setProperty("profileId", profile.id); reconnect->setEnabled(!switchBlocked()); layout->addWidget(reconnect);
        connect(reconnect, &QPushButton::clicked, this, [this, id = profile.id] {
            const auto it = std::find_if(profiles_.cbegin(), profiles_.cend(), [&](const Profile& p) { return p.id == id; });
            if (it != profiles_.cend()) showWifi(&*it);
        });
        auto* remove = new ActionButton(text("app.device.remove_profile"), row); remove->setObjectName("deviceRecentRemove");
        remove->setToolTip(text("app.device.remove_profile_help")); layout->addWidget(remove);
        connect(remove, &QPushButton::clicked, this, [this, id = profile.id] {
            const auto previous = profiles_;
            profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.id == id; }), profiles_.end());
            if (!persistProfiles()) {
                profiles_ = previous; profilesWarning_ = true;
                profileNotice_->setText(text("app.device.profile_unavailable")); profileNotice_->show(); return;
            }
            if (selectedProfileId_ == id) selectedProfileId_.clear();
            QTimer::singleShot(0, this, [this] { rebuildRecent(); });
        });
        recentLayout_->addWidget(row);
    }
    profileNotice_->setVisible(profilesWarning_);
    profileNotice_->setText(profilesWarning_ ? text(profilesWritable_ ? "app.device.profile_invalid" : "app.device.profile_unavailable") : QString());
}
QString DeviceWorkspace::text(const char* key, const QJsonObject& params) const { return i18n_->text(QLatin1String(key), params); }
void DeviceWorkspace::setDarkTheme(bool dark) {
    traces_->setDarkTheme(dark);
    // All connection controls inherit the main window's palette and style.
}
void DeviceWorkspace::chooseRecordingDirectory() {
    const auto directory = QFileDialog::getExistingDirectory(this, text("app.device.choose_directory"));
    if (!directory.isEmpty()) controller_->startRecording(directory);
}
void DeviceWorkspace::retranslateUi() {
    openRecording_->setText(text("app.capture.view_recording"));
    viewWindowLabel_->setText(text("app.device.view_window"));
    viewChannelLabel_->setText(text("app.device.view_channels"));
    viewWindow_->setAccessibleName(viewWindowLabel_->text()); viewChannel_->setAccessibleName(viewChannelLabel_->text());
    for (int i = 0; i < viewWindow_->count(); ++i)
        viewWindow_->setItemText(i, text("app.device.view_seconds", {{"seconds", i18n_->qlocale().toString(viewWindow_->itemData(i).toDouble(), 'g', 3)}}));
    if (viewChannel_->count()) viewChannel_->setItemText(0, text("app.device.view_all_channels"));
    const auto viewHelp = text("app.device.view_only_hint");
    viewWindow_->setToolTip(viewHelp); viewChannel_->setToolTip(viewHelp); freeze_->setToolTip(viewHelp);
    resetView_->setText(text("app.device.view_reset")); resetView_->setToolTip(viewHelp);
    viewHelp_->setText(text("app.device.view_help")); viewHelp_->setAccessibleName(text("app.device.view_help")); viewHelp_->setToolTip(viewHelp);
    if (!sharedWaveform_) traces_->setToolTip(viewHelp);
    openLogs_->setText(text("app.device.open_logs"));
    liveAdvancedToggle_->setText(text("app.device.live_advanced")); connectionAdvancedToggle_->setText(text("app.device.connection_advanced"));
    managerTitle_->setText(text("app.device.manager_title")); managerIntro_->setText(text("app.device.manager_intro"));
    managerDisconnect_->setText(text("app.device.disconnect"));
    tcpTitle_->setText(text("app.device.manager_tcp_title")); tcpIntro_->setText(text("app.device.manager_tcp_intro"));
    wifiNetworkHeading_->setText(text("app.device.manager_wifi_network")); wifiParametersHeading_->setText(text("app.device.manager_wifi_parameters"));
    listTitle_->setText(text("app.device.list_title")); listIntro_->setText(text("app.device.list_intro"));
    addHeading_->setText(text("app.device.add_device")); rtTitle_->setText(text("app.device.rt_name")); rtDescription_->setText(text("app.device.rt_description"));
    listWifi_->setText(text("app.device.select_connect")); listSimulator_->setText(text("app.device.use_simulator"));
    simulatorProfileLabel_->setText(text("app.device.simulator_profile"));
    simulatorProfile_->setAccessibleName(text("app.device.simulator_profile"));
    simulatorProfile_->setItemText(0, text("app.device.simulator_ica"));
    simulatorProfile_->setItemText(1, text("app.device.simulator_periodic"));
    simulatorProfile_->setItemData(0, text("app.device.simulator_ica_hint"), Qt::ToolTipRole);
    simulatorProfile_->setItemData(1, text("app.device.simulator_periodic_hint"), Qt::ToolTipRole);
    simulatorProfile_->setToolTip(simulatorProfile_->currentData(Qt::ToolTipRole).toString());
    simulatorProfile_->setAccessibleDescription(simulatorProfile_->toolTip());
    listTcp_->setText(text("app.device.use_tcp")); listCurrent_->setText(text("app.device.view_current"));
    recentHeading_->setText(text("app.device.recent_heading")); liveList_->setText(text("app.device.manager_title"));
    wifiBack_->setText(text("app.device.back_to_list")); wifiTitle_->setText(text("app.device.wifi_title"));
    wifiSettings_->setText(text("app.device.wifi_open_settings")); wifiRetry_->setText(text("app.device.wifi_retry"));
    wifiCopyDiagnostics_->setText(text("app.device.copy_diagnostics"));
    wifiAdvancedToggle_->setText(text("app.device.wifi_advanced"));
    wifiNameLabel_->setText(text("app.device.wifi_name")); wifiName_->setAccessibleName(wifiNameLabel_->text());
    wifiChannelsLabel_->setText(text("app.device.wifi_channels")); wifiChannels_->setAccessibleName(wifiChannelsLabel_->text());
    wifiChannels_->setItemText(0, text("app.device.wifi_choose_channels"));
    wifiRateLabel_->setText(text("app.device.sample_rate")); wifiRate_->setAccessibleName(wifiRateLabel_->text());
    wifiHostLabel_->setText(text("app.device.wifi_host")); wifiHost_->setAccessibleName(wifiHostLabel_->text());
    wifiHttpLabel_->setText(text("app.device.wifi_http_port")); wifiHttpPort_->setAccessibleName(wifiHttpLabel_->text());
    wifiListenLabel_->setText(text("app.device.wifi_listen_port")); wifiListenPort_->setAccessibleName(wifiListenLabel_->text());
    wifiLocalLabel_->setText(text("app.device.wifi_local")); wifiLocal_->setAccessibleName(wifiLocalLabel_->text());
    wifiLocal_->setPlaceholderText(text("app.device.wifi_auto"));
    wifiIdLabel_->setText(text("app.device.expected_identity")); wifiExpectedId_->setAccessibleName(wifiIdLabel_->text());
    wifiGainLabel_->setText(text("app.device.wifi_gain")); wifiGain_->setAccessibleName(wifiGainLabel_->text());
    wifiGain_->setItemText(0, text("app.device.wifi_raw_count"));
    wifiReferenceLabel_->setText(text("app.device.wifi_reference")); wifiReference_->setAccessibleName(wifiReferenceLabel_->text());
    title_->setText(text("app.device.title"));
    deviceHeading_->setText(text("app.device.device_heading"));
    sourceLabel_->setText(text("app.device.source"));
    source_->setItemText(0, text("app.device.source_simulator"));
    source_->setItemText(1, text("app.device.source_tcp"));
    source_->setAccessibleName(text("app.device.source"));
    hostLabel_->setText(text("app.device.host")); host_->setAccessibleName(text("app.device.host"));
    portLabel_->setText(text("app.device.port")); port_->setAccessibleName(text("app.device.port"));
    identityLabel_->setText(text("app.device.expected_identity")); expectedIdentity_->setAccessibleName(text("app.device.expected_identity"));
    tcpNotice_->setText(text("app.device.tcp_plaintext"));
    settingsHeading_->setText(text("app.device.settings"));
    rateLabel_->setText(text("app.device.sample_rate"));
    apply_->setText(text("app.device.apply"));
    unsupported_->setText(text("app.device.unsupported"));
    connect_->setAccessibleName(text("app.device.connection_accessible"));
    rate_->setAccessibleName(text("app.device.sample_rate"));
    if (!sharedWaveform_) {
        traces_->setAccessibleName(text("app.device.trace_accessible"));
        traces_->setNumberLocale(i18n_->qlocale());
    }
    scaleNotice_->setText(text("app.device.adaptive_scale") + " " + text("app.device.relative_time_notice"));
    rebuildRecent(); refreshConnectionPage();
    refresh(true);
}
void DeviceWorkspace::refresh(bool forceStatus) {
    const auto view = controller_->snapshot();
    if (!forceStatus && statusRefresh_.isValid() && statusRefresh_.elapsed() < 500) {
        if (liveViewActive_ || (!sharedWaveform_ && isVisible())) updateWaveform();
        return;
    }
    if (statusRefresh_.isValid()) statusRefresh_.restart(); else statusRefresh_.start();
    const bool wifi = adapter_ == Adapter::Wifi;
    const bool tcp = !wifi && source_->currentData().toBool();
    const bool external = tcp || wifi;
    const bool connected = view.connection == DeviceConnectionState::Connected;
    const bool connecting = view.connection == DeviceConnectionState::Connecting;
    const bool idle = view.acquisition == DeviceAcquisitionState::Idle;
    const bool faulted = view.acquisition == DeviceAcquisitionState::Faulted;
    const bool starting = view.acquisition == DeviceAcquisitionState::Starting;
    const bool streaming = view.acquisition == DeviceAcquisitionState::Streaming;
    const bool stopping = view.acquisition == DeviceAcquisitionState::Stopping;
    const bool recording = view.recording.state == CaptureRecordState::Recording;
    const bool opening = view.recording.state == CaptureRecordState::Opening;
    const bool finalizing = view.recording.state == CaptureRecordState::Finalizing;
    const bool recordBusy = recording || opening || finalizing;
    const bool configuring = view.configurationInProgress;
    const bool blocked = switchBlocked();
    if (blocked && !wifiConnecting_ && (probeInFlight_ || probesRemaining_ > 0)) cancelProbe();
    listWifi_->setEnabled(!blocked); listSimulator_->setEnabled(!blocked); listTcp_->setEnabled(!blocked);
    simulatorProfile_->setEnabled(!blocked && !connecting);
    managerDisconnect_->setVisible(connected); managerDisconnect_->setEnabled(connected && !blocked);
    managerDisconnect_->setToolTip(blocked ? text("app.device.stop_before_disconnect") : QString());
    managerSummary_->setText(connected ? text("app.device.manager_current", {{"device", view.deviceName}}) :
        connecting ? text("app.device.connecting") : text("app.device.manager_no_connection"));
    tcpConnect_->setText(text(connected && tcp ? "app.device.view_current" : connecting && tcp ? "app.device.cancel_connection" : "app.device.connect_tcp"));
    tcpConnect_->setEnabled((connecting && tcp) || (!blocked && !host_->text().trimmed().isEmpty()));
    tcpError_->setText(tcp && !view.error.code.isEmpty() ? errorText(view.error) : QString());
    tcpError_->setVisible(!tcpError_->text().isEmpty());
    for (auto* button : listPage_->findChildren<QPushButton*>(QStringLiteral("deviceRecentConnect"))) button->setEnabled(!blocked);
    listCurrent_->setVisible(connected || controller_->hasActiveTasks() || (adapter_ != Adapter::None && view.connection == DeviceConnectionState::Faulted));
    listNotice_->setText(blocked ? text("app.device.switch_blocked") : QString());
    listNotice_->setVisible(blocked);
    refreshConnectionPage();
    title_->setText(wifi ? QStringLiteral("RT-BCI") : text("app.device.title"));
    const auto& exact = view.exactRate;
    const int effectiveRate = external ? (connected && exact.denominator == 1 && exact.numerator <= 2147483647u ?
        static_cast<int>(exact.numerator) : -1) : view.sampleRate;
    if (effectiveRate != lastAppliedRate_) {
        lastAppliedRate_ = effectiveRate;
        const QSignalBlocker blocker(rate_);
        rate_->setCurrentIndex(rate_->findData(effectiveRate));
    }
    const bool dirty = rate_->currentIndex() >= 0 &&
        (exact.denominator != 1 || rate_->currentData().toInt() != effectiveRate);
    sourceLabel_->hide(); source_->hide(); unsupported_->hide();
    source_->setEnabled(!connected && !connecting && !recordBusy);
    host_->setEnabled(!blocked && !(connected && tcp)); port_->setEnabled(!blocked && !(connected && tcp));
    expectedIdentity_->setEnabled(!blocked && !(connected && tcp));
    QString deviceText = wifi ? text(connected ? "app.device.wifi_connected" : "app.device.wifi_disconnected") :
                     tcp ? connected ? text("app.device.tcp_connected", {{"device", view.deviceName}}) : text("app.device.tcp_disconnected") :
                     connected ? text("app.device.simulator_connected") : text("app.device.simulator_disconnected");
    if (wifi && connected) {
        deviceText += QStringLiteral("\n") + text("app.device.wifi_requested", {{"channels", pendingWifiOptions_.channels}, {"rate", view.sampleRate}});
        deviceText += QStringLiteral("\n") + text(pendingWifiOptions_.gain == 0 ? "app.device.wifi_raw_gain" : "app.device.wifi_declared_gain");
        if (!view.configurationVerified) deviceText += QStringLiteral("\n") + text("app.device.wifi_config_unverified");
    }
    device_->setText(deviceText); device_->setTextFormat(Qt::PlainText);
    connect_->setText(connecting ? tcp ? text("app.device.cancel_connection") : text("app.device.connecting") :
                      connected ? text("app.device.disconnect") : wifi ? text("app.device.reconnect") : tcp ? text("app.device.connect_tcp") : text("app.device.connect_simulator"));
    connect_->setEnabled(connecting ? tcp : (!connected ? (!tcp || !host_->text().trimmed().isEmpty()) : ((idle || faulted) && !recordBusy && !configuring)));
    connect_->setToolTip(connect_->isEnabled() ? QString() :
        tcp && !connected && host_->text().trimmed().isEmpty() ? text("app.device.host") :
        configuring ? text("app.device.connecting") : text("app.device.stop_before_disconnect"));
    rate_->setEnabled(connected && idle && !recordBusy && view.canConfigure);
    apply_->setEnabled(connected && idle && !recordBusy && view.canConfigure && dirty);
    hint_->setText(starting || streaming || stopping ? text("app.device.stop_before_configure") : wifi && !view.configurationVerified ? text("app.device.wifi_config_unverified") :
        configuring ? text("app.device.connecting") : dirty && connected ? text("app.device.settings_unapplied") : tcp ?
        text(view.canConfigure ? "app.device.tcp_settings_available" : "app.device.tcp_settings_notice") : text("app.device.simulated_notice"));
    acquire_->setEnabled((connected && idle && !dirty && !recordBusy && !configuring) || starting || streaming);
    acquire_->setText(stopping ? text("app.device.stopping") : starting ? text("app.device.cancel_start") :
                      streaming && recordBusy ? text("app.device.stop_and_save") : streaming ? text("app.device.stop") : text("app.device.start"));
    acquire_->setToolTip(acquire_->isEnabled() ? QString() : dirty ? text("app.device.settings_unapplied") :
        finalizing ? text("app.device.record_finalizing") : text("app.device.connect_first"));
    record_->setEnabled((streaming && !finalizing) || opening || recording);
    record_->setText(finalizing ? text("app.device.finalizing") : opening ? text("app.device.cancel_record") :
                     recording ? text("app.device.stop_record") : text("app.device.start_record"));
    record_->setToolTip(record_->isEnabled() ? QString() : text("app.device.start_first"));
    const char* statusKey = connecting ? "app.device.status_connecting" : !connected ? "app.device.status_disconnected" : starting ? "app.device.status_starting" :
                        streaming ? (wifi ? "app.device.status_streaming_wifi" : view.simulated ? "app.device.status_streaming" : "app.device.status_streaming_tcp") : stopping ? "app.device.status_stopping" : "app.device.status_idle";
    if (view.acquisition == DeviceAcquisitionState::Faulted || view.connection == DeviceConnectionState::Faulted) statusKey = acquisitionFailureStatusKey(view);
    status_->setText(text(statusKey));
    managerState_->setText(text(statusKey));
    const QString missing = view.unknownGaps ? text("app.device.gaps_unknown", {{"count", QString::number(view.missingSamples)}}) :
                                             text("app.device.gaps", {{"count", QString::number(view.missingSamples)}});
    const auto& rate = view.exactRate;
    const QString rateText = (external && !connected) || !rate.numerator || !rate.denominator ? QStringLiteral("—") : rate.denominator == 1 ?
        QString::number(rate.numerator) : QString::number(rate.numerator) + "/" + QString::number(rate.denominator);
    const bool multipleStreams = std::any_of(view.traces.begin(), view.traces.end(), [](const auto& trace) { return trace.multipleStreams; });
    quality_->setText(text(multipleStreams ? "app.device.signal_summary_multiple" : "app.device.signal_summary",
                           {{"rate", rateText}, {"samples", QString::number(view.samples)}, {"gaps", missing}}));
    const bool limited = view.totalChannels > view.traces.size();
    channelLimit_->setVisible(limited && !signalViewRequested);
    if (limited) channelLimit_->setText(text("app.device.channel_limit", {{"shown", static_cast<int>(view.traces.size())}, {"total", static_cast<int>(view.totalChannels)}}));
    const char* recordKey = "app.device.record_off";
    switch (view.recording.state) {
    case CaptureRecordState::Off: break;
    case CaptureRecordState::Opening: recordKey = "app.device.record_opening"; break;
    case CaptureRecordState::Recording: recordKey = "app.device.record_active"; break;
    case CaptureRecordState::Finalizing: recordKey = "app.device.record_finalizing"; break;
    case CaptureRecordState::Saved: recordKey = "app.device.record_saved"; break;
    case CaptureRecordState::Partial: recordKey = view.recording.writtenSamples == 0 ? "app.device.record_empty" : "app.device.record_partial"; break;
    case CaptureRecordState::Failed: recordKey = "app.device.record_failed"; break;
    }
    QString recordText = text(recordKey, {{"path", view.recording.path}, {"samples", QString::number(view.recording.writtenSamples)}});
    const auto errorText = [this](const AppError& error) {
        const QString key = "error." + error.code;
        const QString translated = i18n_->text(key, error.params);
        return translated == key ? text("app.device.error_detail", {{"code", error.code}}) : translated;
    };
    if (!view.recording.error.code.isEmpty()) recordText += " — " + errorText(view.recording.error);
    if (!view.error.code.isEmpty()) recordText += " — " + errorText(view.error);
    recordStatus_->setText(recordText);
    const bool notice = recordBusy || view.recording.state == CaptureRecordState::Saved ||
        view.recording.state == CaptureRecordState::Partial || view.recording.state == CaptureRecordState::Failed || !view.error.code.isEmpty();
    if (recordingStatusChanged) {
        recordStatus_->hide(); recordingNotice_->hide();
        recordingStatusChanged(recordText, notice);
    } else {
        recordStatus_->setVisible(notice); recordingNotice_->setVisible(notice);
    }
    openRecording_->setVisible((view.recording.state == CaptureRecordState::Saved || view.recording.state == CaptureRecordState::Partial) && view.recording.writtenSamples > 0);
    openRecording_->setEnabled(static_cast<bool>(captureOpenRequested));
    freeze_->setText(frozen_ ? text("app.device.return_live") : text("app.device.freeze"));
    freeze_->setChecked(frozen_);
    freeze_->setEnabled(frozen_ || !view.traces.empty());
    resetView_->setEnabled(true);
    for (QWidget* control : {static_cast<QWidget*>(freeze_), static_cast<QWidget*>(resetView_), static_cast<QWidget*>(viewHelp_)})
        control->setToolTip(text("app.device.view_only_hint"));
    viewStatus_->setText(text(frozen_ ? (starting || streaming ? "app.device.frozen_notice" : "app.device.view_paused") :
        view.traces.empty() ? "app.device.view_waiting" : starting || streaming ? "app.device.live_notice" : "app.device.view_stopped"));
    if (std::abs(traces_->amplitudeZoom() - 1) > 1e-9)
        viewStatus_->setText(viewStatus_->text() + QStringLiteral(" · ") +
            text("app.device.view_gain", {{"gain", i18n_->qlocale().toString(traces_->amplitudeZoom(), 'g', 3)}}));
    viewHelp_->setVisible(true);
    if (viewControlsOverride) {
        if (const auto state = viewControlsOverride()) {
            viewHelp_->setVisible(state->helpVisible);
            freeze_->setText(state->pauseText); freeze_->setChecked(state->paused);
            freeze_->setEnabled(state->available); resetView_->setEnabled(state->available);
            viewStatus_->setText(state->status);
            for (QWidget* control : {static_cast<QWidget*>(freeze_), static_cast<QWidget*>(resetView_), static_cast<QWidget*>(viewHelp_)})
                control->setToolTip(state->hint);
        }
    }
    if (liveViewActive_ || (!sharedWaveform_ && isVisible())) updateWaveform();
}
} // namespace bf

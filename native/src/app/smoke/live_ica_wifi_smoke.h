#pragma once
#include <QRadioButton>
#include "ui/main_window.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <random>
#include <cmath>
#include <iostream>

namespace bf {
// Local test peer only. Encodes independent mixed sources in real 33-byte
// RT-BCI frames, including same-counter 16-channel banks and a report of 8.
class LiveIcaWifiFixture {
public:
    explicit LiveIcaWifiFixture(int channels) : channels_(channels) {
        server_.listen(QHostAddress::LocalHost, 0);
        timer_.setInterval(20);
        QObject::connect(&timer_, &QTimer::timeout, &server_, [this] {
            if (!data_ || data_->state() != QAbstractSocket::ConnectedState) return;
            QByteArray bytes;
            for (int t = 0; t < 5; ++t) {
                const double s = 4000 * std::sin(counter_ * .107), z = 6000 * std::pow(uniform_(random_), 3);
                const auto counter = counter_++ & 255;
                for (int bank = 0; bank < channels_ / 8; ++bank) {
                    QByteArray frame(33, '\0'); frame[0] = char(0xa0); frame[1] = char(counter); frame[32] = char(0xc0);
                    for (int c = 0; c < 8; ++c) {
                        const int channel = bank * 8 + c;
                        const double mixed = channel == 0 ? 2*s + z : channel == channels_-1 ? s - 3*z : s + z*(channel+1)*.1;
                        const unsigned value = unsigned(int(std::llround(mixed + channel*1234))) & 0xffffff;
                        frame[2+3*c] = char(value >> 16); frame[3+3*c] = char(value >> 8); frame[4+3*c] = char(value);
                    }
                    bytes += frame;
                }
            }
            data_->write(bytes);
        });
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
            while (server_.hasPendingConnections()) {
                auto* socket = server_.nextPendingConnection();
                auto request = std::make_shared<QByteArray>(); auto handled = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request, handled] {
                    if (*handled) return;
                    request->append(socket->readAll());
                    const int header = request->indexOf("\r\n\r\n"); if (header < 0) return;
                    const auto lines = request->left(header).split('\n'); const auto first = lines.first().trimmed().split(' ');
                    if (first.size() < 2) return;
                    int length = 0;
                    for (const auto& line : lines) if (line.trimmed().toLower().startsWith("content-length:"))
                        length = line.trimmed().mid(15).trimmed().toInt();
                    if (request->size() < header+4+length) return;
                    *handled = true;
                    const auto body = QJsonDocument::fromJson(request->mid(header+4, length)).object();
                    QByteArray payload = "{}";
                    if (first[1] == "/board") payload = R"({"board_connected":true,"board_type":"RT-BCI","num_channels":8,"device_id":"live-ica-test"})";
                    if (first[1] == "/tcp" && first[0] == "GET") payload = R"({"connected":false,"ip":"0.0.0.0"})";
                    if (first[1] == "/tcp" && first[0] == "POST") {
                        data_ = new QTcpSocket(&server_); data_->connectToHost(body.value("ip").toString(), quint16(body.value("port").toInt()));
                    }
                    if (first[1] == "/stream/start") { counter_ = 0; timer_.start(); }
                    if (first[1] == "/stream/stop") timer_.stop();
                    socket->write(QByteArray("HTTP/1.0 200 OK\r\nConnection: close\r\nContent-Length: ") +
                        QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    quint16 port() const { return server_.serverPort(); }
    void pause() { timer_.stop(); }
private:
    int channels_;
    QTcpServer server_;
    QTimer timer_;
    QPointer<QTcpSocket> data_;
    unsigned counter_ = 0;
    std::mt19937 random_{42};
    std::uniform_real_distribution<double> uniform_{-1, 1};
};

template<class Until, class Wait>
bool smokeWifiLiveIca(MainWindow& window, const Until& until, const Wait& wait) {
    auto* acquisition = window.findChild<AcquisitionController*>();
    auto* separation = window.findChild<EegSeparationController*>();
    auto* live = window.findChild<LiveIcaController*>();
    auto* navigation = window.findChild<QTabBar*>("eegWorkspaceNavigation");
    auto* snapshot = window.findChild<QPushButton*>("separationDeviceSnapshot");
    auto* toggle = window.findChild<QPushButton*>("separationLiveToggle");
    for (int channels : {8, 16}) {
        const auto failed = [&](const char* stage) {
            const auto view = acquisition->snapshot();
            std::cerr << "LIVE_ICA_WIFI FAIL channels=" << channels << " stage=" << stage
                      << " device_error=" << view.error.code.toStdString()
                      << " samples=" << view.samples
                      << " status=" << window.findChild<QLabel*>("separationStatus")->text().toStdString() << '\n';
            return false;
        };
        acquisition->disconnectDevice();
        if (!until([&] { return acquisition->snapshot().connection == DeviceConnectionState::Disconnected; })) return failed("disconnect_previous");
        LiveIcaWifiFixture fixture(channels);
        RtBciWifiOptions options; options.host = "127.0.0.1"; options.httpPort = fixture.port();
        options.localAddress = "127.0.0.1"; options.listenPort = 0; options.channels = channels; options.gain = 0;
        acquisition->connectWifi(options);
        if (!until([&] { return acquisition->snapshot().connection == DeviceConnectionState::Connected; })) return failed("connect");
        acquisition->startAcquisition();
        if (!until([&] { return acquisition->snapshot().samples > 5; })) return failed("first_samples");
        navigation->setCurrentIndex(1);
        if (!until([&] { return snapshot->isEnabled(); })) return failed("capture_available");
        snapshot->click();
        const auto previousPath = separation->source().path;
        if (!until([&] { return separation->state() == EegSeparationController::State::Ready && separation->source().path != previousPath; })) return failed("capture_ready");
        if (separation->source().channels.size() != channels || separation->source().metadata.value("unit") != "count") return failed("source_layout");
        for (int c = 0; c < channels; ++c)
            window.findChild<QComboBox*>(QString("separationRole%1").arg(c))->setCurrentIndex(c == 0 || c == channels-1 ? 1 : 4);
        window.findChild<QPushButton*>("separationMainAction")->click();
        if (!until([&] { return !separation->isBusy(); }, 60000) || !separation->output()) return failed("fit");
        const auto model = separation->output();
        window.findChild<QRadioButton*>("separationClassify0")->click();
        window.findChild<QCheckBox*>("separationConfirmed")->setChecked(true);
        if (!until([&] { return toggle->isEnabled(); })) return failed("live_available");
        int frames = 0;
        // Stack captures are disconnected before the fixture leaves scope.
        const auto connection = QObject::connect(live, &LiveIcaController::frameReady, &window, [&](auto frame) {
            if (frame->signal.channels.size() == 2 && frame->signal.channels.last() == model->channels.last()) ++frames;
        });
        toggle->click();
        const bool displayed = until([&] { return frames >= 3; });
        QObject::disconnect(connection);
        if (!displayed || separation->output() != model) return failed("live_frames");
        const auto description = window.findChild<QWidget*>("separationGroupTraces")->accessibleDescription();
        if (!description.contains("count") || description.contains(QString::fromUtf8("µV")) ||
            description.contains(QString::fromUtf8("μV"))) return failed("display_units");
        const auto screens = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
        if (!screens.isEmpty()) {
            window.findChild<QComboBox*>("separationPreviewChannel")->setCurrentIndex(1);
            wait(100);
            if (!window.grab().save(QDir(screens).filePath(QString("live-ica-rtbci-%1-count.png").arg(channels)))) return failed("screenshot");
        }
        auto* groupAxes = window.findChild<QWidget*>("separationGroupTraces");
        auto* componentAxes = window.findChild<QWidget*>("separationComponentTrace");
        groupAxes->grab(); componentAxes->grab();
        if (!groupAxes->property("axisYLabel").toString().contains("count") ||
            !componentAxes->property("axisYLabel").toString().contains("a.u.")) return failed("axis_units");
        // A silent socket must end live display instead of repeating old data.
        fixture.pause();
        if (!until([&] { return !live->isActive(); }, 6000)) return failed("stale_timeout");
        wait(100);
        acquisition->stopAcquisition();
        if (!until([&] { return !acquisition->hasActiveTasks(); })) return failed("stop");
        acquisition->disconnectDevice();
        if (!until([&] { return acquisition->snapshot().connection == DeviceConnectionState::Disconnected; })) return failed("disconnect");
        std::cout << "LIVE_ICA_WIFI PASS channels=" << channels << " unit=count reported=8 stale_stopped=true\n" << std::flush;
    }
    return true;
}
}

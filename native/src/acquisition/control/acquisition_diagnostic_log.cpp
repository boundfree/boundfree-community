#include "acquisition/control/acquisition_diagnostic_log.h"
#include "acquisition/control/acquisition_format.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace bf {
namespace {
constexpr qint64 detailLimit = 8 * 1024 * 1024;
constexpr qint64 summaryLimit = detailLimit + 64 * 1024;
QJsonValue redacted(const QJsonValue& value) {
    static const QRegularExpression sensitive("password|passwd|pwd|token|secret|credential|authorization|psk|cookie|api.?key", QRegularExpression::CaseInsensitiveOption);
    if (value.isObject()) {
        QJsonObject clean;
        const auto object = value.toObject();
        for (auto i = object.begin(); i != object.end(); ++i)
            clean.insert(i.key(), sensitive.match(i.key()).hasMatch() ? QJsonValue("[redacted]") : redacted(i.value()));
        return clean;
    }
    if (value.isArray()) {
        QJsonArray clean; for (const auto& entry : value.toArray()) clean.append(redacted(entry)); return clean;
    }
    if (value.isString()) {
        static const QRegularExpression assignment("(?:password|passwd|pwd|token|secret|psk|authorization)\\s*[:=]", QRegularExpression::CaseInsensitiveOption);
        if (assignment.match(value.toString()).hasMatch()) return "[redacted]";
    }
    return value;
}
double sample(const acquisition::SampleStorage& storage, std::size_t i) {
    if (const auto* p = storage.data<std::int16_t>()) return (*p)[i];
    if (const auto* p = storage.data<std::int32_t>()) return (*p)[i];
    if (const auto* p = storage.data<float>()) return (*p)[i];
    if (const auto* p = storage.data<double>()) return (*p)[i];
    return std::numeric_limits<double>::quiet_NaN();
}
}
AcquisitionDiagnosticLog::~AcquisitionDiagnosticLog() { close(); }
QString AcquisitionDiagnosticLog::directory() {
    QStringList roots;
    auto override = qEnvironmentVariable("BF_ACQUISITION_LOG_DIRECTORY");
    if (override.isEmpty()) override = qEnvironmentVariable("BF_RTBCI_LOG_DIRECTORY");
    if (!override.isEmpty()) roots = {override}; // Explicit test/developer override is exclusive.
    else roots = {QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QDir::tempPath()};
    for (const auto& root : roots) {
        if (root.isEmpty()) continue;
        QDir directory(root + "/BoundFree-Acquisition-Logs");
        if (directory.mkpath(".")) {
            QTemporaryFile probe(directory.filePath(".write-check-XXXXXX"));
            if (probe.open()) return directory.absolutePath();
        }
    }
    return {};
}
bool AcquisitionDiagnosticLog::open(const QString& adapter, std::optional<bool> simulated, const QJsonObject& details) {
    close(); path_.clear(); streams_.clear();
    adapter_ = adapter; simulated_ = simulated; warnings_ = dropped_ = 0; capped_ = false;
    session_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto root = directory();
    if (root.isEmpty()) return false;
    file_.setFileName(QDir(root).filePath("acquisition-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + "-" + session_ + ".jsonl"));
    if (!file_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    path_ = file_.fileName(); clock_.start(); write("connection_open", details); return true;
}
void AcquisitionDiagnosticLog::close() {
    if (!file_.isOpen()) return;
    flushStatistics(); write("connection_closed"); file_.close();
}
void AcquisitionDiagnosticLog::setSimulated(bool simulated) {
    if (simulated_ == simulated) return;
    simulated_ = simulated; write("source_identity");
}
void AcquisitionDiagnosticLog::write(const QString& kind, QJsonObject details) {
    if (!file_.isOpen()) return;
    if (kind == "wire_warning") ++warnings_;
    const bool detail = kind == "wire_warning" || kind == "signal_stats" || kind == "status";
    if (file_.size() >= summaryLimit || (detail && file_.size() >= detailLimit)) {
        ++dropped_;
        if (!capped_ && file_.size() < summaryLimit) { capped_ = true; write("log_limit", {{"detail_limit_bytes", double(detailLimit)}}); }
        return;
    }
    details = redacted(details).toObject();
    details.insert("schema", 1); details.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    details.insert("kind", kind); details.insert("adapter", adapter_); details.insert("simulated", simulated_ ? QJsonValue(*simulated_) : QJsonValue());
    details.insert("log_session_id", session_);
    const auto version = QCoreApplication::applicationVersion();
    details.insert("app_version", version.isEmpty() ? QStringLiteral("unknown") : version);
    details.insert("wire_warnings_total", QString::number(warnings_));
    details.insert("events_dropped_total", QString::number(dropped_));
    const auto bytes = QJsonDocument(details).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 64 * 1024) { ++dropped_; return; }
    if (file_.size() + bytes.size() > (detail ? detailLimit : summaryLimit)) {
        ++dropped_;
        if (detail && !capped_) { capped_ = true; write("log_limit", {{"detail_limit_bytes", double(detailLimit)}}); }
        return;
    }
    if (file_.write(bytes) != bytes.size() || !file_.flush()) file_.close(); // Logging never stops acquisition.
}
AcquisitionDiagnosticLog::Window& AcquisitionDiagnosticLog::window(const acquisition::StreamDescriptor& descriptor) {
    auto& w = streams_[descriptor.key.stream_id];
    if (!w.channels.empty()) return w;
    w.dtype = acquisition_io::dtypeName(descriptor.dtype); w.totalChannels = descriptor.channels.size(); w.simulated = descriptor.clock.simulated;
    w.channels.resize(std::min<std::size_t>(16, w.totalChannels));
    for (std::size_t ch = 0; ch < w.channels.size(); ++ch) {
        auto& c = w.channels[ch]; const auto& d = descriptor.channels[ch];
        c.name = QString::fromStdString(d.name); c.scale = d.scale; c.offset = d.offset;
        c.unit = d.unit ? acquisition_io::unitName(*d.unit) : QString();
        if (descriptor.dtype == acquisition::DType::Int16 || descriptor.dtype == acquisition::DType::Int24 || descriptor.dtype == acquisition::DType::Int32)
            c.railBits = d.valid_adc_bits.value_or(0);
    }
    return w;
}
void AcquisitionDiagnosticLog::observeValue(Channel& c, double value) {
    if (!std::isfinite(value)) { ++c.nonfinite; c.last = value; c.lastFinite = false; return; }
    if (!c.count) c.low = c.high = value;
    else if (c.lastFinite && c.last != value) ++c.changes;
    c.low = std::min(c.low, value); c.high = std::max(c.high, value); c.last = value;
    c.lastFinite = true;
    ++c.count;
    if (c.railBits >= 2 && c.railBits <= 32 && (value == -std::ldexp(1., c.railBits - 1) || value == std::ldexp(1., c.railBits - 1) - 1)) ++c.rails;
    const double delta = value - c.mean; c.mean += delta / c.count; c.m2 += delta * (value - c.mean);
}
void AcquisitionDiagnosticLog::observe(const acquisition::SampleBlock& block, const acquisition::StreamDescriptor& descriptor) {
    if (!file_.isOpen() || !block.storage) return;
    auto& w = window(descriptor); w.samples += block.sample_count;
    for (std::size_t ch = 0; ch < w.channels.size(); ++ch)
        for (std::uint32_t i = 0; i < block.sample_count; ++i) observeValue(w.channels[ch], sample(*block.storage, ch * block.sample_count + i));
    if (clock_.elapsed() >= 1000) flushStatistics();
}
void AcquisitionDiagnosticLog::observeInt24(const std::int32_t* values, const acquisition::StreamDescriptor& descriptor) {
    if (!file_.isOpen()) return;
    auto& w = window(descriptor); ++w.samples;
    for (std::size_t ch = 0; ch < w.channels.size(); ++ch) observeValue(w.channels[ch], values[ch]);
    if (clock_.elapsed() >= 1000) flushStatistics();
}
void AcquisitionDiagnosticLog::flushStatistics() {
    for (const auto& [id, w] : streams_) {
        if (!w.samples) continue;
        QJsonArray channels;
        for (std::size_t ch = 0; ch < w.channels.size(); ++ch) {
            const auto& c = w.channels[ch];
            const auto finite = [&](double n) { return c.count ? QJsonValue(n) : QJsonValue(); };
            channels.append(QJsonObject{{"channel", int(ch + 1)}, {"name", c.name}, {"physical_unit", c.unit}, {"scale", c.scale}, {"offset", c.offset},
                {"raw_min", finite(c.low)}, {"raw_max", finite(c.high)}, {"raw_last", c.lastFinite ? finite(c.last) : QJsonValue()}, {"peak_to_peak", finite(c.high - c.low)},
                {"mean", finite(c.mean)}, {"stddev", finite(c.count ? std::sqrt(std::max(0., c.m2 / c.count)) : 0)},
                {"changes", QString::number(c.changes)}, {"flat", c.count > 0 && !c.nonfinite && c.low == c.high}, {"nonfinite_samples", QString::number(c.nonfinite)},
                {"rail_samples", QString::number(c.rails)}, {"rail_bits", c.railBits}, {"rail_fraction", c.count && c.railBits >= 2 ? QJsonValue(double(c.rails) / c.count) : QJsonValue()}});
        }
        write("signal_stats", {{"unit", "raw_sample"}, {"dtype", w.dtype}, {"stream_id", double(id)}, {"samples", QString::number(w.samples)},
            {"total_channels", double(w.totalChannels)}, {"logged_channels", int(w.channels.size())}, {"stream_simulated", w.simulated}, {"channels", channels}});
    }
    streams_.clear(); clock_.restart();
}
} // namespace bf

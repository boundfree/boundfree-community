#pragma once

#include "acquisition/model/signal_model.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <map>

namespace bf {

// Owned by an acquisition worker, never the GUI. All adapters share the same
// schema, bounded writer, raw signal statistics and failure behavior.
class AcquisitionDiagnosticLog {
public:
    ~AcquisitionDiagnosticLog();
    bool open(const QString& adapter, std::optional<bool> simulated, const QJsonObject& details = {});
    void close();
    void write(const QString& kind, QJsonObject details = {});
    void setSimulated(bool simulated);
    QString path() const { return path_; } // Retained after stop/close.
    static QString directory();
    void observe(const acquisition::SampleBlock& block, const acquisition::StreamDescriptor& descriptor);
    void observeInt24(const std::int32_t* values, const acquisition::StreamDescriptor& descriptor);
    void flushStatistics();
private:
    struct Channel {
        double low = 0, high = 0, last = 0, mean = 0, m2 = 0;
        quint64 count = 0, nonfinite = 0, changes = 0, rails = 0;
        bool lastFinite = false;
        int railBits = 0;
        QString name, unit;
        double scale = 1, offset = 0;
    };
    struct Window {
        quint64 samples = 0;
        QString dtype;
        std::size_t totalChannels = 0;
        bool simulated = false;
        std::vector<Channel> channels;
    };
    Window& window(const acquisition::StreamDescriptor& descriptor);
    void observeValue(Channel& channel, double value);
    QFile file_;
    QString path_, adapter_, session_;
    std::optional<bool> simulated_;
    bool capped_ = false;
    quint64 warnings_ = 0, dropped_ = 0;
    QElapsedTimer clock_;
    std::map<quint32, Window> streams_;
};
} // namespace bf

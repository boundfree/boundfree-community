#include "acquisition/adapters/rt_bci_decoder.h"
#include "controller/acquisition_view.h"
#include "ui/plot_style.h"
#include "ui/waveform_widget.h"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace bf;
using namespace bf::acquisition;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while(false)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        std::vector<std::uint8_t> wire;
        if (argc > 2) {
            QFile file(QString::fromLocal8Bit(argv[2])); CHECK(file.open(QIODevice::ReadOnly));
            const auto bytes = file.readAll(); CHECK(bytes.size() <= 1024*1024 && bytes.size() % 33 == 0);
            wire.assign(bytes.begin(), bytes.end());
        } else {
            // The observed device emits distinct payloads with paired counters.
            // Exercise enough data to fill/evict history and downsample a ten
            // second window, rather than testing the decoder in isolation.
            for (int i = 0; i < 5000; ++i) {
                std::uint8_t frame[33]{}; frame[0] = 0xa0; frame[1] = (i / 2) % 256; frame[32] = 0xc0;
                for (int ch = 0; ch < 8; ++ch) {
                    const auto raw = static_cast<std::uint32_t>(static_cast<int>(5000000 + 1000000 * std::sin(i * .03 + ch))) & 0xffffff;
                    frame[2 + 3*ch] = raw >> 16; frame[3 + 3*ch] = raw >> 8; frame[4 + 3*ch] = raw;
                }
                wire.insert(wire.end(), frame, frame + 33);
            }
        }
        StreamDescriptor d; d.key = {SessionId{"session"}, 1}; d.capture_id = CaptureId{"capture"}; d.generation = 1;
        d.rate = {250, 1}; d.dtype = DType::Int24; d.max_samples_per_block = 64; d.signal_type = "biopotential";
        d.sample_index_source = SampleIndexSource::AdapterReceived;
        d.clock = {1, 250, 1, ClockSource::Estimated, ClockEpoch::SessionRelative, false};
        for (int ch = 0; ch < 8; ++ch) {
            ChannelDescriptor c; c.channel_id = std::to_string(ch + 1); c.name = "CH" + c.channel_id;
            c.unit = Unit::Count; c.valid_adc_bits = 24; c.signal_type = d.signal_type; d.channels.push_back(c);
        }
        CaptureDescriptor capture{d.key.session_id, d.capture_id, 1, {d}};
        MemoryBudget budget(64 * 1024 * 1024); SignalHub hub(capture, budget);
        auto history = hub.addConsumer(ConsumerKind::History, {32 * 1024 * 1024, 1500}); CHECK(history);
        rt_bci::Decoder decoder(8); std::vector<rt_bci::Sample> pending;
        std::size_t warnings = 0;
        const auto flush = [&] {
            if (pending.empty()) return;
            std::vector<std::int32_t> values;
            for (int ch = 0; ch < 8; ++ch) for (const auto& s : pending) values.push_back(s.values[ch]);
            SampleBlock b; b.stream = d.key; b.capture_id = d.capture_id; b.generation = 1; b.descriptor_revision = d.revision;
            b.channel_count = 8; b.sample_count = static_cast<std::uint32_t>(pending.size()); b.first_sample_index = pending.front().index;
            b.clock_id = 1; b.first_timestamp_ticks = b.first_sample_index; b.timestamp_estimated = true;
            b.storage = SampleStorage::fromInt32(DType::Int24, values, budget); CHECK(b.storage);
            CHECK(hub.publish(b).status == PublishStatus::Accepted); pending.clear();
        };
        for (std::size_t pos = 0; pos < wire.size();) {
            const auto size = std::min<std::size_t>(239, wire.size() - pos);
            const auto batch = decoder.feed(wire.data() + pos, size); pos += size; CHECK(!batch.error);
            for (const auto& note : batch.diagnostics) { CHECK(note.reason == "duplicate_counter"); ++warnings; }
            for (const auto& s : batch.samples) {
                if (s.gap_before) { flush(); CHECK(!hub.reportUnknownGap(d.key, "rt_bci.wire_discontinuity")); }
                pending.push_back(s); if (pending.size() == 5) flush();
            }
        }
        flush(); CHECK(!decoder.finish().error);
        const auto traces = buildAcquisitionTraces(capture, *history.value);
        const auto slice = acquisitionWaveformSlice(traces); CHECK(slice.data.size() == 8);
        WaveformWidget widget; widget.resize(1000, 900); widget.setAxisLabels("Time (s)", "Amplitude (count)");
        widget.setSlice(slice);
        if (argc > 3) {
            bool valid = false; const double seconds = QString::fromLocal8Bit(argv[3]).toDouble(&valid);
            CHECK(valid && seconds > 0 && seconds <= 10);
            widget.setTimeRange(std::max(slice.t0, slice.t1 - seconds), slice.t1);
        }
        widget.show(); app.processEvents();
        QImage image(widget.size(), QImage::Format_ARGB32); widget.render(&image);
        if (argc > 1) CHECK(image.save(QString::fromLocal8Bit(argv[1])));
        std::size_t segments = 0;
        for (std::size_t i = 1; i < slice.data[0].size(); ++i)
            segments += std::isfinite(slice.data[0][i-1]) && std::isfinite(slice.data[0][i]);
        int pixels = 0, top = image.height(), bottom = -1;
        const auto color = plot::eegChannelColor(0, false).rgba();
        for (int y = 0; y < image.height(); ++y) for (int x = 175; x < image.width() - 20; ++x)
            if (image.pixel(x, y) == color) { ++pixels; top = std::min(top, y); bottom = std::max(bottom, y); }
        std::cout << "acquisition-waveform samples=" << hub.stats().accepted_samples << " warnings=" << warnings
                  << " unknown_gaps=" << hub.stats().unknown_gaps << " drawable_segments=" << segments << " color_pixels=" << pixels << std::endl;
        CHECK(hub.stats().accepted_samples == wire.size() / 33);
        CHECK(warnings + 2 >= wire.size() / 66 && warnings <= wire.size() / 66 + 2);
        CHECK(hub.stats().unknown_gaps == 0 && segments > 100 && pixels > 200 && bottom - top > 40);
        std::cout << "ACQUISITION_WAVEFORM_TEST PASS duplicate_counter_logged valid_samples_visible no_false_gaps\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "ACQUISITION_WAVEFORM_TEST FAIL " << e.what() << '\n'; return 1; }
}

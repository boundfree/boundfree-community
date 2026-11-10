#include "controller/live_ica_projection.h"
#include "dsp/ica_core.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

using namespace bf;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int checks = 0;
    const auto check = [&](bool good) { ++checks; if (!good) throw std::runtime_error("live ICA assertion " + std::to_string(checks)); };
    try {
        // Sixteen physical identities in count units; the observation order is
        // deliberately different from the wire order, with a nonzero mean.
        WaveformSlice raw; raw.fs = 250; raw.unit = "count"; raw.sourceId = "capture/1"; raw.streamId = "session/1";
        raw.t0 = 40; raw.t1 = 41;
        for (int c = 0; c < 16; ++c) {
            raw.channelIds << QString("adc-%1").arg(c); raw.channelNames << QString("CH %1").arg(c + 1); raw.channelUnits << "count";
            raw.data.push_back(std::vector<float>(250)); raw.sampleTimes.push_back(std::vector<double>(250));
            for (int t = 0; t < 250; ++t) { raw.data[c][t] = c * 100 + t; raw.sampleTimes[c][t] = 40 + t / 250.; }
        }
        SeparationOutput model; model.fs = 250; model.rank = 2; model.channels = {"CH 16", "CH 1"};
        model.mean = {12, -3}; model.mixing = {{2, 1}, {1, 1}};
        model.manifest = {{"source_rate", 250}, {"notch_hz", 0},
            {"source", QJsonObject{{"unit", "count"}, {"channel_ids", QJsonArray{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"}}, {"signal_provenance", QJsonObject{
                {"source_id", raw.sourceId}, {"stream_id", raw.streamId},
                {"channel_ids", QJsonArray::fromStringList(raw.channelIds)}, {"channel_units", QJsonArray::fromStringList(raw.channelUnits)}}}}},
            {"observation_channel_ids", QJsonArray{"15", "0"}}, {"unmixing", QJsonArray{QJsonArray{1, -1}, QJsonArray{-1, 2}}}};
        const auto originalModel = model.manifest;
        auto frame = projectLiveIca(model, {0, 1}, raw);
        check(!frame.hasGaps && frame.signal.analysis.size() == 2 && frame.signal.analysis[0].size() == 250);
        for (int t = 0; t < 250; ++t) {
            const double a = raw.data[15][t] - 12, b = raw.data[0][t] + 3;
            check(frame.signal.sources[0][t] == a - b && frame.signal.sources[1][t] == -a + 2*b);
            for (int c = 0; c < 2; ++c) {
                double sum = model.mean[c] + frame.signal.residual[c][t];
                for (const auto& group : frame.groups) sum += group[c][t];
                check(std::abs(sum - frame.signal.analysis[c][t]) < 1e-10);
            }
        }
        const auto relabeled = projectLiveIca(model, {2, 3}, raw);
        check(relabeled.signal.sources == frame.signal.sources && model.manifest == originalModel);
        check(relabeled.groups[0][0][20] == 0 && relabeled.groups[2][0][20] == frame.groups[0][0][20]);
        const auto rejects = [&](const SeparationOutput& m, const WaveformSlice& w, std::vector<int> labels = {0, 1}) {
            try { projectLiveIca(m, labels, w); check(false); }
            catch (const std::invalid_argument&) { check(true); }
        };
        auto changed = raw; changed.sourceId = "capture/2"; rejects(model, changed);
        changed = raw; changed.streamId = "session/2"; rejects(model, changed);
        changed = raw; changed.channelIds.swapItemsAt(0, 15); rejects(model, changed);
        changed = raw; changed.channelUnits[15] = "uV"; rejects(model, changed);
        changed = raw; changed.fs = 500; rejects(model, changed);
        changed = raw; changed.displayOnly = true; rejects(model, changed);
        auto badModel = model; badModel.manifest["notch_hz"] = 50; rejects(badModel, raw);
        badModel = model; badModel.manifest["source_rate"] = 500; rejects(badModel, raw);
        badModel = model; badModel.manifest["unmixing"] = QJsonArray{QJsonArray{"bad", 0}, QJsonArray{0, 1}}; rejects(badModel, raw);
        rejects(model, raw, {0}); rejects(model, raw, {0, 4});
        changed = raw; changed.data[15][10] = std::numeric_limits<float>::quiet_NaN();
        frame = projectLiveIca(model, {0, 1}, changed);
        check(frame.hasGaps && std::isnan(frame.signal.sources[0][10]) && std::isfinite(frame.signal.sources[0][11]));
        changed = raw;
        for (size_t c = 0; c < changed.data.size(); ++c) { changed.data[c].erase(changed.data[c].begin()+10); changed.sampleTimes[c].erase(changed.sampleTimes[c].begin()+10); }
        frame = projectLiveIca(model, {0, 1}, changed);
        check(frame.hasGaps && std::isnan(frame.groups[0][0][10]) && frame.signal.analysis[0][11] == raw.data[15][11]);
        changed = raw; for (auto& channel : changed.data) std::fill(channel.begin(), channel.end(), std::numeric_limits<float>::quiet_NaN());
        check(projectLiveIca(model, {0, 1}, changed).hasGaps);
        // Exercise a genuinely fitted model on held-out samples, rather than
        // only reproducing the projection implementation with identity weights.
        separation::Matrix train(2, std::vector<double>(2500));
        std::mt19937 random(42); std::uniform_real_distribution<double> uniform(-1, 1);
        for (int t = 0; t < 2500; ++t) {
            const double s = std::sin(t * .14) * 40, z = std::pow(uniform(random), 3) * 90;
            train[0][t] = 2*s + z + 23; train[1][t] = s - 3*z - 12;
        }
        separation::ica::Options options; options.sampleRate = 250;
        const auto fit = separation::ica::fit(train, options);
        model.channels = {"CH 1", "CH 16"}; model.mean = fit.mean; model.mixing = fit.mixing; model.rank = fit.rank;
        QJsonArray weights; for (const auto& row : fit.unmixing) { QJsonArray values; for (double v : row) values.append(v); weights.append(values); }
        model.manifest["unmixing"] = weights; model.manifest["observation_channel_ids"] = QJsonArray{"0", "15"};
        std::vector<double> expectedS, expectedZ;
        for (int t = 0; t < 250; ++t) {
            const double s = std::sin((t+2500)*.14)*40, z = std::pow(uniform(random), 3)*90;
            raw.data[0][t] = 2*s + z + 23; raw.data[15][t] = s - 3*z - 12; expectedS.push_back(s); expectedZ.push_back(z);
        }
        frame = projectLiveIca(model, {0, 1}, raw);
        const double a = std::abs(separation::ica::correlation(frame.signal.sources[0], expectedS));
        const double b = std::abs(separation::ica::correlation(frame.signal.sources[1], expectedS));
        check(std::max(a, b) > .95);
        check(std::abs(separation::ica::correlation(frame.signal.sources[a>b?1:0], expectedZ)) > .95);
        std::cout << "LIVE_ICA_PROJECTION PASS checks=" << checks << " held_out_sources=true channels16=true gaps=true\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

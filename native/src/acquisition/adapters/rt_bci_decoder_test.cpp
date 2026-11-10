#include "acquisition/adapters/rt_bci_decoder.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace bf::acquisition::rt_bci;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (false)
namespace {
using Bytes = std::vector<std::uint8_t>;
int value(int sample, int channel, int chip) {
    return (chip ? -1 : 1) * (sample * 997 + channel * 40009 + chip * 7000000) % 8000000;
}
Bytes frame(unsigned counter, int sample, int chip) {
    Bytes b(33, 0); b[0] = 0xa0; b[1] = static_cast<std::uint8_t>(counter); b[32] = 0xc0;
    for (int k = 0; k < 8; ++k) {
        const auto raw = static_cast<unsigned>(value(sample, k, chip)) & 0xffffff;
        b[2+3*k] = raw >> 16; b[3+3*k] = raw >> 8; b[4+3*k] = raw;
    }
    return b;
}
void append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
Bytes firmwareStream(int channels, int samples) {
    Bytes bytes;
    for (int n = 0; n < samples; ++n) {
        const auto count = static_cast<std::uint8_t>(channels == 8 ? n + 1 : n);
        append(bytes, frame(count, n, 0));
        if (channels == 16) append(bytes, frame(count, n, 1));
    }
    return bytes;
}
struct Output {
    std::vector<Sample> samples;
    std::size_t discarded = 0;
    std::vector<Batch::Diagnostic> diagnostics;
};
void collect(Output& out, const Batch& batch) {
    CHECK(!batch.error);
    out.samples.insert(out.samples.end(), batch.samples.begin(), batch.samples.end());
    out.diagnostics.insert(out.diagnostics.end(), batch.diagnostics.begin(), batch.diagnostics.end());
    out.discarded += batch.discarded_bytes;
}
Output run(Decoder& decoder, const Bytes& bytes, unsigned seed = 5) {
    Output out; std::mt19937 random(seed);
    for (std::size_t pos = 0; pos < bytes.size();) {
        const auto size = std::min<std::size_t>(1 + random() % 1460, bytes.size() - pos);
        collect(out, decoder.feed(bytes.data() + pos, size)); pos += size;
    }
    collect(out, decoder.finish());
    return out;
}
void checkValues(const Output& out, int channels, bool complete, int first = 0) {
    int previous = first - 1;
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        const auto& sample = out.samples[i];
        const int n = sample.values[0] / 997;
        CHECK(sample.index == i && n >= first && n < 700);
        CHECK(n > previous);
        previous = n;
        if (complete) CHECK(n == first + static_cast<int>(i));
        for (int ch = 0; ch < channels; ++ch) CHECK(sample.values[ch] == value(n, ch % 8, ch / 8));
    }
}
int gaps(const Output& out) {
    return static_cast<int>(std::count_if(out.samples.begin(), out.samples.end(), [](const auto& s) { return s.gap_before; }));
}
// Independent aligned-packet model. Misaligned data deliberately no longer
// follows the BrainFlow receiver, which never searches for a new frame header.
std::vector<Sample> reference(const Bytes& bytes, unsigned channels) {
    std::vector<Sample> out; Sample cached{}; int previous = -1;
    for (std::size_t pos = 0; pos + 33 <= bytes.size(); pos += 33) {
        const auto id = bytes[pos+1];
        const bool first = channels == 8 || id != previous;
        for (int ch = 0; ch < 8; ++ch) {
            const auto p = pos + 2 + 3 * ch;
            int n = int(bytes[p])*65536 + int(bytes[p+1])*256 + bytes[p+2];
            if (n >= 8388608) n -= 16777216;
            cached.values[(first ? 0 : 8) + ch] = n;
        }
        if (channels == 8 || !first) { cached.index = out.size(); cached.counter = id; out.push_back(cached); }
        previous = id;
    }
    return out;
}
void referenceEquivalence() {
    std::mt19937 random(896239);
    for (unsigned channels : {8U, 16U}) {
        Bytes bytes;
        for (unsigned i = 0; i < 800; ++i) {
            const auto counter = random() % 256;
            append(bytes, frame(counter, i % 700, 0));
            if (channels == 16) {
                append(bytes, frame(counter, i % 700, 1));
                if (i % 17 == 0) append(bytes, frame(counter, i % 700, 1));
            }
        }
        Decoder decoder(channels); const auto got = run(decoder, bytes); const auto expected = reference(bytes, channels);
        CHECK(got.samples.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(got.samples[i].index == expected[i].index && got.samples[i].counter == expected[i].counter);
            for (unsigned ch = 0; ch < channels; ++ch) CHECK(got.samples[i].values[ch] == expected[i].values[ch]);
        }
    }
}
void normalAndAuto() {
    for (int channels : {8,16}) {
        const auto bytes = firmwareStream(channels, 700);
        for (bool automatic : {false, true}) {
            Decoder decoder = automatic ? Decoder(Layout::Auto) : Decoder(static_cast<unsigned>(channels));
            const auto out = run(decoder, bytes);
            CHECK(decoder.detectedChannels() == static_cast<unsigned>(channels));
            CHECK(out.samples.size() == 700 && gaps(out) == 0 && out.discarded == 0 && out.diagnostics.empty());
            checkValues(out, channels, true);
            if (automatic) {
                const auto evidence = decoder.detectionEvidence();
                CHECK(evidence.frames == 24 && (channels == 8 ? evidence.pairs == 0 : evidence.pairs >= 10));
            }
        }
    }
}
void startMidPair() {
    auto bytes = firmwareStream(16, 700); bytes.erase(bytes.begin(), bytes.begin() + 33);
    for (bool automatic : {false, true}) {
        Decoder decoder = automatic ? Decoder(Layout::Auto) : Decoder(16);
        const auto out = run(decoder, bytes); CHECK(out.samples.size() == 699);
        checkValues(out, 16, true, 1);
    }
}
void detectionAfterLoss() {
    for (int channels : {8,16}) {
        auto bytes = firmwareStream(channels, 700);
        const std::size_t lost = (channels == 8 ? 5 : 10) * 33;
        bytes.erase(bytes.begin() + lost, bytes.begin() + lost + 33);
        Decoder decoder(Layout::Auto);
        const auto first = decoder.feed(bytes.data(), 24 * 33);
        CHECK(first.samples.empty() && !decoder.detectedChannels());
        Output out; collect(out, first);
        for (std::size_t pos = 24 * 33; pos < bytes.size();) {
            const auto count = std::min<std::size_t>(1024, bytes.size() - pos);
            collect(out, decoder.feed(bytes.data() + pos, count)); pos += count;
        }
        collect(out, decoder.finish());
        CHECK(decoder.detectedChannels() == static_cast<unsigned>(channels));
        CHECK(decoder.detectionEvidence().frames > 24 && out.samples.size() == 699 && gaps(out) >= 1);
        checkValues(out, channels, false);
    }
}
void recover() {
    for (int channels : {8,16}) for (int scenario = 0; scenario < 4; ++scenario) {
        auto bytes = firmwareStream(channels, 700);
        const std::size_t pos = (channels == 8 ? 200 : 400) * 33 + 9;
        if (scenario == 0) bytes.erase(bytes.begin() + pos);
        if (scenario == 1) bytes.insert(bytes.begin() + pos, 0x77);
        if (scenario == 2) bytes.erase(bytes.begin() + pos, bytes.begin() + pos + 70);
        if (scenario == 3) {
            const std::size_t first = (channels == 8 ? 100 : 200) * 33;
            bytes.erase(bytes.begin() + first, bytes.begin() + first + (channels == 8 ? 3 : 1) * 33);
        }
        Decoder decoder(Layout::Auto); const auto out = run(decoder, bytes);
        CHECK(decoder.detectedChannels() == static_cast<unsigned>(channels));
        CHECK(out.samples.size() > 690 && gaps(out) >= 1);
        CHECK(out.samples.back().values[0] == value(699, 0, 0));
        checkValues(out, channels, false);
        if (scenario != 3) {
            CHECK(out.discarded > 0);
            CHECK(std::any_of(out.diagnostics.begin(), out.diagnostics.end(), [](const auto& d) { return d.reason == "frame_resync"; }));
        }
    }
}
void falseHeaders() {
    Bytes prefix(130, 0);
    prefix[4] = 0xa0; prefix[36] = 0xc0;
    prefix[37] = 0xa0; prefix[69] = 0xc6; // Two apparent frames, but no third.
    prefix[80] = 0x0d; prefix[81] = 0x0a;
    append(prefix, firmwareStream(8, 700));
    Decoder decoder(Layout::Auto); const auto out = run(decoder, prefix);
    CHECK(out.samples.size() == 700 && out.discarded == 130 && gaps(out) == 1);
    checkValues(out, 8, true);

    auto wire = firmwareStream(8, 700);
    const std::size_t pos = 17 * 33 + 2;
    wire[pos] = 0xa0; wire[pos+1] = 0xc0; wire[pos+2] = 0x0d;
    wire[pos+3] = 0x0a; wire[pos+4] = 0xa0; wire[pos+5] = 0xc0;
    Decoder embedded(Layout::Auto); const auto special = run(embedded, wire);
    CHECK(special.samples.size() == 700 && special.discarded == 0 && gaps(special) == 0);
    CHECK(special.samples[17].values[0] == 0xa0c00d - 0x1000000);
    CHECK(special.samples[17].values[1] == 0x0aa0c0);
    for (std::size_t i = 0; i < special.samples.size(); ++i)
        if (i != 17) for (int ch = 0; ch < 8; ++ch)
            CHECK(special.samples[i].values[ch] == value(static_cast<int>(i), ch, 0));
}
void undetermined() {
    Bytes noise(10000); std::mt19937 random(91);
    for (auto& byte : noise) byte = static_cast<std::uint8_t>(random());
    for (const auto& bytes : {noise, Bytes(10000, 0), [&] { Bytes b; for (int i = 0; i < 210; ++i) append(b, frame(3, 0, 0)); return b; }()}) {
        Decoder decoder(Layout::Auto); const auto out = run(decoder, bytes);
        CHECK(decoder.state() == DecodeState::Undetermined && !decoder.detectedChannels() && out.samples.empty());
    }
}
void forcedAndLimits() {
    Decoder wrong8(8); const auto a = run(wrong8, firmwareStream(16, 300)); CHECK(a.samples.size() == 600);
    for (std::size_t i = 0; i < a.samples.size(); ++i)
        for (int ch = 0; ch < 8; ++ch)
            CHECK(a.samples[i].values[ch] == value(static_cast<int>(i / 2), ch, static_cast<int>(i % 2)));
    CHECK(a.diagnostics.size() == 300);
    Decoder wrong16(16); const auto b = run(wrong16, firmwareStream(8, 300));
    CHECK(b.samples.empty() && wrong16.discardedGroups() == 300);
    Decoder fallback(Layout::Auto); Bytes repeated;
    for (int i = 0; i < 200; ++i) append(repeated, frame(3, 0, 0));
    const auto initial = fallback.feed(repeated.data(), repeated.size());
    CHECK(initial.samples.empty() && fallback.state() == DecodeState::Undetermined);
    const auto resumed = fallback.useFallback(8); CHECK(resumed.samples.size() == 200);
    CHECK(fallback.detectedChannels() == 8 && fallback.detectionEvidence().frames == 200);
    bool threw=false; try { Decoder invalid(10); } catch (const std::invalid_argument&) { threw=true; } CHECK(threw);
    Decoder decoder(8); CHECK(!decoder.feed(nullptr,0).error); CHECK(decoder.feed(nullptr,1).error); decoder.reset();
    Bytes large(65537); CHECK(decoder.feed(large.data(),large.size()).error); decoder.reset();
    const auto shortStream = firmwareStream(8, 2);
    CHECK(decoder.feed(shortStream.data(), shortStream.size()).samples.empty());
    CHECK(decoder.finish().samples.size() == 2); CHECK(decoder.feed(nullptr,0).error);
}
}
int main() { try {
    referenceEquivalence(); normalAndAuto(); startMidPair(); detectionAfterLoss(); recover(); falseHeaders(); undetermined(); forcedAndLimits();
    std::cout << "rt_bci_decoder_test: PASS aligned_reference firmware_auto fragmentation_wrap resync_loss noise fallback\n"; return 0;
} catch (const std::exception& e) { std::cerr << "rt_bci_decoder_test: FAIL " << e.what() << '\n'; return 1; } }

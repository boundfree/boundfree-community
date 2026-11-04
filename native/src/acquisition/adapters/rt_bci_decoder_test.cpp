#include "acquisition/adapters/rt_bci_decoder.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace bf::acquisition::rt_bci;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (false)
namespace {
using Bytes = std::vector<std::uint8_t>;
Bytes frame(unsigned counter, int base = 0, unsigned end = 0xc0) {
    Bytes b(33); b[0] = 0xa0; b[1] = static_cast<std::uint8_t>(counter); b[32] = static_cast<std::uint8_t>(end);
    for (int i = 0; i < 8; ++i) {
        const auto raw = static_cast<unsigned>(base + i) & 0xffffff;
        b[2+i*3] = raw >> 16; b[3+i*3] = raw >> 8; b[4+i*3] = raw;
    }
    b[26] = 0xa0; b[27] = 0xc6; b[28] = 0x0d; b[29] = 0x0a;
    return b;
}
void append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
// Independent reference behavior model. Fixed packet boundaries, no byte scan,
// counter has no role in 8ch admission, Daisy commits on same-ID bank two.
std::vector<Sample> reference(const Bytes& b, unsigned channels) {
    std::vector<Sample> out; Sample cached{}; int previous = -1;
    for (std::size_t pos = 0; pos + 33 <= b.size(); pos += 33) {
        if (b[pos] != 160 || b[pos+32] < 192 || b[pos+32] > 198) continue;
        const int id = b[pos+1];
        const bool bankOne = channels == 8 || id != previous;
        for (int c = 0; c < 8; ++c) {
            const auto start = pos + 2 + c * 3;
            int v = int(b[start])*65536 + int(b[start+1])*256 + b[start+2];
            if (v >= 8388608) v -= 16777216;
            cached.values[(bankOne ? 0 : 8) + c] = v;
        }
        if (channels == 8 || !bankOne) { cached.index = out.size(); cached.counter = id; out.push_back(cached); }
        previous = id;
    }
    return out;
}
void compare(const Bytes& bytes, unsigned channels, std::size_t chunk) {
    Decoder decoder(channels); std::vector<Sample> got;
    for (std::size_t pos = 0; pos < bytes.size();) {
        const auto count = std::min(chunk, bytes.size()-pos);
        const auto batch = decoder.feed(bytes.data()+pos, count); CHECK(!batch.error);
        got.insert(got.end(), batch.samples.begin(), batch.samples.end()); pos += count;
    }
    CHECK(!decoder.finish().error);
    const auto expected = reference(bytes, channels); CHECK(got.size() == expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        CHECK(got[i].index == expected[i].index && got[i].counter == expected[i].counter);
        for (unsigned c = 0; c < channels; ++c) CHECK(got[i].values[c] == expected[i].values[c]);
    }
}
void referenceEquivalence() {
    std::mt19937 random(896239);
    for (unsigned channels : {8U,16U}) {
        Bytes bytes;
        for (unsigned i = 0; i < 800; ++i) {
            const auto counter = random() % 256;
            auto b = frame(counter, int(random()%16777209)-8388608, 0xc0+random()%7);
            if (i % 11 == 0) b[0] = 0;
            if (i % 13 == 0) b[32] = 0xcf;
            append(bytes,b);
            if (channels == 16) {
                append(bytes,frame(counter,1000+i));
                if (i%17 == 0) append(bytes,frame(counter,5000+i));
            }
        }
        append(bytes,Bytes{0xa0,0xc0,0x0d});
        for (std::size_t chunk : {1U,2U,32U,33U,34U,65U,257U,65536U}) compare(bytes,channels,chunk);
        // Delimiters are not stripped by the reference. Ensure our behavior
        // matches it even when payload and transport text contain CR/LF/A0.
        Bytes delimited; for(unsigned i=0;i<100;++i){append(delimited,frame(i));append(delimited,Bytes{13,10});}
        compare(delimited,channels,7);
    }
}
void anomaliesContinue() {
    Decoder decoder(8); Bytes bytes;
    append(bytes,frame(250,-8388608)); append(bytes,frame(250,8388600,0xc6));
    auto bad=frame(1);bad[32]=0xcf;append(bytes,bad);
    append(bytes,frame(1,0x0d0a));append(bytes,frame(200,-1));
    auto b=decoder.feed(bytes.data(),bytes.size()); CHECK(!b.error && b.samples.size()==4);
    CHECK(b.diagnostics.size()==4 && b.discarded_bytes==33);
    CHECK(b.samples[0].values[0]==-8388608 && b.samples[1].values[7]==8388607);
    CHECK(!b.samples[1].gap_before && b.samples[2].gap_before && b.samples[3].gap_before);
    CHECK(b.diagnostics[0].reason=="duplicate_counter" && b.diagnostics[0].bytes==frame(250,8388600,0xc6));
    auto next=frame(201);CHECK(decoder.feed(next.data(),next.size()).samples.size()==1);
    CHECK(!decoder.finish().error);
    decoder.reset(); CHECK(!decoder.firstCounter());
    next=frame(0);CHECK(decoder.feed(next.data(),3).samples.empty());
    auto tail=decoder.finish(); CHECK(!tail.error && tail.discarded_bytes==3);
    CHECK(tail.diagnostics[0].reason=="truncated_frame"); CHECK(!decoder.finish().error);
}
void daisyImmediatePairs() {
    Decoder decoder(16); auto a=frame(0,100),b=frame(0,200),c=frame(0,300),d=frame(3,400);
    CHECK(decoder.feed(a.data(),a.size()).samples.empty());
    auto pair=decoder.feed(b.data(),b.size()); CHECK(pair.samples.size()==1);
    CHECK(pair.samples[0].values[0]==100 && pair.samples[0].values[8]==200);
    pair=decoder.feed(c.data(),c.size()); CHECK(pair.samples.size()==1 && pair.samples[0].values[8]==300);
    CHECK(pair.diagnostics.size()==1 && pair.diagnostics[0].reason=="repeated_second_bank" && pair.samples[0].gap_before);
    CHECK(decoder.feed(d.data(),d.size()).samples.empty());
    auto tail=decoder.finish(); CHECK(!tail.error && decoder.discardedGroups()==1);
    CHECK(tail.diagnostics.back().reason=="unpaired_tail");
}
void duplicateAfterDiscardStillBreaks() {
    Decoder decoder(8); Bytes bytes;
    append(bytes, frame(42, 100));
    auto bad = frame(43); bad[32] = 0xcf; append(bytes, bad);
    append(bytes, frame(42, 200)); // Same ID does not erase a prior framing loss.
    append(bytes, frame(42, 300)); // This valid repeat adds no further gap.
    append(bytes, frame(45, 400)); // A real wire counter jump is still marked.
    const auto batch = decoder.feed(bytes.data(), bytes.size());
    CHECK(!batch.error && batch.samples.size() == 4 && batch.discarded_bytes == 33);
    CHECK(!batch.samples[0].gap_before && batch.samples[1].gap_before &&
          !batch.samples[2].gap_before && batch.samples[3].gap_before);
    CHECK(batch.samples[1].values[0] == 200 && batch.samples[2].values[0] == 300);
}
void limits() {
    bool threw=false;try{Decoder invalid(10);}catch(const std::invalid_argument&){threw=true;}CHECK(threw);
    Decoder decoder(8); CHECK(!decoder.feed(nullptr,0).error);
    CHECK(decoder.feed(nullptr,1).error);decoder.reset();
    Bytes large(65537);CHECK(decoder.feed(large.data(),large.size()).error);decoder.reset();
    CHECK(!decoder.finish().error);CHECK(decoder.feed(nullptr,0).error);
}
}
int main(){try{referenceEquivalence();anomaliesContinue();daisyImmediatePairs();duplicateAfterDiscardStillBreaks();limits();
    std::cout<<"rt_bci_decoder_test: PASS reference_equivalence fragmentation malformed_packet_skip footer_C0_C6 counter_tolerance daisy_immediate_pair\n";return 0;
}catch(const std::exception& e){std::cerr<<"rt_bci_decoder_test: FAIL "<<e.what()<<'\n';return 1;}}

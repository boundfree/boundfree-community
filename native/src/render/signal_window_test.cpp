#include "render/signal_processor.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace bf;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
WaveformSlice fixture(int samples){WaveformSlice s;s.fs=250;s.t0=0;s.t1=samples/250.;s.unit="count";s.channelUnits={"count","count"};s.channelNames={"A","B"};s.channelIds={"a","b"};s.sourceId="test";s.streamId="1";s.data.resize(2);s.sampleTimes.resize(2);for(int c=0;c<2;++c)for(int i=0;i<samples;++i){s.data[c].push_back(100*std::sin(i*.1+c));s.sampleTimes[c].push_back(i/250.);}return s;}
void same(const WaveformSlice& a,const WaveformSlice& b){check(a.data.size()==b.data.size(),"channel shape");for(std::size_t c=0;c<a.data.size();++c){check(a.data[c].size()==b.data[c].size(),"sample shape");for(std::size_t i=0;i<a.data[c].size();++i)check(std::abs(a.data[c][i]-b.data[c][i])<1e-4 || (std::isnan(a.data[c][i]) && std::isnan(b.data[c][i])),"sample value");}}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    auto raw=fixture(500);ProcessingViewState settings;settings.notchEnabled=true;settings.globalBandpassEnabled=true;
    auto gapped=raw;for(auto& ch:gapped.data)ch[100]=std::numeric_limits<float>::quiet_NaN();
    for(auto& axis:gapped.sampleTimes)for(std::size_t i=250;i<axis.size();++i)axis[i]+=.4;
    const auto spans=signalContinuousRanges(gapped,{0,1});check(spans.size()==3,"gap segmentation");
    const auto filtered=applyProcessingToSlice(gapped,settings);
    check(std::isnan(filtered.data[0][100]) && std::isfinite(filtered.data[0][200]) && std::isfinite(filtered.data[0][450]),"gap poisons filter");
    const auto selection=signalWindowRange(gapped,1.4,1.44);check(selection.data[0].size()==10,"sparse coordinate selection");
    SignalProcessor chunks,full;const auto a=signalWindowRange(raw,0,.4);chunks.process(a,settings,true);
    same(chunks.process(raw,settings,true),full.process(raw,settings,true));
    const auto cropped=signalWindowRange(raw,.2,2);same(chunks.process(cropped,settings,true),signalWindowRange(full.process(raw,settings,true),.2,2));
    raw.sourceId="new-capture";SignalProcessor fresh;same(chunks.process(raw,settings,true),fresh.process(raw,settings,true));
    auto bipolar=settings;bipolar.referenceMode=dsp::ReferenceMode::Bipolar;SignalProcessor pairs;
    const auto pair=pairs.process(raw,bipolar,true);check(pair.data.size()==1 && pair.channelIds.size()==1 && pair.channelIds.front()=="a-b" && pair.channelUnits.size()==1,"bipolar channel metadata");
    auto mixed=raw;mixed.channelUnits[1]="degC";settings.referenceMode=bf::dsp::ReferenceMode::Car;
    bool rejected=false;try{applyProcessingToSlice(mixed,settings);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"incompatible reference units accepted");
    auto temperature=raw;temperature.channelUnits={QString::fromUtf8("°C"),"degC"};check(normalizedSignalWindow(temperature).unit=="degC","temperature unit alias");
    raw.displayOnly=true;rejected=false;try{fresh.process(raw,settings,true);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"display envelope accepted as samples");
    auto volts=fixture(2);volts.unit="V";volts.channelUnits={"V","mV"};volts.data={{1e-6f,2e-6f},{.001f,.002f}};
    const auto uv=normalizedSignalWindow(volts);check(uv.unit=="uV" && std::abs(uv.data[0][1]-2)<1e-6 && std::abs(uv.data[1][1]-2)<1e-6,"voltage calibration");
    std::cout<<"SIGNAL_WINDOW_TEST PASS gap_segments causal_overlap state_reset sparse_selection units display_envelope_rejected\n";return 0;
}catch(const std::exception& e){std::cerr<<"SIGNAL_WINDOW_TEST FAIL "<<e.what()<<'\n';return 1;}}

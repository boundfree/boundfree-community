#include "render/signal_processor.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
namespace bf {
void SignalProcessor::reset() { identity_.clear(); configuration_.clear(); cached_={}; haveTime_=false; }
WaveformSlice SignalProcessor::process(const WaveformSlice& input,const ProcessingViewState& settings,bool causal) {
    validateSignalProcessing(input,settings);
    if (!causal) { reset(); return applyProcessingToSlice(input,settings); }
    auto raw=normalizedSignalWindow(input);
    if (raw.data.empty() || raw.fs<=0) return raw;
    const auto cfg=settings.toConfig(raw.fs,static_cast<int>(raw.data.size()));
    if (cfg.referenceMode!=dsp::ReferenceMode::Raw)
        for(int c=1;c<static_cast<int>(raw.data.size());++c)
            if(signalUnit(raw,c)!=signalUnit(raw,0))throw std::invalid_argument("signal.incompatible_units");
    QJsonArray notch,bands;for(double f:cfg.notchFreqs)notch.append(f);
    for(const auto& b:cfg.bandpass)bands.append(QJsonArray{b.enabled,b.low,b.high});
    const auto key=QString::fromUtf8(QJsonDocument(QJsonArray{cfg.fs,cfg.nChannels,int(cfg.referenceMode),notch,bands,QJsonArray::fromStringList(raw.channelUnits)}).toJson(QJsonDocument::Compact));
    const auto identity=raw.sourceId+"/"+raw.streamId;
    const auto time=[&](std::size_t i){return raw.sampleTimes.empty()?raw.t0+i/raw.fs:raw.sampleTimes.front()[i];};
    if(identity_!=identity || configuration_!=key || (haveTime_ && !raw.data.front().empty() && time(raw.data.front().size()-1)<lastTime_-1e-9)) {
        reset();identity_=identity;configuration_=key;pipeline_.rebuild(cfg);
    }
    const int outputs=cfg.referenceMode==dsp::ReferenceMode::Bipolar && cfg.nChannels>=2?cfg.nChannels-1:cfg.nChannels;
    if(cached_.data.empty()) {
        cached_=raw;cached_.data.assign(outputs,{});cached_.sampleTimes.assign(outputs,{});
        std::vector<std::string> names;for(const auto& n:raw.channelNames)names.push_back(n.toStdString());
        cached_.channelNames=qStringListFromStd(dsp::effectiveChannelNames(names,cfg.referenceMode,outputs));
        while(cached_.channelUnits.size()>outputs)cached_.channelUnits.removeLast();
        if(outputs<cfg.nChannels){cached_.channelIds.clear();for(int c=0;c<outputs;++c)cached_.channelIds<<raw.channelIds.value(c)+"-"+raw.channelIds.value(c+1);}
    }
    std::size_t first=0;while(first<raw.data.front().size() && haveTime_ && time(first)<=lastTime_+1e-9)++first;
    while(first<raw.data.front().size()) {
        const double t=time(first);bool valid=std::isfinite(t);
        for(const auto& ch:raw.data)valid=valid && first<ch.size() && std::isfinite(ch[first]);
        if(!valid) {
            for(int c=0;c<outputs;++c){cached_.data[c].push_back(std::numeric_limits<float>::quiet_NaN());cached_.sampleTimes[c].push_back(t);}
            pipeline_.rebuild(cfg);++first;continue;
        }
        if(haveTime_ && std::abs(t-lastTime_-1/raw.fs)>1/raw.fs*1e-4) pipeline_.rebuild(cfg);
        std::size_t end=first+1;
        while(end<raw.data.front().size()) {
            bool good=std::abs(time(end)-time(end-1)-1/raw.fs)<=1/raw.fs*1e-4;
            for(const auto& ch:raw.data)good=good && end<ch.size() && std::isfinite(ch[end]);
            if(!good)break;++end;
        }
        dsp::Matrix part;for(const auto& ch:raw.data)part.emplace_back(ch.begin()+first,ch.begin()+end);
        const auto processed=pipeline_.processRealtime(part);
        for(int c=0;c<outputs;++c)for(std::size_t i=first;i<end;++i){cached_.data[c].push_back(processed[c][i-first]);cached_.sampleTimes[c].push_back(time(i));}
        lastTime_=time(end-1);haveTime_=true;first=end;
    }
    cached_.fs=raw.fs;cached_.t0=raw.t0;cached_.t1=raw.t1;cached_.metadata=raw.metadata;
    cached_=signalWindowRange(cached_,raw.t0,raw.t1+1e-9);
    return cached_;
}
}

// ProcessingViewState per-channel bandpass override self-test.
// Style references i18n_locale_format_test.cpp: custom main, returns non-zero on
// failure, prints PASS/FAIL lines. No GUI required -- ProcessingViewState is a
// plain value struct and toConfig() is pure logic.
//
// Coverage:
//   (a) with no overrides, toConfig() emits the global bandpass for every
//       channel (identical to the pre-override behavior)
//   (b) after writing overrides for some channels, overridden channels carry
//       the override values while the rest still carry global
//   (c) clearAllChannelBandpass() returns every channel to global
//   (d) summary() is unchanged when no override is set, and gains a (+N ovr)
//       suffix once overrides exist

#include "render/processing_view_state.h"

#include <QString>
#include <iostream>

namespace {

int failures = 0;

void check(bool ok, const std::string& label) {
    if (ok) {
        std::cout << "  ok: " << label << "\n";
    } else {
        std::cerr << "  FAIL: " << label << "\n";
        ++failures;
    }
}

bool channelEquals(const bf::dsp::ChannelBandpassSetting& actual,
                   bool enabled, double low, double high) {
    return actual.enabled == enabled
        && actual.low == low
        && actual.high == high;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const double fs = 1000.0;
    const int nChannels = 4;

    // ----- (a) no overrides: every channel equals global -------------------
    {
        bf::ProcessingViewState s;
        s.globalBandpassEnabled = true;
        s.bandpassLow = 0.5;
        s.bandpassHigh = 40.0;

        const bf::dsp::ProcessingConfig cfg = s.toConfig(fs, nChannels);
        check(static_cast<int>(cfg.bandpass.size()) == nChannels,
              "no-override: bandpass vector sized to nChannels");
        bool allGlobal = true;
        for (int ch = 0; ch < nChannels; ++ch) {
            if (!channelEquals(cfg.bandpass[static_cast<std::size_t>(ch)],
                               s.globalBandpassEnabled, s.bandpassLow, s.bandpassHigh)) {
                allGlobal = false;
            }
        }
        check(allGlobal, "no-override: every channel == global");
        check(s.channelBandpassOverrideCount() == 0, "no-override: override count == 0");

        // summary must not mention overrides when none are set.
        const QString sum = s.summary();
        check(!sum.contains("ovr"), "no-override: summary has no ovr suffix");
    }

    // ----- (b) overrides on channels 0 and 2 -------------------------------
    {
        bf::ProcessingViewState s;
        s.globalBandpassEnabled = true;
        s.bandpassLow = 0.5;
        s.bandpassHigh = 40.0;
        s.setChannelBandpass(0, {true, 1.0, 30.0});
        s.setChannelBandpass(2, {false, 0.1, 100.0});

        const bf::dsp::ProcessingConfig cfg = s.toConfig(fs, nChannels);
        check(channelEquals(cfg.bandpass[0], true, 1.0, 30.0),
              "override: ch0 uses override (enabled,1-30)");
        check(channelEquals(cfg.bandpass[1], true, 0.5, 40.0),
              "override: ch1 falls back to global (enabled,0.5-40)");
        check(channelEquals(cfg.bandpass[2], false, 0.1, 100.0),
              "override: ch2 uses override (disabled,0.1-100)");
        check(channelEquals(cfg.bandpass[3], true, 0.5, 40.0),
              "override: ch3 falls back to global (enabled,0.5-40)");
        check(s.channelBandpassOverrideCount() == 2, "override: override count == 2");
    }

    // ----- (c) clearAll returns to global ----------------------------------
    {
        bf::ProcessingViewState s;
        s.globalBandpassEnabled = true;
        s.bandpassLow = 0.5;
        s.bandpassHigh = 40.0;
        s.setChannelBandpass(0, {true, 1.0, 30.0});
        s.setChannelBandpass(2, {false, 0.1, 100.0});
        s.clearAllChannelBandpass();

        const bf::dsp::ProcessingConfig cfg = s.toConfig(fs, nChannels);
        bool allGlobal = true;
        for (int ch = 0; ch < nChannels; ++ch) {
            if (!channelEquals(cfg.bandpass[static_cast<std::size_t>(ch)],
                               s.globalBandpassEnabled, s.bandpassLow, s.bandpassHigh)) {
                allGlobal = false;
            }
        }
        check(allGlobal, "clearAll: every channel == global again");
        check(s.channelBandpassOverrideCount() == 0, "clearAll: override count == 0");
    }

    // ----- (d) clearChannelBandpass removes a single override -------------
    {
        bf::ProcessingViewState s;
        s.globalBandpassEnabled = true;
        s.bandpassLow = 0.5;
        s.bandpassHigh = 40.0;
        s.setChannelBandpass(1, {true, 2.0, 20.0});
        check(s.channelBandpassOverrideCount() == 1, "single: override count == 1");
        s.clearChannelBandpass(1);
        check(s.channelBandpassOverrideCount() == 0, "single: override count == 0 after clear");
        const bf::dsp::ProcessingConfig cfg = s.toConfig(fs, nChannels);
        check(channelEquals(cfg.bandpass[1], true, 0.5, 40.0),
              "single: ch1 back to global after clear");
    }

    // ----- (e) summary gains (+N ovr) when overrides exist -----------------
    {
        bf::ProcessingViewState s;
        s.globalBandpassEnabled = true;
        s.bandpassLow = 0.5;
        s.bandpassHigh = 40.0;
        s.setChannelBandpass(0, {true, 1.0, 30.0});
        const QString sum = s.summary();
        check(sum.contains("bp=0.5-40(+1ovr)"),
              "summary: bp text carries (+1ovr) suffix");
    }

    if (failures == 0) {
        std::cout << "processing-view-state-test-ok\n";
        return 0;
    }
    std::cerr << "processing-view-state-test-failed failures=" << failures << "\n";
    return 1;
}

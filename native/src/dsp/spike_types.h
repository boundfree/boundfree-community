#pragma once

#include <vector>

namespace bf::dsp {

// Shared Spike data model. EEG uses [n_channels, n_samples] + Spectrum/Spectrogram;
// Spike uses "discharge sequence organized by cluster + waveform snippet" as a unified vehicle between analysis layer and rendering layer.
// All values related to sampling rate (snippetLen, timestamps) are derived from fs during the detection phase, no hardcoding.

// Single cluster (putative single unit) discharge sequence and waveforms.
struct SpikeCluster {
    int id = 0;                                  // Cluster ID [0, nClusters)
    std::vector<double> timestamps;              // All discharge times of this cluster, in seconds, ascending
    std::vector<std::vector<double>> waveforms;  // [n_spikes][snippetLen] aligned waveform snippets
    std::vector<double> meanWaveform;            // [snippetLen] mean waveform (template)
};

// Complete Spike analysis result of a single channel (or a single detection+sorting run).
// This is the unified model produced by spike_analysis_service and consumed by all Spike views.
struct SpikeAnalysis {
    double fs = 0.0;               // Sampling rate Hz
    double duration = 0.0;         // Recording duration in seconds (used for raster/PSTH timeline)
    int snippetLen = 0;            // Waveform snippet length (samples)
    double preMs = 0.5;            // Snippet alignment pre-window (used for waveform view timeline)
    double postMs = 1.0;           // Snippet alignment post-window
    int channel = 0;               // Source channel
    int totalSpikes = 0;           // Total number of discharges across all clusters
    std::vector<SpikeCluster> clusters;
};

}  // namespace bf::dsp

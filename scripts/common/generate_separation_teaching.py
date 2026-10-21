#!/usr/bin/env python3
"""Generate the BoundFree-authored, explicitly synthetic offline tutorial.

This creates new mathematical signals, never patient/EEGdenoiseNet recordings.
The committed manifest pins the actual HDF5 bytes; regenerating intentionally
updates its hash so templates cannot accidentally apply to unrelated files.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
import h5py


def generate(destination):
    destination.mkdir(parents=True, exist_ok=True)
    fs = 500
    t = np.arange(16 * fs, dtype=np.float64) / fs
    rng = np.random.default_rng(12138)
    brain = 12 * np.sin(2 * np.pi * 10 * t) + 4 * np.sin(2 * np.pi * 63 * t)
    eye = 18 * np.sin(2 * np.pi * .18 * t)
    for blink in (2.2, 5.1, 9.3, 11.6, 14.2):
        eye += 100 * np.exp(-((t - blink) / .12) ** 2)
    muscle = (rng.random(len(t)) - .5) * (8 + 65 * np.exp(-((t - 7) / .5) ** 2))
    mixing = np.array([[1., .5, .3], [.3, 1.2, .1], [1., -.2, 1.1]])
    observations = mixing @ np.array([brain, eye, muscle]) + np.array([13., 14., 15.])[:, None]
    names = ['Mixed-1', 'Mixed-2', 'Mixed-3', 'Eye-reference', 'Muscle-reference']
    path = destination / 'teaching.h5'
    with h5py.File(path, 'w', track_order=True) as output:
        data = output.create_dataset('eeg', data=np.vstack([observations, eye, muscle]), dtype='<f8')
        data.attrs['fs'] = fs
        data.attrs['unit'] = 'uV'
        data.attrs['ch_names'] = names
        output.attrs['synthetic'] = True
        output.attrs['description'] = 'BoundFree mathematical teaching mixture; not a physiological validation'
    manifest = {'schema': 'boundfree.separation-teaching.v1', 'file': 'teaching.h5',
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'fs': fs, 'n_samples': len(t), 'ch_names': names,
                'channel_ids': [str(i) for i in range(5)], 'roles': [1, 1, 1, 2, 3],
                'seed': 12138, 'duration_s': 16, 'synthetic': True,
                'generator': 'scripts/common/generate_separation_teaching.py',
                'mixing': mixing.tolist(), 'sources': ['oscillations', 'slow/blink waveform', 'burst noise'],
                'notice': 'Synthetic mathematical mixture for learning the workflow; not evidence of physiological separation accuracy.'}
    (destination / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'samples/offline-eeg')
    print(json.dumps(generate(parser.parse_args().output), ensure_ascii=False, indent=2))

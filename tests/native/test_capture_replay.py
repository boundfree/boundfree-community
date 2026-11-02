"""Writer → sidecar → shared native waveform, including multi-rate streams."""
import os
from pathlib import Path
import subprocess
import sys
from uuid import uuid4

import numpy as np

from boundfree_sidecar.protocol import handle_request
from test_bfsp_native import native_binary


def make_recording(tmp_path):
    def call(method, params, payload=None):
        r = handle_request({'id': method, 'method': method, 'params': params}, payload)
        assert r['ok'], r
        return r['result']

    streams = []
    for i, fs, unit in ((1, 250, 'count'), (2, 25, 'degC')):
        streams.append(dict(device_id='replay-test', boot_id=str(uuid4()), session_id=str(uuid4()),
            capture_id=str(uuid4()), generation='1', stream_id=i, descriptor_revision=1,
            name=f'Stream {i}', signal_type='other', rate_num=fs, rate_den=1,
            dtype='float64', layout='channel_major', max_samples_per_block=1000,
            channels=[dict(channel_id='ch1', name='Channel 1', signal_type='other', unit=unit, scale=1, offset=0)],
            clock=dict(clock_id=i, ticks_per_second_num='1000000', ticks_per_second_den='1',
                       source='host_monotonic', epoch='boot_relative', simulated=True)))
    opened = call('capture_open', dict(out_dir=str(tmp_path), streams=streams,
        host_clock=dict(source='monotonic', boot_id='test', monotonic_base_ns='1000')))
    summaries = []
    for d in streams:
        ident = {k: d[k] for k in ('device_id', 'boot_id', 'session_id', 'capture_id', 'generation', 'stream_id', 'descriptor_revision')}
        n = d['rate_num'] * 4
        call('capture_append', dict(handle=opened['handle'], stream=ident, dtype='float64', shape=[1, n],
            sample_count=n, first_sample_index='0', first_timestamp_ticks='0', host_received_monotonic_ns='1000',
            clock_id=d['clock']['clock_id'], time_flags=1, quality_flags=0), np.sin(np.arange(n)).astype('<f8').tobytes())
        summaries.append(dict(stream=ident, last_accepted_sample_exclusive=str(n), terminal_sample_exclusive=str(n), complete=True))
    call('capture_finalize', dict(handle=opened['handle'], streams=summaries))
    return opened['path']


def test_capture_native_replay_units_stream_selection_seek_playback_and_layout(tmp_path):
    app = native_binary('bf_app')
    path = make_recording(tmp_path)
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='offscreen', BF_PYTHON_EXECUTABLE=sys.executable, BF_SIDECAR_EXECUTABLE='')
    root = Path(__file__).resolve().parents[2]
    result = subprocess.run([str(app), '--smoke-ui-capture-replay', path], cwd=root, env=env,
                            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'CAPTURE_REPLAY_SMOKE PASS' in result.stdout

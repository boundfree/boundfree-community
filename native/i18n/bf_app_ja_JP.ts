<?xml version="1.0" encoding="utf-8"?>
<TS version="2.1" language="ja_JP">
  <context>
    <name>app</name>
    <message>
      <source>app.window_title</source>
      <translation>{product_name} {version}</translation>
    </message>
    <message>
      <source>app.main.empty_state</source>
      <translation>No file open</translation>
    </message>
    <message>
      <source>app.menu.file</source>
      <translation>File</translation>
    </message>
    <message>
      <source>app.menu.language</source>
      <translation>Language</translation>
    </message>
    <message>
      <source>app.action.open</source>
      <translation>Open File</translation>
    </message>
    <message>
      <source>app.action.quit</source>
      <translation>Quit</translation>
    </message>
    <message>
      <source>app.dialog.open_title</source>
      <translation>Open EEG File</translation>
    </message>
    <message>
      <source>app.dialog.open_filter</source>
      <translation>EEG files (*.csv *.h5 *.hdf5 *.edf *.bdf *.vhdr *.set *.fif);;All files (*)</translation>
    </message>
    <message>
      <source>app.browser.previous</source>
      <translation>Previous window</translation>
    </message>
    <message>
      <source>app.browser.next</source>
      <translation>Next window</translation>
    </message>
    <message>
      <source>app.browser.position</source>
      <translation>Start</translation>
    </message>
    <message>
      <source>app.browser.window</source>
      <translation>Window</translation>
    </message>
    <message>
      <source>app.browser.amplitude</source>
      <translation>Amplitude</translation>
    </message>
    <message>
      <source>app.browser.first_channel</source>
      <translation>First channel</translation>
    </message>
    <message>
      <source>app.browser.channels</source>
      <translation>Channels</translation>
    </message>
    <message>
      <source>app.status.ready</source>
      <translation>Ready</translation>
    </message>
    <message>
      <source>app.status.sidecar_connecting</source>
      <translation>Connecting sidecar</translation>
    </message>
    <message>
      <source>app.status.sidecar_connected</source>
      <translation>Sidecar connected</translation>
    </message>
    <message>
      <source>app.status.sidecar_failed</source>
      <translation>Sidecar connection failed: {error}</translation>
    </message>
    <message>
      <source>app.status.file_opening</source>
      <translation>Opening {path}</translation>
    </message>
    <message>
      <source>app.status.file_failed</source>
      <translation>Could not open file: {error}</translation>
    </message>
    <message>
      <source>app.status.file_opened</source>
      <translation>{format} | {fs} Hz | {n_channels} channels</translation>
    </message>
    <message>
      <source>app.status.file_window</source>
      <translation>{format} | {t0}-{t1} s | +/-{amplitude} uV | {n_visible} visible channels</translation>
    </message>
    <message>
      <source>app.processing.reference</source>
      <translation>Reference</translation>
    </message>
    <message>
      <source>app.processing.ref.raw</source>
      <translation>Raw</translation>
    </message>
    <message>
      <source>app.processing.ref.car</source>
      <translation>CAR</translation>
    </message>
    <message>
      <source>app.processing.ref.bipolar</source>
      <translation>Bipolar</translation>
    </message>
    <message>
      <source>app.processing.notch</source>
      <translation>Notch</translation>
    </message>
    <message>
      <source>app.processing.harmonics</source>
      <translation>Harmonics</translation>
    </message>
    <message>
      <source>app.processing.bandpass</source>
      <translation>Bandpass</translation>
    </message>
    <message>
      <source>app.processing.panel</source>
      <translation>Processing</translation>
    </message>
    <message>
      <source>app.language.zh_CN</source>
      <translation>Simplified Chinese</translation>
    </message>
    <message>
      <source>app.language.zh_TW</source>
      <translation>Traditional Chinese</translation>
    </message>
    <message>
      <source>app.language.en_US</source>
      <translation>English</translation>
    </message>
    <message>
      <source>app.language.ja_JP</source>
      <translation>Japanese</translation>
    </message>
    <message>
      <source>app.language.ko_KR</source>
      <translation>Korean</translation>
    </message>
    <message>
      <source>app.language.de_DE</source>
      <translation>German</translation>
    </message>
    <message>
      <source>app.language.fr_FR</source>
      <translation>French</translation>
    </message>
    <message>
      <source>app.language.es_ES</source>
      <translation>Spanish</translation>
    </message>
    <message>
      <source>app.language.pseudo</source>
      <translation>Pseudo</translation>
    </message>
    <message>
      <source>error.protocol.invalid_json</source>
      <translation>Protocol message is not valid JSON</translation>
    </message>
    <message>
      <source>error.protocol.missing_field</source>
      <translation>Protocol message is missing field: {field}</translation>
    </message>
    <message>
      <source>error.protocol.unknown_method</source>
      <translation>Unknown protocol method: {method}</translation>
    </message>
    <message>
      <source>error.protocol.version_mismatch</source>
      <translation>Protocol version mismatch, expected {expected}, got {actual}</translation>
    </message>
    <message>
      <source>error.protocol.invalid_params</source>
      <translation>Invalid protocol parameter: {field}, {reason}</translation>
    </message>
    <message>
      <source>error.file.not_found</source>
      <translation>File does not exist: {path}</translation>
    </message>
    <message>
      <source>error.reader.unsupported_format</source>
      <translation>Unsupported file format: {path}, suffix {suffix}</translation>
    </message>
    <message>
      <source>error.reader.open_failed</source>
      <translation>Reader failed: {reason}</translation>
    </message>
    <message>
      <source>error.reader.session_not_found</source>
      <translation>File session does not exist: {handle}</translation>
    </message>
    <message>
      <source>error.reader.slice_out_of_range</source>
      <translation>Slice is outside file duration: {handle}, {t0}-{t1}, duration {duration}</translation>
    </message>
    <message>
      <source>error.ipc.sidecar_start_failed</source>
      <translation>Could not start sidecar: {reason}</translation>
    </message>
    <message>
      <source>error.ipc.sidecar_timeout</source>
      <translation>Sidecar operation timed out: {operation}, {timeout_ms} ms</translation>
    </message>
    <message>
      <source>error.ipc.sidecar_exited</source>
      <translation>Sidecar exited unexpectedly with code: {exit_code}</translation>
    </message>
    <message>
      <source>error.sidecar.initialization_failed</source>
      <translation>Sidecar initialization failed: {reason}</translation>
    </message>
    <message>
      <source>error.license.missing</source>
      <translation>Offline license file is missing: {path}</translation>
    </message>
    <message>
      <source>error.license.invalid_signature</source>
      <translation>Offline license signature is invalid: {reason}</translation>
    </message>
    <message>
      <source>error.license.expired</source>
      <translation>Offline license expired at: {expires_at}</translation>
    </message>
    <message>
      <source>error.internal.unexpected</source>
      <translation>Internal error: {operation}</translation>
    </message>
    <message>
      <source>app.analysis.panel</source>
      <translation>Analysis</translation>
    </message>
    <message>
      <source>app.analysis.psd</source>
      <translation>PSD</translation>
    </message>
    <message>
      <source>app.analysis.fft</source>
      <translation>FFT amplitude</translation>
    </message>
    <message>
      <source>app.analysis.stft</source>
      <translation>STFT</translation>
    </message>
    <message>
      <source>app.analysis.trend</source>
      <translation>Band power trend</translation>
    </message>
    <message>
      <source>app.analysis.coherence</source>
      <translation>Coherence</translation>
    </message>
    <message>
      <source>app.analysis.db</source>
      <translation>PSD (dB)</translation>
    </message>
    <message>
      <source>app.analysis.psd_unit</source>
      <translation>PSD (uV^2/Hz)</translation>
    </message>
    <message>
      <source>app.analysis.welch</source>
      <translation>Welch PSD</translation>
    </message>
    <message>
      <source>app.analysis.db_display</source>
      <translation>dB</translation>
    </message>
    <message>
      <source>app.analysis.high_time</source>
      <translation>High time</translation>
    </message>
    <message>
      <source>app.analysis.balanced</source>
      <translation>Balanced</translation>
    </message>
    <message>
      <source>app.analysis.high_frequency</source>
      <translation>High frequency</translation>
    </message>
    <message>
      <source>error.analysis.same_channel</source>
      <translation>Select two different channels.</translation>
    </message>
    <message>
      <source>error.analysis.failed</source>
      <translation>Analysis failed.</translation>
    </message>
    <message>
      <source>error.analysis.invalid_input</source>
      <translation>Analysis input is invalid.</translation>
    </message>
    <message>
      <source>error.analysis.invalid_channel</source>
      <translation>Analysis channel is invalid.</translation>
    </message>
    <message>
      <source>app.analysis.amplitude</source>
      <translation>Amplitude (uV)</translation>
    </message>
    <message>
      <source>app.analysis.time</source>
      <translation>Time (s)</translation>
    </message>
    <message>
      <source>app.analysis.frequency</source>
      <translation>Frequency (Hz)</translation>
    </message>
    <message>
      <source>app.analysis.empty</source>
      <translation>No analysis data</translation>
    </message>
    <message><source>app.playback.play</source><translation>Play</translation></message>
    <message><source>app.playback.pause</source><translation>Pause</translation></message>
    <message><source>app.playback.speed</source><translation>Speed</translation></message>
    <message><source>app.marker.mode</source><translation>Marker mode</translation></message>
    <message><source>app.marker.mode_tooltip</source><translation>Click the waveform to add an event marker</translation></message>
    <message><source>app.marker.panel</source><translation>Event markers</translation></message>
    <message><source>app.marker.time</source><translation>Time (s)</translation></message>
    <message><source>app.marker.label</source><translation>Label</translation></message>
    <message><source>app.marker.color</source><translation>Color</translation></message>
    <message><source>app.marker.delete</source><translation>Delete selected</translation></message>
    <message><source>app.marker.clear</source><translation>Clear all</translation></message>
    <message><source>app.marker.export</source><translation>Export CSV</translation></message>
    <message><source>app.marker.add_title</source><translation>Add marker</translation></message>
    <message><source>app.marker.add_prompt</source><translation>Time: {time} s; enter a label (optional)</translation></message>
    <message><source>app.video.panel</source><translation>Video sync</translation></message>
    <message><source>app.video.open</source><translation>Open video</translation></message>
    <message><source>app.video.offset</source><translation>Offset</translation></message>
    <message><source>app.video.offset_tooltip</source><translation>EEG time corresponding to the first video frame</translation></message>
    <message><source>app.video.apply</source><translation>Apply</translation></message>
    <message><source>app.video.apply_tooltip</source><translation>Apply the offset and synchronize to the current EEG time</translation></message>
    <message><source>app.video.not_loaded</source><translation>No video loaded</translation></message>
    <message><source>app.video.filter</source><translation>Video files (*.mp4 *.avi *.mov *.mkv *.wmv *.m4v);;All files (*)</translation></message>
    <message><source>app.video.loaded</source><translation>Video loaded: {name}</translation></message>
    <message><source>app.video.offset_applied</source><translation>Video offset applied: {offset} s</translation></message>
    <message><source>app.video.load_failed</source><translation>Could not load video: {detail}</translation></message>
    <message><source>app.record.start</source><translation>Start recording</translation></message>
    <message><source>app.record.stop</source><translation>Stop recording</translation></message>
    <message><source>app.record.started</source><translation>Recording started</translation></message>
    <message><source>app.record.completed</source><translation>Recording completed: {samples} samples</translation></message>
    <message><source>app.record.no_file</source><translation>Open an EEG file first</translation></message>
    <message><source>app.record.metadata</source><translation>Recording metadata</translation></message>
    <message><source>app.record.animal_id</source><translation>Animal ID</translation></message>
    <message><source>app.record.exp_type</source><translation>Experiment type</translation></message>
    <message><source>app.record.notes</source><translation>Notes</translation></message>
    <message><source>app.record.output_dir</source><translation>Select recording directory</translation></message>
    <message><source>app.record.status</source><translation>REC {elapsed}; samples {samples}; dropped {dropped}</translation></message>
    <message><source>app.record.low_disk</source><translation>Disk space below 500 MiB; REC {elapsed}; samples {samples}; dropped {dropped}</translation></message>
    <message><source>app.export.action</source><translation>Export segment</translation></message>
    <message><source>app.export.title</source><translation>Export EEG segment</translation></message>
    <message><source>app.export.start</source><translation>Start time (s)</translation></message>
    <message><source>app.export.end</source><translation>End time (s)</translation></message>
    <message><source>app.export.filter</source><translation>EDF (*.edf);;HDF5 (*.h5 *.hdf5);;CSV (*.csv)</translation></message>
    <message><source>app.export.running</source><translation>Exporting segment</translation></message>
    <message><source>app.export.completed</source><translation>Export completed: {path}</translation></message>
    <message><source>error.ipc.invalid_frame</source><translation>Invalid sidecar frame: {reason}</translation></message>
    <message><source>error.record.invalid_state</source><translation>Recording is unavailable in state {state}</translation></message>
    <message><source>error.record.session_not_found</source><translation>Recording session not found: {handle}</translation></message>
    <message><source>error.record.open_failed</source><translation>Could not create recording files in {path}: {reason}</translation></message>
    <message><source>error.record.invalid_chunk</source><translation>Invalid recording chunk: {reason}</translation></message>
    <message><source>error.record.writer_failed</source><translation>Recording session {handle} failed: {reason}</translation></message>
    <message><source>error.record.close_timeout</source><translation>Recording session {handle} did not close within {timeout_ms} ms</translation></message>
    <message><source>error.export.invalid_range</source><translation>Export range {t0}-{t1} s exceeds duration {duration} s</translation></message>
    <message><source>error.export.unsupported_format</source><translation>Export path {path} has unsupported format {suffix}</translation></message>
    <message><source>error.export.busy</source><translation>Another export is already running</translation></message>
    <message><source>error.export.failed</source><translation>Segment export to {path} failed: {reason}</translation></message>

  </context>
</TS>
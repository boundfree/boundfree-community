"""Layering check must catch upward includes and dsp calculations inside ui."""
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/common'))
import check_layering


def make_tree(tmp_path, files):
    root = tmp_path / 'src'
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    return root


def scan(tmp_path, files, include_exceptions=(), dsp_call_exceptions=()):
    root = make_tree(tmp_path, files)
    return check_layering.scan(root, include_exceptions, dsp_call_exceptions)


def test_real_source_tree_passes_with_registered_exceptions():
    # 真实源码扫描通过，同时说明白名单条目仍然命中，没有失效残留。
    assert check_layering.scan() == []


def test_render_include_of_ui_fails_the_cli(tmp_path, capsys):
    root = make_tree(tmp_path, {'render/view.h': '#include "ui/main_window.h"\n'})
    assert check_layering.main(['--src-root', str(root)]) == 1
    stderr = capsys.readouterr().err
    assert 'render/view.h:1' in stderr
    assert 'ui/main_window.h' in stderr
    assert 'does not depend on ui' in stderr


@pytest.mark.parametrize('source,include', [
    ('render/view.h', 'ui/main_window.h'),
    ('render/view.h', 'controller/analysis_service.h'),
    ('render/view.h', 'ipc/sidecar_client.h'),
    ('ipc/client.h', 'render/waveform_slice.h'),
    ('i18n/service.h', 'ui/plot_style.h'),
    ('core/types.h', 'dsp/types.h'),
    ('dsp/types.h', 'core/app_error.h'),
    ('acquisition/model/signal_model.h', 'acquisition/control/acquisition_controller.h'),
    ('acquisition/transport/client.h', 'acquisition/recording/capture_recorder.h'),
    ('controller/service.h', 'ui/main_window.h'),
    ('ui/main_window.h', 'app/main.cpp'),
])
def test_upward_include_is_rejected(tmp_path, source, include):
    findings = scan(tmp_path, {source: f'#include "{include}"\n'})
    assert any(include in finding and 'crosses layers' in finding for finding in findings), findings


@pytest.mark.parametrize('source,include', [
    ('core/app_error.h', 'core/version.h'),
    ('dsp/types.h', 'dsp/spectral.h'),
    ('render/view.h', 'dsp/types.h'),
    ('render/view.h', 'core/version.h'),
    ('ipc/client.h', 'core/app_error.h'),
    ('i18n/service.h', 'core/version.h'),
    ('acquisition/model/signal_model.h', 'acquisition/protocol/codec.h'),
    ('acquisition/recording/recorder.h', 'ipc/sidecar_client.h'),
    ('acquisition/control/controller.h', 'acquisition/sources/source.h'),
    ('controller/service.h', 'acquisition/control/acquisition_controller.h'),
    ('controller/service.h', 'render/waveform_slice.h'),
    ('ui/main_window.h', 'controller/analysis_service.h'),
    ('ui/main_window.h', 'dsp/spectral.h'),
    ('app/main.cpp', 'ui/main_window.h'),
    ('app/smoke/smoke_entries.cpp', 'controller/marker_store.h'),
])
def test_downward_include_is_allowed(tmp_path, source, include):
    assert scan(tmp_path, {source: f'#include "{include}"\n'}) == []


def test_local_include_resolves_inside_its_directory(tmp_path):
    findings = scan(tmp_path, {
        'acquisition/protocol/codec.h': '',
        'acquisition/protocol/internal.h': '#include "codec.h"\n',
        'acquisition/transport/client.cpp': '#include "../protocol/codec.h"\n',
    })
    assert findings == []


@pytest.mark.parametrize('include', [
    'core/../ui/main_window.h',
    'render/../ui/main_window.h',
    'ipc/../ui/main_window.h',
])
def test_path_traversal_include_is_rejected(tmp_path, include):
    findings = scan(tmp_path, {'render/view.cpp': f'#include "{include}"\n'})
    assert any('does not depend on ui' in finding for finding in findings), findings


def test_relative_traversal_include_is_rejected(tmp_path):
    findings = scan(tmp_path, {'render/inner/view.cpp': '#include "../../core/../ui/main_window.h"\n'})
    assert any('does not depend on ui' in finding for finding in findings), findings


def test_include_inside_raw_string_is_ignored(tmp_path):
    findings = scan(tmp_path, {
        'render/view.cpp': 'auto sample = R"(\n#include "ui/main_window.h"\n)";\n',
    })
    assert findings == []


def test_include_after_raw_string_is_still_checked(tmp_path):
    findings = scan(tmp_path, {
        'render/view.cpp': (
            'auto sample = R"(\n'
            '#include "core/version.h"\n'
            ')";\n'
            '#include "ui/main_window.h"\n'
        ),
    })
    assert len(findings) == 1, findings
    assert 'render/view.cpp:4' in findings[0] and 'ui/main_window.h' in findings[0]


def test_comment_does_not_join_tokens_into_an_include(tmp_path):
    findings = scan(tmp_path, {'render/view.cpp': '#inclu/*x*/de "ui/main_window.h"\n'})
    assert findings == []


def test_comment_between_hash_and_include_is_still_a_directive(tmp_path):
    findings = scan(tmp_path, {'render/view.cpp': '#/* spacing */include "ui/main_window.h"\n'})
    assert len(findings) == 1, findings
    assert 'render/view.cpp:1' in findings[0] and 'ui/main_window.h' in findings[0]


def test_line_numbers_survive_multiline_block_comments(tmp_path):
    findings = scan(tmp_path, {
        'render/view.cpp': '/* banner\n   second line */\n#include "ui/main_window.h"\n',
    })
    assert len(findings) == 1, findings
    assert 'render/view.cpp:3' in findings[0]


def test_new_acquisition_subdir_is_foundation(tmp_path):
    assert scan(tmp_path, {'acquisition/experimental/thing.h': '#include "core/version.h"\n'}) == []


def test_new_acquisition_subdir_cannot_reach_coordination(tmp_path):
    findings = scan(tmp_path, {
        'acquisition/experimental/thing.h': '#include "acquisition/control/acquisition_controller.h"\n',
    })
    assert any('does not depend on acquisition/coordination' in finding for finding in findings)


def test_include_into_unknown_acquisition_subdir_is_not_skipped(tmp_path):
    findings = scan(tmp_path, {'core/types.h': '#include "acquisition/experimental/thing.h"\n'})
    assert any('acquisition/experimental/thing.h' in finding and 'does not depend on' in finding
               for finding in findings)


def test_test_file_is_checked_as_its_own_directory(tmp_path):
    findings = scan(tmp_path, {
        'ui/main_window.h': '',
        'acquisition/control/acquisition_ui_test.cpp': '#include "ui/main_window.h"\n',
    })
    assert any('acquisition_ui_test.cpp' in finding and 'ui/main_window.h' in finding for finding in findings)


def test_commented_out_includes_are_ignored(tmp_path):
    findings = scan(tmp_path, {
        'render/view.cpp': (
            '// #include "ui/main_window.h"\n'
            '/* #include "controller/analysis_service.h"\n'
            '   #include "app/main.cpp" */\n'
            'int value = 0;\n'
        ),
    })
    assert findings == []


def test_ui_dsp_function_call_is_rejected(tmp_path):
    findings = scan(tmp_path, {
        'dsp/spectral.h': 'namespace bf::dsp { struct Spectrum { double value; }; }\n',
        'ui/main_window.cpp': 'void render() { auto powers = dsp::bandPower(spectrum, 1.0, 4.0); }\n',
    })
    assert any('dsp::bandPower' in finding and 'ui/main_window.cpp:1' in finding for finding in findings)


@pytest.mark.parametrize('body', [
    'for (const auto& band : dsp::eegBands()) {}',
    'for (const auto& band : bf::dsp::eegBands()) {}',
    'dsp::Spectrum spectrum{ {0.0}, {1.0} };',
    'dsp::Spectrum copy(dsp::Spectrum{ {0.0}, {1.0} });',
    'auto mode = static_cast<dsp::ReferenceMode>(0);',
    'dsp::ReferenceMode mode = dsp::ReferenceMode::Raw;',
])
def test_ui_type_references_and_constants_are_not_function_calls(tmp_path, body):
    findings = scan(tmp_path, {
        'dsp/spectral.h': 'struct Spectrum { double value; };\n',
        'dsp/types.h': 'enum class ReferenceMode { Raw };\n',
        'ui/panel.cpp': f'void draw() {{ {body} }}\n',
    })
    assert findings == []


def test_commented_and_quoted_dsp_calls_are_ignored(tmp_path):
    findings = scan(tmp_path, {
        'ui/panel.cpp': (
            '// dsp::bandPower(spectrum, 1.0, 4.0) moves to the controller in A2\n'
            'const char* hint = "dsp::computeWelchPsd(trace)";\n'
            '/* dsp::eegBands(); */\n'
        ),
    })
    assert findings == []


def test_registered_exception_suppresses_matching_violation(tmp_path):
    exception = check_layering.IncludeException(
        source='render/waveform_slice.h', include='ipc/sidecar_client.h', reason='A3'
    )
    findings = scan(
        tmp_path,
        {'render/waveform_slice.h': '#include "ipc/sidecar_client.h"\n'},
        include_exceptions=(exception,),
    )
    assert findings == []


def test_exception_matches_normalized_target(tmp_path):
    exception = check_layering.IncludeException(
        source='render/waveform_slice.h', include='ipc/sidecar_client.h', reason='A3'
    )
    findings = scan(
        tmp_path,
        {'render/waveform_slice.h': '#include "core/../ipc/sidecar_client.h"\n'},
        include_exceptions=(exception,),
    )
    assert findings == []


def test_stale_include_exception_is_reported(tmp_path):
    exception = check_layering.IncludeException(
        source='render/waveform_slice.h', include='ipc/sidecar_client.h', reason='A3'
    )
    findings = scan(
        tmp_path,
        {'render/waveform_slice.h': '#include "core/version.h"\n'},
        include_exceptions=(exception,),
    )
    assert any('stale exception' in finding for finding in findings)


def test_registered_dsp_call_exception_suppresses_matching_violation(tmp_path):
    exception = check_layering.DspCallException(
        source='ui/main_window.cpp', function='bandPower', reason='A2'
    )
    findings = scan(
        tmp_path,
        {'ui/main_window.cpp': 'void render() { dsp::bandPower(spectrum, 1.0, 4.0); }\n'},
        dsp_call_exceptions=(exception,),
    )
    assert findings == []


def test_stale_dsp_call_exception_is_reported(tmp_path):
    exception = check_layering.DspCallException(
        source='ui/main_window.cpp', function='bandPower', reason='A2'
    )
    findings = scan(
        tmp_path,
        {'ui/main_window.cpp': 'void render() { for (const auto& band : dsp::eegBands()) {} }\n'},
        dsp_call_exceptions=(exception,),
    )
    assert any('stale exception' in finding for finding in findings)


def test_unregistered_layer_directory_is_reported(tmp_path):
    findings = scan(tmp_path, {'plugin/thing.h': '#include "core/version.h"\n'})
    assert any('unregistered layer directory' in finding for finding in findings)


# 白名单按实测的精确 source/target 对登记，新增文件或新增 ui 头不会自动获得豁免。
WHITELISTED_UI_TEST_PAIRS = [
    ('acquisition/control/acquisition_ui_test.cpp', 'ui/device_workspace.h'),
    ('acquisition/control/acquisition_ui_test.cpp', 'ui/waveform_widget.h'),
    ('acquisition/control/acquisition_waveform_test.cpp', 'ui/plot_style.h'),
    ('acquisition/control/acquisition_waveform_test.cpp', 'ui/waveform_widget.h'),
]


def pair_files(extra=()):
    files = {}
    for source, include in list(WHITELISTED_UI_TEST_PAIRS) + list(extra):
        files[source] = files.get(source, '') + f'#include "{include}"\n'
    return files


def layer_violations(findings):
    """丢掉白名单失效提示，只看真实的越层违规。"""
    return [finding for finding in findings if not finding.startswith('stale exception')]


def test_registered_ui_test_exceptions_cover_only_the_measured_pairs(tmp_path):
    findings = scan(tmp_path, pair_files(), include_exceptions=check_layering.INCLUDE_EXCEPTIONS)
    assert layer_violations(findings) == []


def test_new_test_file_in_control_gets_no_exemption(tmp_path):
    findings = scan(
        tmp_path,
        pair_files([('acquisition/control/new_collector_test.cpp', 'ui/plot_style.h')]),
        include_exceptions=check_layering.INCLUDE_EXCEPTIONS,
    )
    violations = layer_violations(findings)
    assert len(violations) == 1, violations
    assert 'new_collector_test.cpp' in violations[0] and 'ui/plot_style.h' in violations[0]


def test_new_ui_header_include_gets_no_exemption(tmp_path):
    findings = scan(
        tmp_path,
        pair_files([('acquisition/control/acquisition_ui_test.cpp', 'ui/new_panel.h')]),
        include_exceptions=check_layering.INCLUDE_EXCEPTIONS,
    )
    violations = layer_violations(findings)
    assert len(violations) == 1, violations
    assert 'ui/new_panel.h' in violations[0]


def test_exception_pair_is_bound_to_its_source_file(tmp_path):
    findings = scan(
        tmp_path,
        pair_files([('acquisition/control/acquisition_waveform_test.cpp', 'ui/device_workspace.h')]),
        include_exceptions=check_layering.INCLUDE_EXCEPTIONS,
    )
    violations = layer_violations(findings)
    assert len(violations) == 1, violations
    assert 'acquisition_waveform_test.cpp' in violations[0]

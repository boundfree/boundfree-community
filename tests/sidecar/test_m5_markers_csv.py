"""M5 export_markers_csv 边车协议往返与错误契约测试。

被测对象: ``boundfree_sidecar.protocol.handle_request`` 中的
``export_markers_csv`` 方法 (M5 计划 docs/plans/m5-playback-annotations.md)。
该方法接受 ``path`` (数据 CSV 路径) 与 ``markers`` (对象数组), 把 markers 写为
``{stem}_markers.csv`` (表头 ``time_s,label``), 复用 ``CsvReader._load_events``
既有侧车读取路径, 由 ``open`` 的 ``events`` 字段回读。

本文件只通过公共 ``handle_request`` 入口驱动协议, 不读取产品源文本, 不修改
产品源 / 构建 / 黄金文件, 不运行全量测试。防御的可观测契约:

1. 往返恒等: 导出一组合法 markers 后, 重新 ``open`` 同一数据 CSV, ``events``
   字段的时间与标签与原输入逐项一致 (顺序与数值)。该契约锁死了「导出格式 ==
   CsvReader._load_events 期望的 ``time_s,label`` 表头 + 文件顺序」跨模块约定;
   任意一侧改表头/列名/序列化方向/排序都会变红。
2. 导出副作用: 导出后磁盘上确实出现 ``{stem}_markers.csv`` 文件 (而非只在
   内存里返回 ok), 证明协议真的落盘; 成功响应携带 ``path`` 与 ``marker_count``。
3. 非法 markers 参数返回结构化错误信封: markers 非列表、元素非对象、元素
   ``time`` 缺失/非数/为负/为 bool、``label`` 缺失/非字符串, 均以
   ``protocol.invalid_params`` 返回, params 含可定位的 ``field=markers[i]``
   与 ``reason``; 不抛裸异常, 不写半截文件。
4. 缺 ``path`` 或缺 ``markers`` 参数返回 ``protocol.invalid_params``, field
   指向缺失字段, 与 ``open(path)`` / ``get_slice(handle)`` 既有校验风格对齐。

实现未到位时 (``export_markers_csv`` 仍为 ``protocol.unknown_method``), 所有
测试 skip, 避免在 TDD 阶段误红。实现到位后应直接 pytest 通过。
"""

import csv
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _open(path: Path, request_id: str = "open"):
    from boundfree_sidecar.protocol import handle_request

    return handle_request(
        {"id": request_id, "method": "open", "params": {"path": str(path)}}
    )


def _export_markers_csv(path: Path, markers: object, request_id: str = "export"):
    from boundfree_sidecar.protocol import handle_request

    return handle_request(
        {
            "id": request_id,
            "method": "export_markers_csv",
            "params": {"path": str(path), "markers": markers},
        }
    )


def _write_data_csv(path: Path) -> None:
    # 与 tests/fixtures/generated/simple_eeg.csv 同构: 100Hz, 2 通道, 10 样本,
    # duration=0.1s。复用临时目录, 不依赖新增 fixture。
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time_s", "ch1", "ch2"])
        for i in range(10):
            writer.writerow([f"{i * 0.01:.2f}", float(i), float(i) + 10.0])


def _method_is_unknown(response: dict) -> bool:
    return (
        response.get("ok") is False
        and response.get("error", {}).get("code") == "protocol.unknown_method"
        and response["error"].get("params", {}).get("method") == "export_markers_csv"
    )


@pytest.fixture()
def data_csv(tmp_path: Path) -> Path:
    path = tmp_path / "session.csv"
    _write_data_csv(path)
    return path


def test_export_markers_csv_round_trips_through_open_events(data_csv: Path):
    """导出的 markers 经 ``open`` 回读后时间与标签逐项一致 (输入顺序)。

    契约: export_markers_csv 把 markers 按 ``time_s,label`` 表头写入
    ``{stem}_markers.csv``, 行序保持输入顺序 (无排序); 随后对同一数据 CSV 调
    ``open`` 必须在 ``events`` 字段返回与输入逐项一致的 {time, label}。
    任一侧改了表头/列名/序列化方向/引入排序, 往返断裂, 测试变红。

    选一组非单调时间 (0.07 在 0.05 之前) 以捕获「实现偷偷排序」的回归:
    若导出或回读任一方按 time 排序, 顺序将与输入不一致。
    """
    markers = [
        {"time": 0.02, "label": "stim_a"},
        {"time": 0.07, "label": "stim_b"},
        {"time": 0.05, "label": "mid"},
    ]
    export_response = _export_markers_csv(data_csv, markers, request_id="export")

    if _method_is_unknown(export_response):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")

    assert export_response["ok"] is True, export_response
    result = export_response["result"]
    assert result["marker_count"] == len(markers), result
    assert result["path"].endswith("_markers.csv"), result

    sidecar_path = Path(result["path"])
    assert sidecar_path == data_csv.with_name(data_csv.stem + "_markers.csv"), sidecar_path
    assert sidecar_path.exists(), "export_markers_csv 未在磁盘写出 _markers.csv 侧车"

    # 关键断言: 经 open 回读的 events 与原 markers 在 time/label 上逐项一致
    # (保持输入顺序, 非排序)。
    reopened = _open(data_csv, request_id="open-after-export")
    assert reopened["ok"] is True, reopened
    events = reopened["result"]["events"]

    assert events == [
        {"time": m["time"], "label": m["label"]} for m in markers
    ], f"round-trip mismatch: got {events!r}"


def test_export_markers_csv_overwrites_existing_sidecar(data_csv: Path):
    """重复导出覆盖旧侧车, 不残留旧事件。

    契约: 二次 export_markers_csv 必须以 ``w`` 模式覆盖 ``{stem}_markers.csv``,
    而非追加; 否则回读会得到旧+新事件并集, 与本次输入不一致。此回归在
    「实现忘了 truncate / 以 append 模式打开」时变红。
    """
    first = _export_markers_csv(
        data_csv,
        [{"time": 0.01, "label": "old"}, {"time": 0.09, "label": "old2"}],
        request_id="export-first",
    )
    if _method_is_unknown(first):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")
    assert first["ok"] is True, first

    second = _export_markers_csv(
        data_csv,
        [{"time": 0.05, "label": "only"}],
        request_id="export-second",
    )
    assert second["ok"] is True, second

    reopened = _open(data_csv, request_id="open-after-overwrite")
    assert reopened["ok"] is True
    events = reopened["result"]["events"]

    assert events == [{"time": 0.05, "label": "only"}], (
        f"二次导出未覆盖旧侧车, 回读得到残留事件: {events!r}"
    )


def test_export_markers_csv_empty_markers_writes_header_only(data_csv: Path):
    """空 markers 列表合法: 写出仅含表头的侧车, open 回读得到空 events。

    契约: 空 markers 不是错误 (区别于 markers 非列表); 导出写出表头行,
    CsvReader._load_events 读到零数据行, 返回 events=[]。此契约锁定「空列表
    合法, 且不污染既有 events」边界。
    """
    export_response = _export_markers_csv(data_csv, [], request_id="export-empty")

    if _method_is_unknown(export_response):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")

    assert export_response["ok"] is True, export_response
    assert export_response["result"]["marker_count"] == 0

    sidecar_path = data_csv.with_name(data_csv.stem + "_markers.csv")
    assert sidecar_path.exists(), sidecar_path
    with sidecar_path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.reader(f))
    assert rows == [["time_s", "label"]], rows

    reopened = _open(data_csv, request_id="open-after-empty-export")
    assert reopened["ok"] is True
    assert reopened["result"]["events"] == []


@pytest.mark.parametrize(
    "markers, expected_field, reason_fragment",
    [
        ("not-a-list", "markers", "required array"),
        (123, "markers", "required array"),
        ({"a": 1}, "markers", "required array"),
        ([{"label": "no-time"}], "markers[0]", "required non-negative time and label"),
        ([{"time": "bad", "label": "x"}], "markers[0]", "required non-negative time and label"),
        ([{"time": -0.5, "label": "neg"}], "markers[0]", "required non-negative time and label"),
        ([{"time": 0.1, "label": 123}], "markers[0]", "required non-negative time and label"),
        ([{"time": 0.1}], "markers[0]", "required non-negative time and label"),
        (["scalar"], "markers[0]", "required object"),
        ([{"time": 0.1, "label": "ok"}, 42], "markers[1]", "required object"),
        ([{"time": True, "label": "bool-time"}], "markers[0]", "required non-negative time and label"),
    ],
    ids=[
        "markers-string",
        "markers-number",
        "markers-object",
        "element-missing-time",
        "time-not-number",
        "time-negative",
        "label-not-string",
        "element-missing-label",
        "element-scalar",
        "second-element-scalar",
        "time-boolean",
    ],
)
def test_export_markers_csv_rejects_invalid_markers_with_structured_error(
    data_csv: Path, markers, expected_field: str, reason_fragment: str
):
    """非法 markers 参数返回 ``protocol.invalid_params`` 结构化错误。

    契约: export_markers_csv 必须校验 markers 形状 —— 必须是列表; 每元素必须是
    含 ``time`` (非负数, 拒绝 bool) 与 ``label`` (非空字符串) 的对象。违规时返回
    ``protocol.invalid_params``, params.field 精确定位到 ``markers`` 或
    ``markers[i]``, params.reason 含可读子串; 不抛裸异常, 不写半截文件。

    注意: 实现对 ``time``/``label`` 校验失败使用统一的
    ``required non-negative time and label`` reason, 不区分具体缺哪个字段;
    本测试锁定该统一 reason 文本, 以捕获实现改写为更细粒度或更粗粒度 reason
    导致下游本地化失配的回归。
    """
    response = _export_markers_csv(data_csv, markers, request_id="export-invalid")

    if _method_is_unknown(response):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")

    assert response["ok"] is False, response
    error = response["error"]
    assert error["code"] == "protocol.invalid_params", error
    assert error["retryable"] is False, error
    assert error["trace_id"], error
    params = error["params"]
    assert params["field"] == expected_field, params
    assert reason_fragment in params["reason"], params


def test_export_markers_csv_requires_path_param(tmp_path: Path):
    """缺 path 返回 ``protocol.invalid_params`` field=path, 与 open(path) 对齐。

    契约: path 为必填字符串; 缺失或非字符串时返回
    ``protocol.invalid_params`` {field: path, reason: required string},
    与 open(path) 既有校验风格一致。
    """
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(
        {
            "id": "export-no-path",
            "method": "export_markers_csv",
            "params": {"markers": []},
        }
    )

    if _method_is_unknown(response):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")

    assert response["ok"] is False, response
    assert response["error"]["code"] == "protocol.invalid_params", response["error"]
    assert response["error"]["params"] == {
        "field": "path",
        "reason": "required string",
    }, response["error"]


def test_export_markers_csv_requires_markers_param(data_csv: Path):
    """缺 markers 返回 ``protocol.invalid_params`` field=markers reason=required array。

    契约: markers 为必填数组; 缺失时返回
    ``protocol.invalid_params`` {field: markers, reason: required array}。
    """
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(
        {
            "id": "export-no-markers",
            "method": "export_markers_csv",
            "params": {"path": str(data_csv)},
        }
    )

    if _method_is_unknown(response):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")

    assert response["ok"] is False, response
    assert response["error"]["code"] == "protocol.invalid_params", response["error"]
    assert response["error"]["params"] == {
        "field": "markers",
        "reason": "required array",
    }, response["error"]


def test_export_markers_csv_missing_file_dir_is_reported_as_open_failed(tmp_path: Path):
    """path 指向不存在目录时, 写侧车失败以 ``reader.open_failed`` 结构化返回。

    契约: ``{stem}_markers.csv`` 的父目录不存在时, csv.open 抛 OSError, 协议捕获
    并以 ``reader.open_failed`` {path, reason} 返回, 不抛裸异常。此契约锁定
    「写失败也走结构化错误信封」与 open 失败路径一致。
    """
    missing_dir = tmp_path / "no-such-dir" / "session.csv"
    response = _export_markers_csv(
        missing_dir,
        [{"time": 0.1, "label": "x"}],
        request_id="export-missing-dir",
    )

    if _method_is_unknown(response):
        pytest.skip("export_markers_csv 尚未实现 (protocol.unknown_method)")

    assert response["ok"] is False, response
    assert response["error"]["code"] == "reader.open_failed", response["error"]
    assert response["error"]["params"]["path"].endswith("_markers.csv"), response["error"]
    assert response["error"]["params"]["reason"], response["error"]

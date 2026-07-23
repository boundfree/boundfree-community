"""Intan RHD/RHS 高采样率文件读取器。

复用 ``neo``（可选依赖）读取 Intan RHD/RHS 二进制文件，懒加载：
``metadata()`` 只解析头部，``get_slice()`` 按需读取片段。

neo 不可用时抛出 ``OptionalDependencyMissing``，由协议层映射为
``reader.optional_dependency_missing`` error_code。本任务只实现 neo 路径
+ 明确的 error_code 回退；纯 struct 解析器留待后续移植。
"""
from __future__ import annotations

from pathlib import Path

import numpy as np

from .base import FileMetadata, Reader, SliceData
from .errors import OptionalDependencyMissing


def _import_neo():
    """惰性导入 neo；失败时抛出带 dependency 信息的结构化异常。"""
    try:
        import neo  # noqa: F401
    except ImportError as exc:  # 由测试 monkeypatch 覆盖
        raise OptionalDependencyMissing("neo", str(exc)) from exc
    return neo


# neo Intan amplifier 通道单位 -> uV 缩放
_UNIT_SCALE_TO_UV = {"uv": 1.0, "µv": 1.0, "mv": 1e3, "v": 1e6}


class IntanReader(Reader):
    """通过 neo 读取 Intan ``.rhd`` / ``.rhs`` 文件。

    neo 的 IntanIO 覆盖 RHD 与 RHS 两种变体（按 magic number 自动识别）。
    这里只导出放大器通道（amplifier）波形——Spike 线关注的高采样率放电数据。
    """

    SUPPORTED_SUFFIXES = {".rhd", ".rhs"}

    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._reader = None
        self._block = None
        self._metadata: FileMetadata | None = None
        self._data: np.ndarray | None = None

    @property
    def format_name(self) -> str:
        suffix = self.path.suffix.lower()
        if suffix == ".rhs":
            return "Intan RHS"
        if suffix == ".rhd":
            return "Intan RHD"
        return "Intan"

    # -- neo lazy handles ------------------------------------------------

    def _load_block(self):
        """惰性打开 neo reader 并解析第一个 block。"""
        if self._block is not None:
            return self._block
        neo = _import_neo()
        self._reader = neo.io.IntanIO(filename=str(self.path))
        # read(lazy=True) 仅解析 header，不读波形数据。
        self._block = self._reader.read(lazy=True)[0]
        return self._block

    def _amplifier_signals(self, block) -> list:
        seg = block.segments[0]
        return list(seg.analogsignals)

    @staticmethod
    def _stack_signals(signals: list) -> tuple[np.ndarray, list[str], float, str]:
        """把 neo AnalogSignal 列表堆叠为 [n_channels, n_samples] double 矩阵。

        返回 (data, ch_names, fs, unit)。单位统一为 uV。
        """
        if not signals:
            raise ValueError("Intan file has no amplifier channels")

        fs = float(signals[0].sampling_rate.magnitude)
        unit = str(signals[0].units).lower()
        scale = _UNIT_SCALE_TO_UV.get(unit, 1.0)

        arrays: list[np.ndarray] = []
        ch_names: list[str] = []
        for sig in signals:
            arr = np.asarray(sig.magnitude, dtype=np.float64) * scale
            # AnalogSignal 形状为 (n_samples, n_channels)
            if arr.ndim == 1:
                arr = arr.reshape(-1, 1)
            for col in range(arr.shape[1]):
                arrays.append(arr[:, col])
                ch_names.append(str(sig.name) if sig.name else f"Ch{len(ch_names) + 1}")

        data = np.vstack(arrays) if arrays else np.empty((0, 0), dtype=np.float64)
        return data, ch_names, fs, "uV"

    def _load_data(self) -> np.ndarray:
        """惰性读取并缓存完整 amplifier 波形矩阵。"""
        if self._data is not None:
            return self._data
        block = self._load_block()
        # 需要真实数据时重新以 non-lazy 方式读取
        if self._reader is not None:
            self._block = self._reader.read(lazy=False)[0]
        signals = self._amplifier_signals(self._block)
        data, _, _, _ = self._stack_signals(signals)
        self._data = data
        return data

    # -- Reader 契约 -----------------------------------------------------

    def metadata(self) -> FileMetadata:
        if self._metadata is not None:
            return self._metadata
        block = self._load_block()
        signals = self._amplifier_signals(block)
        # lazy 模式下 magnitude 仍可获取维度/采样率/通道名，但样本数据可能为占位。
        # 为得到 n_samples，这里读取真实数据矩阵。
        data, ch_names, fs, unit = self._stack_signals(signals)
        n_channels, n_samples = data.shape
        self._data = data
        self._metadata = FileMetadata(
            fs=fs,
            n_channels=n_channels,
            n_samples=n_samples,
            duration=n_samples / fs if fs > 0 else 0.0,
            ch_names=ch_names,
            unit=unit,
            format_name=self.format_name,
            events=[],
        )
        return self._metadata

    def get_slice(self, t0: float, t1: float) -> SliceData:
        meta = self.metadata()
        data = self._load_data()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        if start >= stop:
            sliced = np.empty((data.shape[0], 0), dtype=np.float64)
        else:
            sliced = data[:, start:stop]
        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit=meta.unit,
            data=sliced.astype(float).tolist(),
        )

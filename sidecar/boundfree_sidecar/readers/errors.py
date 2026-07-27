"""读取器层共享异常。

协议层（protocol）捕获 ``OptionalDependencyMissing`` 并映射到
``reader.optional_dependency_missing`` error_code，而不是让上层看到裸异常。
"""


class OptionalDependencyMissing(RuntimeError):
    """某个可选依赖（如 neo）未安装，导致读取器无法工作。

    Attributes:
        dependency: 缺失的依赖名（例如 ``"neo"``）。
        reason: 额外说明（通常是原始 import 错误信息）。
    """

    def __init__(self, dependency: str, reason: str = "") -> None:
        self.dependency = dependency
        self.reason = reason
        message = f"optional dependency '{dependency}' is missing"
        if reason:
            message = f"{message}: {reason}"
        super().__init__(message)


class CsvSchemaError(RuntimeError):
    """CSV 内容不符合连续信号数据 schema。"""

    def __init__(self, code: str, params: dict[str, object] | None = None) -> None:
        self.code = code
        self.params = params or {}
        super().__init__(code)

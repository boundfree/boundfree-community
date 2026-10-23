"""Structured errors without loading numerical/format dependencies at startup."""


class AnalysisError(ValueError):
    def __init__(self, code, **params):
        super().__init__(code)
        self.code, self.params = code, params


def fail(code, **params):
    raise AnalysisError("analysis." + code, **params)

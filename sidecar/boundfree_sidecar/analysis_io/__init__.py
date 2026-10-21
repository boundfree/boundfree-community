"""Dedicated analysis I/O; format dependencies load only on analysis requests."""
from .errors import AnalysisError

_source_registry = None
_bundle_registry = None


def source_registry():
    global _source_registry
    if _source_registry is None:
        from .sources import SourceRegistry
        _source_registry = SourceRegistry()
    return _source_registry


def bundle_registry():
    global _bundle_registry
    if _bundle_registry is None:
        from .bundle import BundleRegistry
        _bundle_registry = BundleRegistry()
    return _bundle_registry


def handle_analysis(method, params, payload=None):
    if method.startswith("analysis_source_"):
        registry = source_registry()
        return {"analysis_source_open": registry.open, "analysis_source_read": registry.read,
                "analysis_source_close": registry.close}[method](params)
    registry = bundle_registry()
    if method == "analysis_bundle_write":
        return registry.write(params, payload)
    return {"analysis_bundle_publish": registry.publish, "analysis_bundle_discard": registry.discard}[method](params)


def cleanup():
    if _source_registry is not None:
        _source_registry.sources.clear()
    if _bundle_registry is not None:
        _bundle_registry.cleanup()

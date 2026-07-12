import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_collect_m2_format_evidence_dry_run_reports_skips_as_non_evidence():
    result = subprocess.run(
        ["python3", "scripts/collect_m2_format_evidence.py", "--dry-run"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )

    data = json.loads(result.stdout)

    assert data["CSV"]["status"] == "accepted"
    assert data["HDF5"]["status"] in {"accepted", "dependency-skipped"}
    assert data["EDF"]["status"] in {"accepted", "dependency-skipped", "sample-skipped"}
    assert data["HDF5"]["skip_is_acceptance_evidence"] is False
    assert data["EDF"]["skip_is_acceptance_evidence"] is False

import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
I18N_DIR = ROOT / "native" / "i18n"


def test_error_reader_open_failed_key_is_present_in_all_locales():
    for path in sorted(I18N_DIR.glob("bf_app_*.ts")):
        root = ET.parse(path).getroot()
        keys = {message.findtext("source") for message in root.findall(".//message")}
        assert "error.reader.open_failed" in keys, path.name


def test_file_open_status_keys_are_present_in_all_locales():
    required = {"app.status.file_opening", "app.status.file_opened", "app.status.file_window"}
    for path in sorted(I18N_DIR.glob("bf_app_*.ts")):
        root = ET.parse(path).getroot()
        keys = {message.findtext("source") for message in root.findall(".//message")}
        assert required <= keys, path.name


def test_i18n_static_check_passes():
    subprocess.run(
        [sys.executable, "scripts/check_i18n.py"],
        cwd=ROOT,
        text=True,
        check=True,
    )

#!/usr/bin/env python3
import importlib.util
import json
import os
import tempfile
import time
import zipfile
from io import BytesIO
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location(
    "voice_manager", ROOT / "src/speakup-cerence-voice-manager.py"
)
manager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manager)

VOICE = {
    "voice_name": "Alpha", "language": "A", "lang_code": "AAA",
    "gender": "Female", "voice_type": "embedded-compact", "quality": "Lowest",
    "package_name": "a.zip", "package_hash": "0" * 40,
    "size_in_bytes": 10, "download_url": "https://example.invalid/a.zip",
}
DATA = json.dumps([VOICE]).encode()


class Response:
    content = DATA

    def __enter__(self): return self
    def __exit__(self, *args): pass
    def raise_for_status(self): pass


with tempfile.TemporaryDirectory() as temporary:
    cache = Path(temporary) / "voices.json"
    os.environ["SPEAKUP_CERENCE_CATALOG"] = str(cache)
    cache.write_bytes(DATA)

    def no_request(*args, **kwargs):
        raise AssertionError("fresh catalogue was downloaded")

    manager.requests.get = no_request
    assert manager.load_catalog()[0]["voice_name"] == "Alpha"

    os.utime(cache, (time.time() - 90000, time.time() - 90000))
    seen = {}

    def request(url, **kwargs):
        seen["url"] = url
        seen["headers"] = kwargs["headers"]
        return Response()

    manager.requests.get = request
    assert manager.load_catalog()[0]["voice_name"] == "Alpha"
    assert seen["url"] == manager.CATALOG_URL
    assert seen["headers"] == {"User-Agent": None}
    assert time.time() - cache.stat().st_mtime < 5

    os.utime(cache, (time.time() - 90000, time.time() - 90000))

    def failed_request(*args, **kwargs):
        raise manager.requests.ConnectionError("offline")

    manager.requests.get = failed_request
    assert manager.load_catalog()[0]["voice_name"] == "Alpha"

    package_buffer = BytesIO()
    with zipfile.ZipFile(package_buffer, "w") as package:
        package.writestr("aaa/speech/components/a.dat", b"data")
        package.writestr("aaa/speech/ve/a.hdr", b"header")
    package_data = package_buffer.getvalue()
    VOICE["package_hash"] = __import__("hashlib").sha1(package_data).hexdigest()

    class PackageResponse:
        def __enter__(self): return self
        def __exit__(self, *args): pass
        def raise_for_status(self): pass
        def iter_content(self, size): return [package_data]

    def package_request(url, **kwargs):
        assert kwargs["headers"] == {"User-Agent": None}
        return PackageResponse()

    manager.requests.get = package_request
    os.environ["XDG_CACHE_HOME"] = str(Path(temporary) / "cache")
    os.environ["XDG_DATA_HOME"] = str(Path(temporary) / "data")
    args = SimpleNamespace(lang=None, quality=None, get_voice="Alpha")
    assert manager.get_voice([VOICE], args) == 0
    assert (manager.voice_store() / "aaa/speech/components/a.dat").read_bytes() == b"data"

print("voice manager cache and install tests passed")

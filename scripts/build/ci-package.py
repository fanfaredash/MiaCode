#!/usr/bin/env python3
"""Platform package identity, static verification and Actions measurements."""

import hashlib
import json
import os
from datetime import datetime, timedelta, timezone
from pathlib import Path
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request


DIST = Path("dist")
PLATFORM = os.environ.get("CI_PLATFORM", "")
# Compiler/build cache prefix follows files that change compile or package
# layout. Verification scripts stay out of `source` so a finished package can
# be reused; they must not rotate this prefix.
WINDOWS_BUILD_RECIPE = (
    "scripts/build/build-win.ps1",
    "scripts/build/package-win.ps1",
    "scripts/build/provision-qt.ps1",
    "scripts/build/windows-toolchain.psd1",
)
MACOS_BUILD_RECIPE = (
    "scripts/build/build-macos-ci.sh",
    "scripts/build/package-mac.sh",
    "scripts/build/thin-macos-app.sh",
)
# These change CI checks and job text, not the 7z payload.
SOURCE_EXCLUDE = (
    "scripts/build/ci-package.py",
    "scripts/build/verify-win-package.ps1",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tree(*paths, exclude=()):
    # Git object IDs cover LFS pointers and submodule commits before downloads.
    # Generated SDKs/build outputs never enter the source identity.
    data = subprocess.check_output(["git", "ls-tree", "-r", "-z", "HEAD", "--", *paths])
    if not exclude:
        return data
    skipped = set(exclude)
    kept = []
    for entry in data.split(b"\0"):
        if not entry:
            continue
        path = entry.split(b"\t", 1)[1].decode()
        if path in skipped:
            continue
        kept.append(entry)
    return b"\0".join(kept) + b"\0" if kept else b""


def output(**values):
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
        for key, value in values.items():
            stream.write(f"{key}={value}\n")


def summary(message):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
        stream.write(message + "\n")


def inputs():
    source = digest(tree("CMakeLists.txt", "CMakePresets.json", "cmake", "src",
                         "resources", "assets", "translations", "templates",
                         "third_party", "scripts", "licenses", "LICENSE",
                         "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md",
                         ".github/workflows/package.yml", ".gitmodules", ".gitattributes",
                         exclude=SOURCE_EXCLUDE))
    recipe = digest(tree("scripts/ffmpeg", "scripts/build/windows-toolchain.psd1"))
    if PLATFORM.startswith("windows-"):
        build_recipe = digest(tree(*WINDOWS_BUILD_RECIPE))
    elif PLATFORM == "macos-arm64":
        build_recipe = digest(tree(*MACOS_BUILD_RECIPE))
    else:
        build_recipe = digest(tree("scripts/build", ".github/workflows/package.yml"))
    toolchain = digest((recipe + build_recipe + os.environ.get("ImageOS", "") +
                        os.environ.get("ImageVersion", "")).encode())
    output(source=source, recipe=recipe, toolchain=toolchain)


def archive():
    archives = list(DIST.glob("*.7z"))
    if len(archives) != 1:
        raise RuntimeError(f"Expected one archive, found {archives}")
    return archives[0]


def execute(*args):
    subprocess.run([str(arg) for arg in args], check=True, timeout=120)


def verify_caches():
    prefix = "platform-v1-"
    expected = {
        f"{prefix}qt-macos-arm64-6.11.1",
        f"{prefix}media-macos-arm64-ffmpeg8.1.2-{os.environ['CI_RECIPE']}",
        f"{prefix}compiler-macos-arm64-{os.environ['CI_TOOLCHAIN']}-{os.environ['CI_SOURCE']}",
        f"{prefix}build-macos-arm64-{os.environ['CI_TOOLCHAIN']}-{os.environ['CI_SOURCE']}",
    }
    page = 1
    found = set()
    while True:
        query = urllib.parse.urlencode(dict(ref=os.environ["GITHUB_REF"], key=prefix,
                                           per_page=100, page=page))
        url = (f"{os.environ['GITHUB_API_URL']}/repos/{os.environ['GITHUB_REPOSITORY']}"
               f"/actions/caches?{query}")
        request = urllib.request.Request(url, headers={
            "Authorization": f"Bearer {os.environ['GH_TOKEN']}",
            "Accept": "application/vnd.github+json",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            caches = json.load(response)["actions_caches"]
        found.update(c["key"] for c in caches if c["size_in_bytes"] > 0)
        if expected <= found or len(caches) < 100:
            break
        page += 1
    if expected - found:
        raise RuntimeError(f"Saved caches missing: {sorted(expected - found)}")


def verify():
    package = archive()
    folder = package.with_suffix("")
    checks = []
    if PLATFORM.startswith("windows-"):
        app = folder / "app"
        ffmpeg = app / "ffmpeg/ffmpeg.exe"
        machine = 0x8664 if PLATFORM == "windows-x64" else 0xAA64
        for binary in [folder / "MiaCode.exe", app / "MiaCode.exe", ffmpeg,
                       *app.glob("avcodec-*.dll")]:
            with binary.open("rb") as stream:
                stream.seek(0x3C)
                offset = struct.unpack("<I", stream.read(4))[0]
                stream.seek(offset)
                signature, actual = struct.unpack("<4sH", stream.read(6))
            if signature != b"PE\0\0" or actual != machine:
                raise RuntimeError(f"Unexpected PE architecture: {binary}")
        checks.extend(["PE 架构", "Windows 包结构与依赖检查"])
    else:
        app = folder / "MiaCode.app/Contents/MacOS"
        ffmpeg = app / "ffmpeg/ffmpeg"
        execute("lipo", app / "MiaCode", "-verify_arch", "arm64")
        checks.extend(["arm64 架构", "macOS 包依赖与部署版本检查"])
    version = subprocess.check_output([str(ffmpeg), "-version"], text=True).splitlines()[0]
    expected = "7.1" if PLATFORM == "windows-x64" else "8.1"
    if expected not in version:
        raise RuntimeError(f"Unexpected FFmpeg version: {version}")
    media = DIST / "validation/media.mp4"
    media.parent.mkdir(parents=True, exist_ok=True)
    execute(ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "color=c=black:s=64x64:r=10:d=0.3",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=0.3",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", media)
    execute(ffmpeg, "-hide_banner", "-loglevel", "error", "-i", media, "-f", "null", "-")
    checks.extend(["FFmpeg 版本", "H.264/AAC 编码与解码"])
    record = dict(platform=PLATFORM, source=os.environ["CI_SOURCE"], archive=package.name,
                  bytes=package.stat().st_size, sha256=digest(package.read_bytes()),
                  ffmpeg=version, checks=checks, verified_run=os.environ["GITHUB_RUN_ID"])
    (DIST / "verification.json").write_text(json.dumps(record, ensure_ascii=False, indent=2),
                                           encoding="utf-8")


def check():
    package = archive()
    record = json.loads((DIST / "verification.json").read_text(encoding="utf-8"))
    if (record["platform"] != PLATFORM or record["source"] != os.environ["CI_SOURCE"]
            or record["archive"] != package.name
            or record["sha256"] != digest(package.read_bytes())
            or record["bytes"] != package.stat().st_size):
        raise RuntimeError("Package identity mismatch")
    if PLATFORM == "windows-x64" and package.stat().st_size >= 80 * 1048576:
        raise RuntimeError("Windows x64 archive must be below 80 MiB")
    output(name=package.name)


def format_elapsed(elapsed):
    total = max(0, int(round(elapsed)))
    hours, rem = divmod(total, 3600)
    minutes, seconds = divmod(rem, 60)
    if hours:
        return f"{hours} 小时 {minutes} 分"
    if minutes and seconds:
        return f"{minutes} 分 {seconds:02d} 秒"
    if minutes:
        return f"{minutes} 分钟"
    return f"{seconds} 秒"


def format_trigger():
    raw = os.environ.get("CI_TRIGGERED_AT", "").strip()
    if raw:
        moment = datetime.fromisoformat(raw.replace("Z", "+00:00"))
    else:
        moment = datetime.fromtimestamp(int(os.environ["CI_STARTED_AT"]), tz=timezone.utc)
    local = moment.astimezone(timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M")
    event = {"push": "推送", "workflow_dispatch": "手动"}.get(os.environ.get("CI_EVENT", ""))
    return f"{local}（{event}）" if event else local


def report():
    record = json.loads((DIST / "verification.json").read_text(encoding="utf-8"))
    elapsed = time.time() - int(os.environ["CI_STARTED_AT"])
    summary("\n".join((
        f"耗时 {format_elapsed(elapsed)}",
        f"大小 {record['bytes'] / 1048576:.2f} MiB",
        f"触发 {format_trigger()}",
    )))
    if os.environ.get("CI_PACKAGE_HIT") == "true" and elapsed >= 60:
        raise RuntimeError(f"Cached package delivery exceeded 60 seconds: {elapsed:.1f}s")


if __name__ == "__main__":
    commands = {"inputs": inputs, "verify": verify, "check": check, "report": report,
                "verify-caches": verify_caches}
    commands[sys.argv[1]]()

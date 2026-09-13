#!/usr/bin/env python3
"""Platform package identity, smoke verification and Actions measurements."""

import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time


DIST = Path("dist")
PLATFORM = os.environ.get("CI_PLATFORM", "")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tree(*paths):
    # Git object IDs cover LFS pointers and submodule commits before downloads.
    # Generated SDKs/build outputs never enter the source identity.
    return subprocess.check_output(["git", "ls-tree", "-r", "-z", "HEAD", "--", *paths])


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
                         ".github/workflows/package.yml", ".gitmodules", ".gitattributes"))
    recipe = digest(tree("scripts/ffmpeg", "scripts/build/windows-toolchain.psd1"))
    build_recipe = digest(tree("scripts/build", ".github/workflows/package.yml"))
    if PLATFORM == "macos-arm64":
        build_recipe = digest(tree("scripts/build/build-macos-ci.sh",
                                   "scripts/build/package-mac.sh",
                                   "scripts/build/thin-macos-app.sh"))
    toolchain = digest((recipe + build_recipe + os.environ.get("ImageOS", "") +
                        os.environ.get("ImageVersion", "")).encode())
    output(source=source, recipe=recipe, toolchain=toolchain)
    summary(f"平台：{PLATFORM}；运行前包体估算：{os.environ['CI_ESTIMATE']}。\n"
            f"输入：`{source}`。估算依据为历史成功构建的 7z 体积。")


def archive():
    archives = list(DIST.glob("*.7z"))
    if len(archives) != 1:
        raise RuntimeError(f"Expected one archive, found {archives}")
    return archives[0]


def execute(*args):
    subprocess.run([str(arg) for arg in args], check=True, timeout=120)


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
        checks.extend(["PE 架构", "Windows 包结构与依赖检查", "应用与启动器启动"])
    else:
        app = folder / "MiaCode.app/Contents/MacOS"
        ffmpeg = app / "ffmpeg/ffmpeg"
        execute("lipo", app / "MiaCode", "-verify_arch", "arm64")
        log_dir = (DIST / "validation").resolve()
        log_dir.mkdir(parents=True, exist_ok=True)
        with (log_dir / "launch.log").open("w") as stream:
            process = subprocess.Popen([str((app / "MiaCode").resolve()), "--debug"],
                                       env={**os.environ, "MIACODE_LOG_DIR": str(log_dir)},
                                       stdout=stream, stderr=subprocess.STDOUT)
            try:
                try:
                    code = process.wait(timeout=15)
                    raise RuntimeError(f"Application exited during launch: {code}")
                except subprocess.TimeoutExpired:
                    pass
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=15)
        logs = "\n".join(p.read_text(encoding="utf-8", errors="replace")
                         for p in log_dir.glob("*.log"))
        if not re.search(r"action=start_ok|quick_shell/backend", logs):
            raise RuntimeError("Application readiness marker missing")
        if re.search(r"load_failed|qml_object_creation_failed", logs):
            raise RuntimeError("QML application startup failed")
        checks.extend(["arm64 架构", "macOS 包依赖与部署版本检查", "应用启动"])
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


def report():
    record = json.loads((DIST / "verification.json").read_text(encoding="utf-8"))
    elapsed = time.time() - int(os.environ["CI_STARTED_AT"])
    hit = os.environ["CI_PACKAGE_HIT"] == "true"
    summary(f"\n出包耗时：{elapsed:.1f} 秒（作业计时步骤至上传完成）。\n"
            f"包体：{record['bytes'] / 1048576:.2f} MiB（{record['bytes']} 字节）。\n"
            f"SHA256：`{record['sha256']}`。\n\n|缓存|结果|\n|---|---|")
    for label, name in [("成品", "PACKAGE"), ("Qt", "QT"), ("FFmpeg", "MEDIA"),
                        ("编译器输出", "COMPILER"), ("构建目录", "BUILD")]:
        value = os.environ.get(f"CI_{name}_HIT", "")
        result = "命中" if value == "true" else ("跳过：成品命中" if hit else "未精确命中")
        summary(f"|{label}|{result}|")
    summary(f"\n功能检查：{'、'.join(record['checks'])}。\n"
            f"功能验证运行：{record['verified_run']}；本轮校验压缩包 SHA256。")
    if hit and elapsed >= 60:
        raise RuntimeError(f"Cached package delivery exceeded 60 seconds: {elapsed:.1f}s")


if __name__ == "__main__":
    commands = {"inputs": inputs, "verify": verify, "check": check, "report": report}
    commands[sys.argv[1]]()

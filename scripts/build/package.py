#!/usr/bin/env python3
"""Build and package MiaCode for CI and local use."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timedelta, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DIST = ROOT / "dist"
QT_VERSION = "6.11.1"
QT_MODULES = ("qtmultimedia", "qtshadertools", "qtquick3d")
MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce",
    b"\xfe\xed\xfa\xcf",
    b"\xce\xfa\xed\xfe",
    b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe",
    b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xd0\x0d",
    b"\x0d\xd0\xfe\xca",
}
PLATFORM = os.environ.get("CI_PLATFORM", "")
BUILD_DIR = Path(os.environ.get("CI_BUILD_DIR", "build"))
if not BUILD_DIR.is_absolute():
    BUILD_DIR = ROOT / BUILD_DIR


def cpu_jobs() -> str:
    override = os.environ.get("MIACODE_PACKAGE_JOBS", "").strip()
    if override:
        return override
    return str(os.cpu_count() or 1)


def run(args, cwd=None, env=None):
    subprocess.run([str(a) for a in args], cwd=cwd, env=env, check=True)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def github_output(**values):
    path = os.environ.get("GITHUB_OUTPUT")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as stream:
        for key, value in values.items():
            stream.write(f"{key}={value}\n")


def github_summary(text: str):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as stream:
        stream.write(text + "\n")


def git_tree(*paths: str) -> bytes:
    return subprocess.check_output(["git", "ls-tree", "-r", "-z", "HEAD", "--", *paths], cwd=ROOT)


def download(url: str, dest: Path):
    dest.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(url, dest)


def extract_archive(archive: Path, dest: Path):
    dest.mkdir(parents=True, exist_ok=True)
    suffix = archive.suffix.lower()
    if suffix == ".zip":
        shutil.unpack_archive(archive, dest)
        return
    seven = shutil.which("7z") or shutil.which("7za") or shutil.which("7zz")
    if seven:
        run([seven, "x", "-y", f"-o{dest}", archive])
        return
    # py7zr's CLI takes the destination positionally and rejects 7z's -o
    # flag, so use its API instead.
    import py7zr
    with py7zr.SevenZipFile(archive, "r") as stream:
        stream.extractall(path=dest)


def keys():
    source = digest(git_tree(
        "CMakeLists.txt", "CMakePresets.json", "cmake", "src", "resources", "assets",
        "translations", "templates", "third_party", "scripts", "licenses", "LICENSE",
        "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md", ".github/workflows/package.yml",
        ".gitmodules", ".gitattributes",
    ))
    recipe = digest(git_tree("scripts/build/package.py", "scripts/ffmpeg", "scripts/build/windows-toolchain.psd1"))
    toolchain = digest((recipe + os.environ.get("ImageOS", "") + os.environ.get("ImageVersion", "")).encode())
    github_output(source=source, recipe=recipe, toolchain=toolchain)


def qt_component(root: Path, name: str) -> bool:
    return (root / "lib" / "cmake" / name / f"{name}Config.cmake").is_file()


def windows_qt_root() -> Path:
    spec = WINDOWS[PLATFORM]
    return ROOT / ".qt" / QT_VERSION / spec["archdir"]


def macos_qt_root() -> Path:
    return ROOT / ".qt" / QT_VERSION / "macos"


def provision_windows_qt():
    spec = WINDOWS[PLATFORM]
    root = windows_qt_root()
    needed = []
    if not ((root / "bin" / "windeployqt.exe").is_file() and qt_component(root, "Qt6")):
        needed.append("base")
    module_cmake = {
        "qtmultimedia": "Qt6Multimedia",
        "qtshadertools": "Qt6ShaderTools",
        "qtquick3d": "Qt6Quick3D",
    }
    for module in QT_MODULES:
        if not qt_component(root, module_cmake[module]):
            needed.append(module)
    if not needed:
        print(f"Qt {QT_VERSION} present at {root}")
        return
    version_nodots = QT_VERSION.replace(".", "")
    host = spec["host"]
    aqt = spec["aqt"]
    suffix = re.sub(r"^win(64|32)_", "", aqt)
    desktop = f"https://download.qt.io/online/qtsdkrepository/{host}/desktop"
    folders = [
        f"qt6_{version_nodots}/qt6_{version_nodots}",
        f"qt6_{version_nodots}/qt6_{version_nodots}_{suffix}",
    ]
    updates = None
    folder = None
    for candidate in folders:
        url = f"{desktop}/{candidate}/Updates.xml"
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                updates = response.read()
            folder = candidate
            break
        except OSError:
            continue
    if updates is None:
        raise RuntimeError(f"Qt metadata missing for {QT_VERSION} / {aqt}")
    print(f"Qt repository layout: {folder}")
    tree = ET.fromstring(updates)
    packages = []
    if "base" in needed:
        packages.append(f"qt.qt6.{version_nodots}.{aqt}")
    for module in QT_MODULES:
        if module in needed:
            packages.append(f"qt.qt6.{version_nodots}.addons.{module}.{aqt}")
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="miacode-qt-") as temp:
        temp_dir = Path(temp)
        for package in packages:
            archives = []
            for node in tree.iter("PackageUpdate"):
                name = node.findtext("Name")
                if name != package:
                    continue
                raw = node.findtext("DownloadableArchives") or ""
                archives = [item.strip() for item in raw.split(",") if item.strip()]
            if not archives:
                raise RuntimeError(f"Qt package missing: {package}")
            listing = urllib.request.urlopen(f"{desktop}/{folder}/{package}", timeout=30).read().decode("utf-8", "replace")
            hrefs = re.findall(r'href="([^"]+)"', listing)
            for archive in archives:
                href = next((item for item in hrefs if item.endswith(archive) and not item.endswith(".sha1") and not item.endswith(".mirrorlist")), None)
                if href is None:
                    raise RuntimeError(f"Qt archive missing: {archive}")
                local = temp_dir / Path(href).name
                print(f"Downloading {local.name}")
                download(f"{desktop}/{folder}/{package}/{href}", local)
                extract_archive(local, root)
                local.unlink(missing_ok=True)
    print(f"Qt {QT_VERSION} installed at {root}")


def provision_macos_qt():
    root = macos_qt_root()
    deploy = root / "bin" / "macdeployqt"
    python = ROOT / ".venv" / "macos-build" / "bin" / "python"
    if not python.is_file():
        run([sys.executable, "-m", "venv", python.parent.parent])
    env = os.environ.copy()
    env["PIP_DISABLE_PIP_VERSION_CHECK"] = "1"
    env["PIP_NO_INPUT"] = "1"

    def aqt(*modules: str):
        run([python, "-m", "pip", "install", "--upgrade", "aqtinstall==3.3.*", "py7zr==1.0.*"], env=env)
        args = [python, "-m", "aqt", "install-qt", "mac", "desktop", QT_VERSION, "clang_64", "--outputdir", ROOT / ".qt"]
        if modules:
            args += ["--modules", *modules]
        run(args, env=env)

    if not deploy.is_file():
        aqt(*QT_MODULES)
        return
    missing = []
    if not qt_component(root, "Qt6Multimedia"):
        missing.append("qtmultimedia")
    if not qt_component(root, "Qt6ShaderTools"):
        missing.append("qtshadertools")
    if not qt_component(root, "Qt6Quick3D"):
        missing.append("qtquick3d")
    if missing:
        aqt(*missing)
    else:
        print(f"Qt {QT_VERSION} present at {root}")


def find_named(root: Path, name: str) -> Path | None:
    matches = list(root.rglob(name))
    return matches[0] if matches else None


def provision_windows_ffmpeg():
    spec = WINDOWS[PLATFORM]
    arch = spec["arch"]
    dest_dir = ROOT / "third_party" / "ffmpeg" / "windows" / spec["ffmpeg_dir"]
    binary = dest_dir / "ffmpeg.exe"
    if binary.is_file() and binary.stat().st_size > 1024 * 1024:
        print(f"Using existing Windows ffmpeg: {binary}")
    else:
        with tempfile.TemporaryDirectory(prefix="miacode-ffmpeg-") as temp:
            archive = Path(temp) / spec["ffmpeg_archive"]
            extracted = Path(temp) / "extracted"
            print(f"Downloading Windows ffmpeg ({arch})")
            download(spec["ffmpeg_url"], archive)
            extract_archive(archive, extracted)
            found = find_named(extracted, "ffmpeg.exe")
            if found is None:
                raise RuntimeError("ffmpeg.exe missing from archive")
            dest_dir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(found, binary)
            probe = find_named(extracted, "ffprobe.exe")
            if probe is not None:
                shutil.copy2(probe, dest_dir / "ffprobe.exe")
            print(f"Prepared Windows ffmpeg at {binary}")
    if arch == "x64":
        sdk = dest_dir / "dev"
        header = sdk / "include" / "libavcodec" / "avcodec.h"
        dll = sdk / "bin" / "avcodec-61.dll"
        if header.is_file() and dll.is_file():
            print(f"Using existing FFmpeg preview SDK: {sdk}")
            return
        script = ROOT / "scripts" / "ffmpeg" / "trim" / "build-trimmed-ffmpeg.ps1"
        run(["powershell", "-ExecutionPolicy", "Bypass", "-File", script, "-OutputDir", sdk, "-Jobs", JOBS])
        return
    sdk = dest_dir / "dev"
    if (sdk / "include" / "libavcodec").is_dir() and (sdk / "bin" / "avcodec-62.dll").is_file():
        print(f"Using existing FFmpeg preview SDK: {sdk}")
        return
    url = spec["ffmpeg_dev_url"]
    with tempfile.TemporaryDirectory(prefix="miacode-ffmpeg-dev-") as temp:
        archive = Path(temp) / "sdk.zip"
        extracted = Path(temp) / "extracted"
        print("Downloading FFmpeg dev SDK (arm64)")
        download(url, archive)
        extract_archive(archive, extracted)
        include = next(path for path in extracted.rglob("libavcodec") if path.parent.name == "include")
        src = include.parent
        if sdk.exists():
            shutil.rmtree(sdk)
        shutil.copytree(src / "include", sdk / "include")
        shutil.copytree(src / "lib", sdk / "lib")
        (sdk / "bin").mkdir(parents=True, exist_ok=True)
        for name in ("avcodec-62.dll", "avformat-62.dll", "avutil-60.dll", "swresample-6.dll", "swscale-9.dll", "avfilter-11.dll"):
            shutil.copy2(src / "bin" / name, sdk / "bin" / name)
    print(f"Prepared FFmpeg preview SDK at {sdk}")


def provision_macos_ffmpeg():
    media = ROOT / "third_party" / "ffmpeg" / "macos"
    binary = media / "ffmpeg"
    if binary.is_file() and binary.stat().st_size > 0:
        print(f"Using existing macOS ffmpeg: {binary}")
    else:
        url = "https://ffmpeg.martin-riedl.de/download/macos/arm64/1783011502_8.1.2/ffmpeg.zip"
        with tempfile.TemporaryDirectory(prefix="miacode-ffmpeg-") as temp:
            archive = Path(temp) / "ffmpeg.zip"
            extracted = Path(temp) / "extracted"
            print("Downloading macOS ffmpeg")
            download(url, archive)
            extract_archive(archive, extracted)
            found = find_named(extracted, "ffmpeg")
            if found is None:
                raise RuntimeError("ffmpeg missing from archive")
            media.mkdir(parents=True, exist_ok=True)
            shutil.copy2(found, binary)
            binary.chmod(binary.stat().st_mode | 0o111)
            print(f"Prepared macOS ffmpeg at {binary}")
    run(["bash", ROOT / "scripts" / "ffmpeg" / "ensure-macos-ffmpeg-dev.sh"])


def deps():
    if PLATFORM.startswith("windows-"):
        provision_windows_ffmpeg()
        provision_windows_qt()
        return
    if PLATFORM == "macos-arm64":
        provision_macos_qt()
        provision_macos_ffmpeg()
        return
    raise RuntimeError(f"Unsupported platform: {PLATFORM}")


def msvc_env(arch: str) -> dict[str, str]:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    install = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"], text=True
    ).splitlines()[0].strip()
    vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    dumped = subprocess.check_output(f'"{vcvars}" {arch} >nul && set', shell=True, text=True)
    env = os.environ.copy()
    for line in dumped.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            env[key] = value
    return env


def build_windows():
    spec = WINDOWS[PLATFORM]
    qt = windows_qt_root()
    env = msvc_env(spec["arch"])
    env["PATH"] = str(qt / "bin") + os.pathsep + env.get("PATH", "")
    run([
        "cmake", "-S", ROOT, "-B", BUILD_DIR, "-G", spec["generator"],
        f"-DCMAKE_PREFIX_PATH={qt}",
        "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",
        "-DMIACODE_BUILD_DEV_TOOLS=OFF",
    ], env=env)
    jobs = cpu_jobs()
    run(["cmake", "--build", BUILD_DIR, "--config", "Release", "--target", "MiaCode", "MiaCodeLauncher", "--parallel", jobs], env=env)
    run([
        "powershell", "-ExecutionPolicy", "Bypass", "-File", ROOT / "scripts" / "build" / "package-win.ps1",
        "-Arch", spec["arch"], "-BuildDir", BUILD_DIR, "-QtRoot", qt, "-BuildJobs", jobs,
    ], env=env)


def build_macos():
    qt = macos_qt_root()
    env = os.environ.copy()
    env["PATH"] = str(qt / "bin") + os.pathsep + env.get("PATH", "")
    env["QT_ROOT"] = str(qt)
    env["QT_ROOT_DIR"] = str(qt)
    env["BUILD_DIR"] = str(BUILD_DIR)
    env["CMAKE_OSX_ARCHITECTURES"] = "arm64"
    env["CMAKE_OSX_DEPLOYMENT_TARGET"] = os.environ.get("CMAKE_OSX_DEPLOYMENT_TARGET", "13.0")
    run(["bash", ROOT / "scripts" / "build" / "package-mac.sh"], env=env)


def build():
    if PLATFORM.startswith("windows-"):
        build_windows()
        return
    if PLATFORM == "macos-arm64":
        build_macos()
        return
    raise RuntimeError(f"Unsupported platform: {PLATFORM}")


def is_macho(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) in MACHO_MAGICS
    except OSError:
        return False


def macho_files(app: Path) -> list[Path]:
    seen: set[Path] = set()
    files: list[Path] = []
    for dirpath, _, names in os.walk(app):
        for name in names:
            path = Path(dirpath) / name
            if path.is_symlink() or not is_macho(path):
                continue
            real = path.resolve()
            if real in seen:
                continue
            seen.add(real)
            files.append(path)
    return files


def thin_one(path: Path, arch: str) -> int:
    archs = subprocess.check_output(["lipo", "-archs", str(path)], text=True).split()
    if arch not in archs:
        raise RuntimeError(f"Mach-O is missing '{arch}': {path} (has: {' '.join(archs)})")
    if len(archs) == 1:
        return 0
    handle, tmp = tempfile.mkstemp(prefix=path.name + ".", suffix=".thin", dir=path.parent)
    os.close(handle)
    try:
        run(["lipo", path, "-thin", arch, "-output", tmp])
        os.chmod(tmp, path.stat().st_mode)
        os.replace(tmp, path)
    except Exception:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    return 1


def thin():
    app = Path(sys.argv[2])
    arch = sys.argv[3]
    if not app.is_dir() or not app.name.endswith(".app"):
        raise RuntimeError(f"App bundle not found: {app}")
    files = macho_files(app)
    if not files:
        raise RuntimeError(f"No Mach-O binaries found in {app}")
    thinned = 0
    with ThreadPoolExecutor(max_workers=int(cpu_jobs())) as pool:
        futures = [pool.submit(thin_one, path, arch) for path in files]
        for future in as_completed(futures):
            thinned += future.result()
    print(f"thinned {thinned}/{len(files)} Mach-O files to {arch}")


def artifact():
    archives = list(DIST.glob("*.7z"))
    if len(archives) != 1:
        raise RuntimeError(f"Expected one archive, found {archives}")
    github_output(name=archives[0].name)


def report():
    archives = list(DIST.glob("*.7z"))
    if len(archives) != 1:
        raise RuntimeError(f"Expected one archive, found {archives}")
    elapsed = time.time() - int(os.environ["CI_STARTED_AT"])
    size = archives[0].stat().st_size / 1048576
    raw = os.environ.get("CI_TRIGGERED_AT", "").strip()
    if raw:
        moment = datetime.fromisoformat(raw.replace("Z", "+00:00"))
    else:
        moment = datetime.fromtimestamp(int(os.environ["CI_STARTED_AT"]), tz=timezone.utc)
    local = moment.astimezone(timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M")
    event = {"push": "推送", "workflow_dispatch": "手动"}.get(os.environ.get("CI_EVENT", ""), "")
    trigger = f"{local}（{event}）" if event else local
    total = max(0, int(round(elapsed)))
    hours, rem = divmod(total, 3600)
    minutes, seconds = divmod(rem, 60)
    if hours:
        spent = f"{hours} 小时 {minutes} 分"
    elif minutes and seconds:
        spent = f"{minutes} 分 {seconds:02d} 秒"
    elif minutes:
        spent = f"{minutes} 分钟"
    else:
        spent = f"{seconds} 秒"
    github_summary("\n".join((f"耗时 {spent}", f"大小 {size:.2f} MiB", f"触发 {trigger}")))


WINDOWS = {
    "windows-x64": {
        "arch": "x64",
        "aqt": "win64_msvc2022_64",
        "archdir": "msvc2022_64",
        "host": "windows_x86",
        "generator": "Ninja Multi-Config",
        "ffmpeg_dir": "win64",
        "ffmpeg_url": "https://github.com/GyanD/codexffmpeg/releases/download/7.1.1/ffmpeg-7.1.1-essentials_build.7z",
        "ffmpeg_archive": "ffmpeg.7z",
    },
    "windows-arm64": {
        "arch": "arm64",
        "aqt": "win64_msvc2022_arm64",
        "archdir": "msvc2022_arm64",
        "host": "windows_arm64",
        "generator": "Ninja Multi-Config",
        "ffmpeg_dir": "winarm64",
        "ffmpeg_url": "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-12-13-12/ffmpeg-n8.1.2-52-g5a03dfa0f6-winarm64-gpl-8.1.zip",
        "ffmpeg_archive": "ffmpeg.zip",
        "ffmpeg_dev_url": "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-12-13-12/ffmpeg-n8.1.2-52-g5a03dfa0f6-winarm64-lgpl-shared-8.1.zip",
    },
}


if __name__ == "__main__":
    {"keys": keys, "deps": deps, "build": build, "artifact": artifact, "report": report, "thin": thin}[sys.argv[1]]()

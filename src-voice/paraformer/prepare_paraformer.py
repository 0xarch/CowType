"""Optional preparation stages used by download_paraformer.py.

Never imported by the Rust recognition path. All subprocesses use argument
vectors, and Python packages are installed only in a dedicated virtualenv.
"""
import ctypes
import ctypes.util
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


NIX_ORT_LIBRARY = ""
TORCH_VERSION = "2.8.0"
EXPORT_PACKAGES = (
    "funasr==1.4.27", "onnx==1.19.1", "onnxruntime==1.23.2",
    "numpy==2.2.6", "transformers==4.57.1", "soundfile>=0.12.1,<1",
    "kaldi-native-fbank>=1.20,<2", "sentencepiece>=0.2,<1",
)
ONNX_FILES = ("model.onnx", "decoder.onnx", "config.yaml", "tokens.json", "am.mvn")


def add_arguments(parser):
    data = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "cowtype"
    parser.add_argument("--export-output", type=Path, default=data / "paraformer-zh-streaming",
                        help="ONNX 输出目录；也用于跳过导出时选择已有模型")
    parser.add_argument("--environment", type=Path, default=data / "paraformer-tools",
                        help="脚本管理的 Python 环境目录")
    parser.add_argument("--python", help="导出用 Python 3.10–3.13，可为可执行文件名或路径")
    parser.add_argument("--disable-environment", action="store_true", help="不创建虚拟环境，使用 --python 或当前解释器")
    parser.add_argument("--disable-python", action="store_true", help="不自动获取兼容的 Python 解释器")
    parser.add_argument("--disable-dependencies", action="store_true", help="不安装导出依赖，使用所选环境的现有依赖")
    parser.add_argument("--disable-download", action="store_true", help="不下载模型，使用 --output 指定的已有原始模型")
    parser.add_argument("--disable-export", action="store_true", help="不导出，后续步骤使用 --export-output 中的已有 ONNX 模型")
    parser.add_argument("--disable-select", action="store_true", help="不修改模型选择；同时跳过启用和重载")
    parser.add_argument("--disable-enable", action="store_true", help="保留现有 Voice 开关，只记录模型选择")
    parser.add_argument("--disable-reload", action="store_true", help="不通知 Fcitx5 重载")
    parser.add_argument("--disable-runtime-check", action="store_true", help="不检查系统 ONNX Runtime 动态库")


def call(argv, **kwargs):
    print("运行：" + " ".join(str(value) for value in argv), file=sys.stderr)
    try:
        subprocess.run([str(value) for value in argv], check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"准备步骤失败（退出码 {error.returncode}），详见上方输出。") from error


def python_info(executable):
    try:
        result = subprocess.run([str(executable), "-c",
            "import json,sys;print(json.dumps({'version':list(sys.version_info[:2]),"
            "'venv':sys.prefix!=sys.base_prefix,'prefix':sys.prefix}))"],
            check=True, stdout=subprocess.PIPE, text=True, timeout=30)
        return json.loads(result.stdout)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"无法读取 Python 解释器信息：{executable}") from error


def compatible(info):
    return (3, 10) <= tuple(info["version"]) <= (3, 13)


def interpreter(args, root):
    """Resolve an existing interpreter without provisioning or installing packages."""
    if args.python:
        executable = shutil.which(args.python)
        if not executable:
            raise RuntimeError("找不到 --python 指定的解释器。")
        if not compatible(python_info(executable)):
            raise RuntimeError("固定的 CPU 导出依赖需要 Python 3.10–3.13；推荐 3.12。")
        return executable
    if args.disable_environment:
        if not compatible(python_info(sys.executable)):
            raise RuntimeError("当前 Python 不适配导出依赖，请用 --python 指定 Python 3.10–3.13。")
        return sys.executable
    if (root / "venv/bin/python").is_file():
        return str(root / "venv/bin/python")
    for name in ("python3.12", "python3.11", "python3.13", "python3.10", sys.executable):
        executable = shutil.which(name)
        if executable and compatible(python_info(executable)):
            return executable
    if args.disable_python:
        raise RuntimeError("未找到 Python 3.10–3.13；已禁用解释器下载，请指定 --python。")
    return None


def writable(path, reserve=0):
    if path.is_symlink():
        raise RuntimeError(f"准备流程写入路径不能是软链接：{path}")
    parent = path if path.is_dir() else path.parent
    while not parent.exists():
        parent = parent.parent
    with tempfile.TemporaryFile(dir=parent) as probe:
        probe.write(b"cowtype")
    if shutil.disk_usage(parent).free < reserve:
        raise RuntimeError(f"可用空间不足（此步骤需预留约 {reserve // 1073741824} GiB）：{path}")


def atomic_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".cowtype-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def raw_ready(source):
    for name in ("model.pt", "config.yaml", "tokens.json", "am.mvn"):
        with (source / name).open("rb") as stream:
            head = stream.read(128)
        if not head or head.startswith(b"version https://git-lfs.github.com/spec/"):
            raise RuntimeError(f"缺少真实原始模型文件（不能使用 Git LFS 指针）：{name}")


def onnx_ready(folder):
    for name in ONNX_FILES:
        with (folder / name).open("rb") as stream:
            head = stream.read(128)
        if not head or head.startswith(b"version https://git-lfs.github.com/spec/"):
            raise RuntimeError(f"ONNX 模型不完整：{name}")


def runtime_check():
    override = os.environ.get("ORT_DYLIB_PATH") or NIX_ORT_LIBRARY
    library = override or ctypes.util.find_library("onnxruntime") or "libonnxruntime.so"
    try:
        runtime = ctypes.CDLL(library)
        runtime.OrtGetApiBase.restype = ctypes.c_void_p
        class ApiBase(ctypes.Structure):
            _fields_ = [("get_api", ctypes.c_void_p), ("get_version", ctypes.CFUNCTYPE(ctypes.c_char_p))]
        address = runtime.OrtGetApiBase()
        if not address:
            raise RuntimeError("OrtGetApiBase 返回空指针")
        version = ctypes.cast(address, ctypes.POINTER(ApiBase)).contents.get_version().decode("ascii")
        numbers = re.match(r"(\d+)\.(\d+)", version)
        if not numbers or tuple(map(int, numbers.groups())) < (1, 22):
            raise RuntimeError(f"ONNX Runtime {version} 低于 1.22")
    except (OSError, AttributeError, UnicodeError) as error:
        raise RuntimeError("找不到可用的系统 ONNX Runtime 1.22+。请安装发行版运行库，"
                           "或用 ORT_DYLIB_PATH 指定它；仅准备模型可用 --disable-runtime-check。") from error
    print(f"系统 ONNX Runtime：{version}（{library}）", file=sys.stderr)


def setup_environment(args, root, python):
    if args.disable_environment:
        return python
    root.mkdir(parents=True, exist_ok=True)
    target = root / "venv"
    if target.exists():
        if not (target / "pyvenv.cfg").is_file() or not (target / "bin/python").is_file():
            raise RuntimeError("环境目录不完整；请使用新的 --environment，脚本不会删除现有目录。")
        if not compatible(python_info(target / "bin/python")):
            raise RuntimeError("已有导出环境的 Python 版本不兼容；请使用新的 --environment。")
        if args.python and python_info(target / "bin/python")["version"] != python_info(python)["version"]:
            raise RuntimeError("已有环境的 Python 版本与 --python 不同；请使用新的 --environment。")
        return str(target / "bin/python")
    if python is None:
        # uv lives in an isolated bootstrap environment, never in system Python.
        uv = shutil.which("uv")
        if uv is None:
            if importlib.util.find_spec("venv") is None or importlib.util.find_spec("ensurepip") is None:
                raise RuntimeError("需要 Python venv/ensurepip 或 uv；Debian/Ubuntu 可安装 python3-venv。")
            bootstrap = root / "bootstrap"
            if not (bootstrap / "bin/python").exists():
                call([sys.executable, "-m", "venv", bootstrap])
            call([bootstrap / "bin/python", "-m", "pip", "install", "uv>=0.9,<1"])
            uv = str(bootstrap / "bin/uv")
        environment = os.environ.copy()
        environment.update(UV_PYTHON_INSTALL_DIR=str(root / "pythons"),
                           UV_CACHE_DIR=str(root / "uv-cache"), UV_NATIVE_TLS="true")
        call([uv, "venv", "--python", "3.12", "--python-preference", "only-managed", "--seed", target], env=environment)
    else:
        call([python, "-m", "venv", target])
    return str(target / "bin/python")


def dependencies(args, root, python):
    if args.disable_dependencies:
        return
    if not python_info(python)["venv"]:
        raise RuntimeError("依赖只能安装到虚拟环境；请启用环境准备，或指定已有虚拟环境的 --python。")
    # The official CPU wheel index avoids implicitly downloading CUDA packages.
    call([python, "-m", "pip", "install", "--only-binary=:all:",
          f"torch=={TORCH_VERSION}", f"torchaudio=={TORCH_VERSION}",
          "--index-url", "https://download.pytorch.org/whl/cpu"])
    call([python, "-m", "pip", "install", *EXPORT_PACKAGES,
          f"torch=={TORCH_VERSION}", f"torchaudio=={TORCH_VERSION}"])
    result = subprocess.run([python, "-m", "pip", "freeze"], check=True, text=True, stdout=subprocess.PIPE)
    atomic_text(root / "requirements-resolved.txt", result.stdout)
    print("依赖版本已记录到 requirements-resolved.txt。", file=sys.stderr)


def check_dependencies(python, imports=False):
    # No checkpoint, audio or graph is loaded here. Imports also expose missing
    # ELF loader / shared-library dependencies before the large model download.
    code = "import importlib.metadata as m; [m.version(n) for n in ('funasr','torch','torchaudio','onnx','onnxruntime')]"
    if imports:
        code += ";import torch,torchaudio,onnx,onnxruntime;from funasr import AutoModel"
    try:
        subprocess.run([python, "-c", code], check=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError("导出依赖不可用，请检查上方错误。NixOS 的 pip 二进制轮子还需 nix-ld/共享库支持；"
                           "可用 --disable-environment --disable-dependencies --python 指定已准备的环境。") from error


def source_fingerprints(source):
    """Include optional assets, so adding/removing them invalidates an old export too."""
    hashes = {}
    # Keep this list in sync with export_paraformer.py's SOURCE.json writer.
    for name in ("model.pt", "config.yaml", "configuration.json", "tokens.json", "am.mvn", "seg_dict"):
        path = source / name
        if not path.is_file():
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1048576), b""):
                digest.update(block)
        hashes[name] = digest.hexdigest()
    return hashes


def export_model(source, output, python):
    raw_ready(source)
    if output.exists():
        # Never silently use a pre-existing export for different weights.
        onnx_ready(output)
        try:
            metadata = json.loads((output / "SOURCE.json").read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise RuntimeError("已有 ONNX 目录缺少来源清单；请改用新的 --export-output，或显式 --disable-export。") from error
        if (metadata.get("interface") != "funasr-streaming-paraformer-two-graph"
                or not isinstance(metadata.get("files"), dict)
                or not set(ONNX_FILES).issubset(metadata["files"])):
            raise RuntimeError("已有导出的来源清单不完整，请使用新的 --export-output。")
        digest = hashlib.sha256()
        with (source / "model.pt").open("rb") as stream:
            for block in iter(lambda: stream.read(1048576), b""):
                digest.update(block)
        if metadata.get("checkpoint_sha256") != digest.hexdigest():
            raise RuntimeError("已有导出目录的权重来源不同，请使用新的 --export-output。")
        if not isinstance(metadata.get("source_files"), dict):
            raise RuntimeError("已有导出缺少原始配置、词表及 CMVN 的校验值；请使用新的 --export-output，"
                               "或显式 --disable-export 选用旧导出。")
        if metadata["source_files"] != source_fingerprints(source):
            raise RuntimeError("已有导出的原始配置、词表、CMVN 或其他模型资产已变化；"
                               "请使用新的 --export-output。")
        for name, expected in metadata.get("files", {}).items():
            if Path(name).name != name:
                raise RuntimeError("导出来源清单中存在不合法路径。")
            digest = hashlib.sha256()
            with (output / name).open("rb") as stream:
                for block in iter(lambda: stream.read(1048576), b""):
                    digest.update(block)
            if digest.hexdigest() != expected:
                raise RuntimeError(f"已有导出文件校验失败：{name}")
        print("复用来源和校验值匹配的 ONNX 导出。", file=sys.stderr)
        return
    helper = Path(__file__).with_name("export_paraformer.py")
    call([python, helper, "--source", source, "--output", output])
    onnx_ready(output)


def selection_paths():
    config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    return config / "cowtype/voice-model.json", config / "fcitx5/conf/cowtype.conf"


def select_model(output):
    onnx_ready(output)
    selection, _ = selection_paths()
    atomic_text(selection, json.dumps({"directory": str(output), "source": ""}, ensure_ascii=False, indent=2) + "\n")
    print(f"已选用：{output}", file=sys.stderr)


def enable_voice():
    _, config = selection_paths()
    text = config.read_text(encoding="utf-8") if config.exists() else ""
    lines = text.splitlines(keepends=True)
    found = False
    grouped = False
    for i, line in enumerate(lines):
        if line.strip().startswith("["):
            grouped = True
        if not grouped and re.match(r"\s*Voice\s*=", line):
            lines[i] = "Voice=True\n"
            found = True
    if not found:
        lines.insert(0, "Voice=True\n")
    atomic_text(config, "".join(lines))
    print("已启用语音输入，其余配置原样保留。", file=sys.stderr)


def reload_config():
    command = shutil.which("dbus-send")
    if not command or not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("未找到会话 D-Bus 或 dbus-send；模型准备已完成，启动或重启 Fcitx5 后生效。", file=sys.stderr)
        return
    try:
        subprocess.run([command, "--session", "--print-reply", "--reply-timeout=5000",
                        "--dest=org.fcitx.Fcitx5", "/controller",
                        "org.fcitx.Fcitx.Controller1.ReloadAddonConfig", "string:cowtype"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=8)
        print("已通知 Fcitx5 重载语音配置。", file=sys.stderr)
    except (OSError, subprocess.SubprocessError):
        print("Fcitx5 暂未响应；模型及配置已保存，启动或重启 Fcitx5 后生效。", file=sys.stderr)


def run(args, downloader):
    if sys.version_info < (3, 10) or not sys.platform.startswith("linux"):
        raise RuntimeError("需要 Linux 和 Python 3.10 或更新版本。")
    source = args.output.expanduser().absolute()
    output = args.export_output.expanduser().absolute()
    root = args.environment.expanduser().absolute()
    needs_export = not args.disable_export
    plans = [
        (not args.disable_runtime_check, "检查系统 ONNX Runtime"),
        (needs_export and not args.disable_environment, "准备独立 Python 环境"),
        (needs_export and not args.disable_dependencies, "安装 CPU 导出依赖"),
        (not args.disable_download, "下载并校验原始模型"),
        (needs_export, "导出或复用 ONNX 模型"),
        (not args.disable_select, "记录模型选择"),
        (not args.disable_select and not args.disable_enable, "启用语音输入"),
        (not args.disable_select and not args.disable_reload, "通知 Fcitx5 重载"),
    ]
    print("准备流程：\n" + "\n".join(f"  {'执行' if enabled else '跳过'} {name}" for enabled, name in plans), file=sys.stderr)
    if os.geteuid() == 0:
        raise RuntimeError("请使用普通用户运行，环境、模型和配置应归当前桌面用户所有。")
    if not args.disable_select and os.environ.get("COWTYPE_VOICE_MODEL_DIR"):
        raise RuntimeError("COWTYPE_VOICE_MODEL_DIR 正在覆盖选择；请取消它，或传 --disable-select。")
    if output == source and needs_export:
        raise RuntimeError("原始模型与 ONNX 输出必须是不同目录。")
    if not source.name or not output.name:
        raise RuntimeError("模型目录不能是根目录。")
    for left, right in ((source, output), (source, root), (output, root)):
        if left == right or left in right.parents or right in left.parents:
            raise RuntimeError("原始权重、ONNX 输出和工具环境目录必须彼此独立，不能互相包含。")
    writable(root)
    context = None
    workspace = source.with_name("." + source.name + ".download")
    if not args.disable_download:
        context = downloader.preflight(source, workspace)
    elif needs_export:
        raw_ready(source)
    if not needs_export and not args.disable_select:
        onnx_ready(output)
    python = interpreter(args, root) if needs_export else None
    if needs_export:
        writable(output, 4 * 1073741824 if not output.exists() else 0)
        writable(root, 10 * 1073741824 if not args.disable_dependencies else 0)
        if not args.disable_dependencies:
            import ssl
            ssl.create_default_context(cafile=os.environ.get("SSL_CERT_FILE") or os.environ.get("NIX_SSL_CERT_FILE") or None)
        if args.disable_environment and not args.disable_dependencies and not python_info(python)["venv"]:
            raise RuntimeError("--disable-environment 安装依赖时必须指定已有虚拟环境的 --python。")
        if not (Path(__file__).with_name("export_paraformer.py")).is_file():
            raise RuntimeError("缺少配套 export_paraformer.py，请检查安装。")
        if not args.disable_environment and not (root / "venv/bin/python").exists():
            if python:
                try:
                    subprocess.run([python, "-c", "import venv,ensurepip"], check=True, timeout=30)
                except subprocess.SubprocessError as error:
                    raise RuntimeError("所选 Python 缺少 venv/ensurepip，请安装对应的 venv 支持。") from error
            elif not shutil.which("uv") and (importlib.util.find_spec("venv") is None
                                               or importlib.util.find_spec("ensurepip") is None):
                raise RuntimeError("自动获取 Python 需要 uv，或当前解释器的 venv/ensurepip 支持。")
        if args.disable_dependencies:
            # For a newly created virtualenv the dependencies would be absent.
            target_python = root / "venv/bin/python" if not args.disable_environment else python
            if not target_python or not Path(target_python).is_file():
                raise RuntimeError("已禁用依赖安装，但不存在已准备的虚拟环境；请指定 --python 和 --disable-environment。")
            check_dependencies(str(target_python))
    if not args.disable_select:
        for path in selection_paths()[:1 if args.disable_enable else 2]:
            writable(path)
    if not args.disable_runtime_check:
        runtime_check()
    if args.check:
        print("前置检查完成。未下载、创建环境、安装依赖、导出或修改配置。")
        return
    # The download has its own lock; serialize the full workflow as well.
    import fcntl
    root.mkdir(parents=True, exist_ok=True)
    with (root / ".prepare.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("同一环境已有准备任务在运行。") from error
        if needs_export:
            python = setup_environment(args, root, python)
            dependencies(args, root, python)
            check_dependencies(python, imports=True)
        if not args.disable_download:
            downloader.download(args, source, workspace, context)
        if needs_export:
            export_model(source, output, python)
        if not args.disable_select:
            select_model(output)
            if not args.disable_enable:
                enable_voice()
            if not args.disable_reload:
                reload_config()
    print("所选模型准备步骤已完成。")

#!/usr/bin/env python3
"""Prepare Paraformer: environment, CPU dependencies, download, ONNX export and selection.

Use --disable-* to skip stages. Requires Linux, Python >= 3.10, HTTPS CA
certificates, network access and disk space. Never installs system packages.
ModelScope is the default; download failures fall back to Hugging Face.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time


# Public, immutable upstream snapshots. Shared file identities were checked
# against ModelScope SHA-256 metadata and Hugging Face Git/LFS blob metadata.
MS_REPO = "iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online"
MS_REVISION = "70e272c03055313e90f34db80bef9c19745e41cf"
HF_REPO = "funasr/paraformer-zh-streaming"
HF_REVISION = "fd2af606b37d7fb8b3b8a218c5be5b07b53ef6ba"
FILES = {
    "model.pt": (880716041, "4fdfb48ed4471777c9a511e96a2acae17f77cac9d709cc756634622769192a64"),
    "config.yaml": (2940, "35e6bf41f8c7eaf9a0f787af7fdc8fc5ed75fa8009ade7d3c2f3ef5bce20c648"),
    "configuration.json": (472, "1aae0f45e3e503f52512eee4798d241611a44cc732a7a91d2e2d2f65dd9755be"),
    "tokens.json": (93676, "2b20c2b12572d682afff84ce1c8d560f67b8b32a4c1f21567411d141ed352127"),
    "am.mvn": (11203, "29b3c740a2c0cfc6b308126d31d7f265fa2be74f3bb095cd2f143ea970896ae5"),
    "seg_dict": (8287834, "59a2ef803a3f1648ad03a2e1480db1c1ee0c0d7dc4ef4dbd16cea33944329022"),
}
DOCUMENTS = {
    "modelscope": {"README.md": (13440, "e424224c75e4d803f2177d916ea614e40b19345bb080b0edb9cc28b45b33133c")},
    "huggingface": {
        "README.md": (3159, "d24e63307ab08112836f3f8c2be89ec014898064f4aa3145a8d16b18b71fe40a"),
        "LICENSE": (11358, "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30"),
    },
}


class DownloadError(Exception):
    pass


def manifest(provider):
    return {**FILES, **DOCUMENTS[provider]}


def url_for(provider, name):
    from urllib.parse import urlencode
    if provider == "modelscope":
        return "https://www.modelscope.cn/api/v1/models/" + MS_REPO + "/repo?" + urlencode(
            {"Revision": MS_REVISION, "FilePath": name})
    return f"https://huggingface.co/{HF_REPO}/resolve/{HF_REVISION}/{name}"


def valid(path, spec):
    size, expected = spec
    if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
        return False
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest() == expected


def complete(destination):
    """Only recognize our own complete output, never adopt arbitrary directories."""
    try:
        metadata = json.loads((destination / "DOWNLOAD.json").read_text(encoding="utf-8"))
        provider = metadata["provider"]
        revision = MS_REVISION if provider == "modelscope" else HF_REVISION
        return (metadata["revision"] == revision and provider in DOCUMENTS
                and all(valid(destination / name, spec) for name, spec in manifest(provider).items()))
    except (OSError, ValueError, KeyError, TypeError):
        return False


def preflight(destination, workspace):
    """Check dependencies and local prerequisites before any network request."""
    if sys.version_info < (3, 10):
        raise RuntimeError("需要 Python 3.10 或更新版本。")
    if not sys.platform.startswith("linux"):
        raise RuntimeError("此脚本面向 Linux（包括 NixOS）。")
    try:
        import ssl
        import fcntl  # noqa: F401 - required for the download lock
        import urllib.request  # noqa: F401 - must be available before writing anything
    except ImportError as error:
        raise RuntimeError("需要包含 ssl、fcntl 和 urllib 的完整 Python 标准库。") from error
    ca = os.environ.get("SSL_CERT_FILE") or os.environ.get("NIX_SSL_CERT_FILE")
    context = ssl.create_default_context(cafile=ca or None)
    if not context.cert_store_stats().get("x509_ca"):
        raise RuntimeError("找不到 HTTPS CA 证书；请安装 ca-certificates/cacert，或设置 SSL_CERT_FILE。")
    if destination.is_symlink():
        raise RuntimeError("输出目录不能是软链接；请指定新的用户数据目录。")
    if destination.exists() and not destination.is_dir():
        raise RuntimeError("输出路径已存在且不是目录。")
    if destination.exists() and not (destination / "DOWNLOAD.json").is_file():
        raise RuntimeError("输出目录已存在且不是本脚本的下载结果；请指定新的 --output 目录。")
    if workspace.is_symlink():
        raise RuntimeError("下载缓存目录不能是软链接。")
    if workspace.exists() and not workspace.is_dir():
        raise RuntimeError("下载缓存路径已存在且不是目录。")
    parent = destination.parent
    while not parent.exists():
        parent = parent.parent
    if not parent.is_dir():
        raise RuntimeError("输出路径的上级不是目录。")
    # Probe actual write access instead of relying on permission bits or uid.
    with tempfile.TemporaryDirectory(prefix=".cowtype-model-check-", dir=parent) as temporary:
        probe = Path(temporary) / "write"
        probe.write_bytes(b"cowtype")
        try:
            os.link(probe, Path(temporary) / "link")
        except OSError as error:
            raise RuntimeError("输出文件系统需支持硬链接，以便完成下载时不复制整份权重。") from error
    if not destination.exists():
        required = sum(size for size, _ in FILES.values()) + 64 * 1024 * 1024
        # Cached bytes and the completed output share storage through hard links.
        cached = 0
        for size, digest in FILES.values():
            for suffix in ("", ".part"):
                path = workspace / (digest + suffix)
                if path.is_file() and not path.is_symlink():
                    cached += min(size, path.stat().st_size)
                    break
        if shutil.disk_usage(parent).free < required - cached:
            raise RuntimeError("磁盘空间不足：首次下载请至少预留约 1 GiB；导出 ONNX 还需额外空间。")
    print("前置检查通过：Python、HTTPS 证书、目录写权限、硬链接和下载空间。", file=sys.stderr)
    return context


def fetch(provider, name, spec, workspace, context, timeout):
    import http.client
    import urllib.error
    import urllib.request
    size, digest = spec
    blob = workspace / digest
    part = workspace / (digest + ".part")
    for path in (blob, part):
        if path.is_symlink():
            raise RuntimeError("下载缓存中发现软链接，请移走缓存后重试。")
    if valid(blob, spec):
        print(f"复用已校验文件：{name}", file=sys.stderr)
        return blob
    if valid(part, spec):
        part.replace(blob)
        return blob
    for attempt in range(3):
        if part.exists() and part.stat().st_size >= size:
            if valid(part, spec):
                part.replace(blob)
                return blob
            part.unlink()
        offset = part.stat().st_size if part.exists() else 0
        headers = {"User-Agent": "CowType-Paraformer-Downloader/1", "Accept-Encoding": "identity"}
        if offset:
            headers["Range"] = f"bytes={offset}-"
        print(f"[{provider}] {name}，尝试 {attempt + 1}/3，已有 {offset}/{size} 字节", file=sys.stderr)
        try:
            request = urllib.request.Request(url_for(provider, name), headers=headers)
            with urllib.request.urlopen(request, context=context, timeout=timeout) as response:
                if not response.geturl().startswith("https://"):
                    raise DownloadError("拒绝非 HTTPS 下载地址")
                if response.status == 206:
                    match = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", response.headers.get("Content-Range", ""))
                    if not match or tuple(map(int, match.groups())) != (offset, size - 1, size):
                        raise DownloadError("服务器返回了不匹配的断点范围")
                elif response.status == 200:
                    offset = 0  # Server ignored Range: restart, never append the full file.
                else:
                    raise DownloadError(f"不支持的 HTTP 状态：{response.status}")
                length = response.headers.get("Content-Length")
                if length is not None and length != str(size - offset):
                    raise DownloadError("下载长度与固定快照不匹配")
                total = offset
                reported = time.monotonic()
                with part.open("ab" if offset else "wb") as stream:
                    while True:
                        try:
                            block = response.read(min(1024 * 1024, size - total + 1))
                        except (OSError, http.client.HTTPException) as error:
                            raise DownloadError(f"{name} 网络读取中断：{error}") from error
                        if not block:
                            break
                        total += len(block)
                        if total > size:
                            raise DownloadError("下载内容超过固定文件大小")
                        stream.write(block)
                        if time.monotonic() - reported >= 5:
                            print(f"  {name}: {total / size:.1%} ({total // 1048576} MiB)", file=sys.stderr)
                            reported = time.monotonic()
            if not valid(part, spec):
                if part.stat().st_size >= size:
                    part.unlink()  # A corrupt full response must not poison mirror retries.
                raise DownloadError(f"{name} 文件不完整或 SHA-256 不匹配（不能使用 Git LFS 指针）")
            part.replace(blob)
            return blob
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            failure = DownloadError(f"{name} 网络错误：{error}")
        except DownloadError as error:
            failure = error
        print(str(failure), file=sys.stderr)
    raise failure


def download(args, destination, workspace, context):
    import fcntl
    destination.parent.mkdir(parents=True, exist_ok=True)
    workspace.mkdir(mode=0o700, exist_ok=True)
    with (workspace / ".lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("同一输出目录已有下载任务在运行。") from error
        if destination.exists():
            if complete(destination):
                print(f"文件已完整并通过校验，无需重复下载：{destination}")
                return
            raise RuntimeError("输出目录已存在且不符合完整下载清单；不会覆盖，请使用新的 --output 目录。")
        providers = [args.source]
        if args.source == "modelscope" and not args.no_fallback:
            providers.append("huggingface")
        for provider in providers:
            try:
                files = {name: fetch(provider, name, spec, workspace, context, args.timeout)
                         for name, spec in manifest(provider).items()}
            except DownloadError as error:
                print(f"{provider} 下载失败：{error}", file=sys.stderr)
                if provider == providers[-1]:
                    raise
                print("回退 Hugging Face；相同 SHA-256 的文件会复用或续传。", file=sys.stderr)
                continue
            result = Path(tempfile.mkdtemp(prefix="result-", dir=workspace))
            try:
                for name, blob in files.items():
                    os.link(blob, result / name)
                metadata = {
                    "provider": provider, "repository": MS_REPO if provider == "modelscope" else HF_REPO,
                    "revision": MS_REVISION if provider == "modelscope" else HF_REVISION,
                    "format": "pytorch-checkpoint-requires-explicit-onnx-export",
                    "license_reference": f"https://huggingface.co/{HF_REPO}/tree/{HF_REVISION}",
                    "files": {name: {"size": spec[0], "sha256": spec[1], "url": url_for(provider, name)}
                              for name, spec in manifest(provider).items()},
                }
                (result / "DOWNLOAD.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                if destination.exists() or destination.is_symlink():
                    raise RuntimeError("输出路径已被其他程序创建；停止，避免覆盖。")
                result.rename(destination)
                # Completed files now live in the destination; keep partial downloads
                # from interrupted attempts, without a second copy of the weights.
                for blob in set(files.values()):
                    blob.unlink()
            finally:
                if result.exists():
                    shutil.rmtree(result)
            print(f"下载完成并通过 SHA-256 校验：{destination}")
            return


def main():
    # Source checkout and installed FHS/Nix prefixes share the same helper files.
    script = Path(__file__).resolve()
    helpers = (script.parent, script.parent.parent / "share/cowtype/voice")
    helper = next((folder for folder in helpers if (folder / "prepare_paraformer.py").is_file()), None)
    if helper is None:
        raise RuntimeError("缺少配套 prepare_paraformer.py，请检查安装是否完整。")
    sys.path.insert(0, str(helper))
    import prepare_paraformer
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", choices=("modelscope", "huggingface"), default="modelscope", help="下载源，默认 modelscope")
    parser.add_argument("--disable-fallback", "--no-fallback", dest="no_fallback", action="store_true", help="ModelScope 失败时不回退 Hugging Face")
    default = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "cowtype/paraformer-zh-streaming-source"
    parser.add_argument("--output", type=Path, default=default, help="原始权重目录，默认遵循 XDG_DATA_HOME")
    parser.add_argument("--timeout", type=float, default=45, help="每次网络操作的超时秒数，默认 45")
    parser.add_argument("--check", action="store_true", help="只检查所选步骤的前置条件，不执行准备操作")
    prepare_paraformer.add_arguments(parser)
    args = parser.parse_args()
    if not 0 < args.timeout < float("inf"):
        parser.error("--timeout 必须为有限正数")
    prepare_paraformer.run(args, sys.modules[__name__])


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("下载已取消；重新运行相同命令可续传。", file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, DownloadError, OSError) as error:
        print(f"错误：{error}", file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Explicit, offline FunASR export. Never used by the Rust inference path.

Run with a separate user-prepared Python environment containing FunASR, CPU
PyTorch and ONNX export dependencies. This script does not install dependencies
or download models. Only load checkpoints whose source you trust.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import tempfile


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def actual_file(path):
    with path.open("rb") as stream:
        head = stream.read(128)
    if not head or head.startswith(b"version https://git-lfs.github.com/spec/"):
        raise ValueError(f"Empty file or unresolved Git LFS pointer: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Local model directory or .pt/.pth checkpoint")
    parser.add_argument("--output", type=Path, required=True, help="New output directory (must not exist)")
    args = parser.parse_args()
    source = args.source.expanduser().resolve(strict=True)
    folder = source if source.is_dir() else source.parent
    checkpoint = folder / "model.pt" if source.is_dir() else source
    destination = args.output.expanduser().absolute()
    if destination.exists():
        raise FileExistsError(f"Output already exists: {destination}")
    if checkpoint.suffix.lower() not in (".pt", ".pth"):
        raise ValueError("Expected a .pt/.pth checkpoint and its config/tokens/CMVN directory")
    for path in (checkpoint, *(folder / name for name in ("config.yaml", "tokens.json", "am.mvn"))):
        actual_file(path)
    # A local path plus offline flags prevents model aliases from silently downloading another model.
    os.environ["HF_HUB_OFFLINE"] = "1"
    os.environ["MODELSCOPE_OFFLINE"] = "1"
    try:
        from funasr import AutoModel
    except ImportError as error:
        raise RuntimeError("Prepare a separate Python environment with FunASR, CPU PyTorch and ONNX export dependencies first; no packages were installed") from error

    destination.parent.mkdir(parents=True, exist_ok=True)
    # Export mutates its model directory. Copy small assets and link only the trusted
    # checkpoint into a disposable workspace so the user's source remains untouched.
    with tempfile.TemporaryDirectory(prefix=".paraformer-export-", dir=destination.parent) as temporary:
        work = Path(temporary) / "source"
        work.mkdir()
        for name in ("config.yaml", "configuration.json", "tokens.json", "am.mvn", "seg_dict", "README.md", "LICENSE", "MODEL_LICENSE", "NOTICE", "DOWNLOAD.json"):
            if (folder / name).is_file():
                shutil.copy2(folder / name, work / name)
        (work / "model.pt").symlink_to(checkpoint)
        model = AutoModel(model=str(work), init_param=str(work / "model.pt"), device="cpu",
                          trust_remote_code=False, disable_update=True)
        exported = model.export(type="onnx", quantize=False, opset_version=14)
        candidates = [work]
        if isinstance(exported, (str, os.PathLike)):
            path = Path(exported)
            candidates.insert(0, path if path.is_dir() else path.parent)
        export_dir = next((path for path in candidates if (path / "model.onnx").is_file()
                           and (path / "decoder.onnx").is_file()), None)
        if export_dir is None:
            raise RuntimeError("Official export did not produce model.onnx and decoder.onnx")
        stage = Path(temporary) / "result"
        stage.mkdir()
        for name in ("model.onnx", "decoder.onnx", "config.yaml", "tokens.json", "am.mvn"):
            path = export_dir / name
            if not path.is_file():
                path = work / name
            actual_file(path)
            shutil.copy2(path, stage / name)
        # ONNX may reference adjacent external tensor data, depending on exporter version.
        for path in export_dir.iterdir():
            if path.is_file() and (".onnx" in path.name or path.suffix == ".data") and not (stage / path.name).exists():
                shutil.copy2(path, stage / path.name)
        for name in ("README.md", "LICENSE", "MODEL_LICENSE", "NOTICE", "configuration.json", "DOWNLOAD.json"):
            if (work / name).is_file():
                shutil.copy2(work / name, stage / name)
        versions = {}
        for name in ("funasr", "torch", "onnx", "onnxruntime"):
            try:
                versions[name] = importlib.metadata.version(name)
            except importlib.metadata.PackageNotFoundError:
                pass
        (stage / "SOURCE.json").write_text(json.dumps({
            "checkpoint": str(checkpoint), "checkpoint_sha256": digest(checkpoint),
            # These assets affect export/inference even when the checkpoint is unchanged.
            # Keep this list in sync with prepare_paraformer.source_fingerprints.
            "source_files": {"model.pt": digest(checkpoint), **{
                name: digest(folder / name)
                for name in ("config.yaml", "configuration.json", "tokens.json", "am.mvn", "seg_dict")
                if (folder / name).is_file()
            }},
            "exporter": versions, "interface": "funasr-streaming-paraformer-two-graph",
            "chunk_size": [5, 10, 5], "runtime": "Rust + system ONNX Runtime",
            "files": {path.name: digest(path) for path in sorted(stage.iterdir()) if path.is_file()},
        }, ensure_ascii=False, indent=2) + "\n")
        # Destination was required to be absent; never replace existing user files.
        stage.rename(destination)
    print(destination)


if __name__ == "__main__":
    main()

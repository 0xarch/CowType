"""Offline provenance regressions; fake graphs/checkpoints, no export or inference."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock


VOICE = Path(__file__).resolve().parents[1]


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, VOICE / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


prepare = load_module("prepare_paraformer")
exporter = load_module("export_paraformer")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ExportProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.output = self.root / "output"
        self.source.mkdir()
        self.output.mkdir()
        for name in ("model.pt", "config.yaml", "tokens.json", "am.mvn", "configuration.json", "seg_dict"):
            (self.source / name).write_bytes(("source-" + name).encode())
        for name in prepare.ONNX_FILES:
            (self.output / name).write_bytes(("exported-" + name).encode())
        self.metadata = {
            "interface": "funasr-streaming-paraformer-two-graph",
            "checkpoint_sha256": digest(self.source / "model.pt"),
            "source_files": prepare.source_fingerprints(self.source),
            "files": {name: digest(self.output / name) for name in prepare.ONNX_FILES},
        }
        self.write_metadata()

    def write_metadata(self):
        (self.output / "SOURCE.json").write_text(json.dumps(self.metadata))

    def test_matching_export_is_reused_without_launching_exporter(self):
        with mock.patch.object(prepare, "call", side_effect=AssertionError("must not export")):
            prepare.export_model(self.source, self.output, "unused")

    def test_each_changed_source_asset_prevents_reuse(self):
        for name in self.metadata["source_files"]:
            with self.subTest(name=name):
                path = self.source / name
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                with self.assertRaises(RuntimeError):
                    prepare.export_model(self.source, self.output, "unused")
                path.write_bytes(original)

    def test_removing_optional_source_asset_prevents_reuse(self):
        (self.source / "seg_dict").unlink()
        with self.assertRaisesRegex(RuntimeError, "资产已变化"):
            prepare.export_model(self.source, self.output, "unused")

    def test_adding_optional_source_asset_prevents_reuse(self):
        self.metadata["source_files"].pop("configuration.json")
        self.write_metadata()
        with self.assertRaisesRegex(RuntimeError, "资产已变化"):
            prepare.export_model(self.source, self.output, "unused")

    def test_legacy_export_requires_explicit_user_choice(self):
        self.metadata.pop("source_files")
        self.write_metadata()
        with self.assertRaisesRegex(RuntimeError, "--disable-export"):
            prepare.export_model(self.source, self.output, "unused")

    def test_modified_export_is_still_rejected(self):
        (self.output / "decoder.onnx").write_bytes(b"changed graph")
        with self.assertRaisesRegex(RuntimeError, "校验失败"):
            prepare.export_model(self.source, self.output, "unused")

    def test_export_metadata_matches_reuse_contract(self):
        destination = self.root / "fresh-export"

        class FakeAutoModel:
            def __init__(self, **kwargs):
                self.work = Path(kwargs["model"])

            def export(self, **kwargs):
                for name in ("model.onnx", "decoder.onnx"):
                    (self.work / name).write_bytes(b"fake graph")
                return self.work

        # Only exercise file handling and metadata, never import the real exporter.
        with mock.patch.dict(sys.modules, {"funasr": types.SimpleNamespace(AutoModel=FakeAutoModel)}), \
                mock.patch.object(sys, "argv", ["export_paraformer.py", "--source", str(self.source),
                                                "--output", str(destination)]), \
                mock.patch.object(exporter.importlib.metadata, "version", return_value="test"), \
                mock.patch.dict(exporter.os.environ, {}, clear=False):
            exporter.main()
        metadata = json.loads((destination / "SOURCE.json").read_text())
        self.assertEqual(metadata["source_files"], prepare.source_fingerprints(self.source))
        with mock.patch.object(prepare, "call", side_effect=AssertionError("must not export")):
            prepare.export_model(self.source, destination, "unused")


if __name__ == "__main__":
    unittest.main()

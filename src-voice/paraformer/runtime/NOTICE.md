# Official implementation references

The Rust frontend and streaming orchestration in this directory were written for
CowType with reference to the MIT-licensed FunASR implementation. ONNX Runtime
executes the neural network; no weights or proprietary inference implementation
are included.

Reference project: https://github.com/modelscope/FunASR

Source files inspected on 2026-11-18, identified by Git blob SHA:

- `runtime/python/onnxruntime/funasr_onnx/paraformer_online_bin.py`
  `4d353b7336c399b4491fcf0b0ea703960bc81cfc`
- `runtime/python/onnxruntime/funasr_onnx/utils/frontend.py`
  `4218718b6c3731c4ae93fb9357aabe5c847f2011`
- `runtime/python/onnxruntime/funasr_onnx/utils/postprocess_utils.py`
  `82c334f855b7846708cb603fd0b97946889656e8`
- `funasr/models/paraformer_streaming/export_meta.py`
  `448a8266d7943350e94b08516258e888b51930a0`
- `funasr/models/paraformer/decoder.py`
  `f784e0914862a84686b74c09d77157abd1ac9fdf`

The upstream copyright and permission notice are preserved in `LICENSE.FunASR`.
This covers the referenced implementation, not every model available through
FunASR. The user must obtain and retain the license of their chosen model.

The runtime implements the official two-graph ONNX interface with `[5,10,5]`
overlap chunks. It does not use the separate PyTorch cache interface. Finalization
first flushes pending LFR frames, then the outstanding overlap center, and finally
CIF tail mass exactly once. In a split final block, the second inference starts
at the next left-context boundary, avoiding replay of previously consumed center
frames. A session owns its frontend, CIF and FSMN caches; only graph sessions and
immutable model metadata are shared.

Compilation alone does not establish numeric equivalence to Kaldi/FunASR or
recognition quality; deployments must validate those properties separately.

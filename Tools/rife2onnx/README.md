# rife2onnx

Converts Practical-RIFE PyTorch models to the ONNX format used by the frame
interpolation feature of MPC Video Renderer. The renderer itself never needs
Python: it converts ONNX to a TensorRT engine on its own.

## Ready-made ONNX files

The ONNX models shipped with [vs-mlrt](https://github.com/AmusementClub/vs-mlrt/releases)
(`models.*.7z`, directory `rife/`, e.g. `rife_v4.6.onnx`, `rife_v4.25.onnx`) and
the ones installed by SVP 4 (`C:\Program Files\SVP 4\rife\models\rife\`) can be
used directly, no conversion needed. Files from the `rife_v2/` directory are a
different interface and are not supported.

## Converting a Practical-RIFE model

1. Download a model archive from the list in the
   [Practical-RIFE](https://github.com/hzwer/Practical-RIFE) README (for example
   4.25 or 4.6) and extract it. The directory contains `flownet.pkl`,
   `IFNet_HDv3.py` and possibly `refine.py`.
2. Create a Python environment with the requirements:

   ```
   python -m venv venv
   venv\Scripts\activate
   pip install -r requirements.txt
   ```

3. Export:

   ```
   python export_rife_onnx.py --model-dir C:\path\to\train_log --output rife_v4.25.onnx --check
   ```

   `--check` runs the exported model with onnxruntime and compares it with
   PyTorch. `--ensemble` exports the ensemble variant for versions that support
   it (slower, slightly better quality). Name the file so that the version is
   recognizable (`rife_v4.25_lite.onnx` etc.): the renderer derives the tensor
   alignment from the name (128 for 4.25 lite, 64 otherwise).

4. Select the `.onnx` file on the "Frame interpolation" page of the renderer
   settings. The first playback at a new resolution builds a TensorRT engine
   (about one to three minutes), later starts use the cache in
   `%LOCALAPPDATA%\MPC-VR\trt`.

## How the export works

`IFNet_HDv3.py` from the model archive is imported unchanged. Only
`model.warplayer.warp` is replaced by a version that takes the sampling grid
from the network input (channels 7-10) instead of computing it from tensor
shapes, and the training-only imports (`model.loss`) are stubbed. A small
wrapper module splits the 11-channel input, calls `IFNet.forward()` and returns
the last merged frame. See the docstring in `export_rife_onnx.py` for the
channel layout.

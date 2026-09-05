#!/usr/bin/env python3
"""Export a Practical-RIFE PyTorch model (flownet.pkl + IFNet_HDv3.py) to ONNX.

The resulting ONNX uses the vs-mlrt "rife v1" interface that MPC Video Renderer
consumes: one float32 input ``input`` of shape [1, 11, H, W] and one output
``output`` of shape [1, 3, H, W] (H and W dynamic). Input channels:

    0-2  first frame RGB in 0..1
    3-5  second frame RGB in 0..1
    6    timestep plane (constant t in (0, 1))
    7    horizontal grid  2*x/(W-1) - 1
    8    vertical grid    2*y/(H-1) - 1
    9    constant 2/(W-1)
    10   constant 2/(H-1)

The grid channels replace the grid that Practical-RIFE's warplayer.py computes
internally, which keeps the graph free of shape-dependent constants.

Usage:
    python export_rife_onnx.py --model-dir path/to/train_log --output rife_v4.25.onnx

``--model-dir`` is the extracted model archive from
https://github.com/hzwer/Practical-RIFE (it contains flownet.pkl and
IFNet_HDv3.py, some versions also refine.py). Requires torch and onnx;
onnxsim and onnxruntime are optional.
"""

from __future__ import annotations

import argparse
import importlib.util
import inspect
import sys
import types
from pathlib import Path

import torch
import torch.nn as nn
import torch.nn.functional as F


# --------------------------------------------------------------------------- #
# Replacement for model/warplayer.py: warp with an externally supplied grid.
# --------------------------------------------------------------------------- #

class _GridState:
    grid: torch.Tensor | None = None       # [1, 2, H, W]  (horizontal, vertical)
    multiplier: torch.Tensor | None = None  # [1, 2, 1, 1]  (2/(W-1), 2/(H-1))


def grid_warp(ten_input: torch.Tensor, ten_flow: torch.Tensor) -> torch.Tensor:
    """Same semantics as Practical-RIFE warp(): flow in pixels, border padding."""
    grid = _GridState.grid
    multiplier = _GridState.multiplier
    if grid is None or multiplier is None:
        raise RuntimeError("grid_warp called outside of the export wrapper")
    if ten_flow.shape[-2:] != grid.shape[-2:]:
        # Some model versions warp feature maps at a reduced resolution.
        scale_h = (ten_flow.shape[-2] - 1) / (grid.shape[-2] - 1)
        scale_w = (ten_flow.shape[-1] - 1) / (grid.shape[-1] - 1)
        grid = F.interpolate(grid, size=ten_flow.shape[-2:], mode="bilinear", align_corners=True)
        multiplier = multiplier * torch.tensor([1.0 / scale_w, 1.0 / scale_h], dtype=multiplier.dtype,
                                               device=multiplier.device).view(1, 2, 1, 1)
    g = (grid + ten_flow * multiplier).permute(0, 2, 3, 1)
    return F.grid_sample(ten_input, g, mode="bilinear", padding_mode="border", align_corners=True)


def _make_stub_module(name: str, **attrs) -> types.ModuleType:
    module = types.ModuleType(name)
    for key, value in attrs.items():
        setattr(module, key, value)
    return module


class _AnyStub(nn.Module):
    """Stands in for training-only classes (losses) imported by some model files."""

    def __init__(self, *args, **kwargs):
        super().__init__()

    def forward(self, *args, **kwargs):
        raise RuntimeError("training-only stub called during export")


def install_model_packages(model_dir: Path) -> None:
    """Registers fake ``model`` / ``train_log`` packages so IFNet_HDv3.py imports resolve.

    The model files import ``from model.warplayer import warp`` (repository layout) or
    ``from train_log.refine import *`` (archive layout). Both package names are mapped to
    the same replacement modules; refine.py is taken from the model directory when present.
    """
    warplayer = _make_stub_module("warplayer", warp=grid_warp)
    loss = _make_stub_module("loss", EPE=_AnyStub, Ternary=_AnyStub, SOBEL=_AnyStub,
                             LapLoss=_AnyStub, VGGPerceptualLoss=_AnyStub, MeanShift=_AnyStub)

    for package_name in ("model", "train_log"):
        package = types.ModuleType(package_name)
        package.__path__ = [str(model_dir)]  # allows ``from train_log.IFNet_HDv3 import ...`` as well
        sys.modules[package_name] = package
        sys.modules[f"{package_name}.warplayer"] = warplayer
        sys.modules[f"{package_name}.loss"] = loss
        setattr(package, "warplayer", warplayer)
        setattr(package, "loss", loss)

    refine_path = model_dir / "refine.py"
    if refine_path.exists():
        spec = importlib.util.spec_from_file_location("refine", refine_path)
        refine = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(refine)
        for package_name in ("model", "train_log"):
            sys.modules[f"{package_name}.refine"] = refine
            setattr(sys.modules[package_name], "refine", refine)


def load_ifnet_class(model_dir: Path):
    ifnet_path = model_dir / "IFNet_HDv3.py"
    if not ifnet_path.exists():
        raise FileNotFoundError(f"{ifnet_path} not found")
    spec = importlib.util.spec_from_file_location("IFNet_HDv3", ifnet_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules["IFNet_HDv3"] = module
    spec.loader.exec_module(module)
    if not hasattr(module, "IFNet"):
        raise RuntimeError("IFNet class not found in IFNet_HDv3.py")
    return module.IFNet


def load_weights(flownet: nn.Module, pkl_path: Path) -> None:
    state = torch.load(pkl_path, map_location="cpu", weights_only=False)
    if isinstance(state, dict) and "state_dict" in state and isinstance(state["state_dict"], dict):
        state = state["state_dict"]
    converted = {}
    for key, value in state.items():
        if key.startswith("module."):
            key = key[len("module."):]
        converted[key] = value
    missing, unexpected = flownet.load_state_dict(converted, strict=False)
    missing = [k for k in missing if not k.startswith("teacher") and "caltime" not in k]
    unexpected = [k for k in unexpected if not k.startswith("teacher") and "caltime" not in k]
    if missing:
        print("missing keys:", ", ".join(missing[:10]), "..." if len(missing) > 10 else "")
    if unexpected:
        print("unexpected keys:", ", ".join(unexpected[:10]), "..." if len(unexpected) > 10 else "")
    if missing or unexpected:
        blocks = [k for k in missing + unexpected if k.startswith("block")]
        if blocks:
            raise RuntimeError("flownet.pkl does not match IFNet_HDv3.py (block weights differ)")


def count_blocks(flownet: nn.Module) -> int:
    count = 0
    while hasattr(flownet, f"block{count}"):
        count += 1
    if count == 0:
        raise RuntimeError("IFNet has no block0..blockN attributes")
    return count


class RifeExportWrapper(nn.Module):
    """[1, 11, H, W] -> [1, 3, H, W] around Practical-RIFE's IFNet.forward()."""

    def __init__(self, flownet: nn.Module, scale: float, ensemble: bool):
        super().__init__()
        self.flownet = flownet
        blocks = count_blocks(flownet)
        self.scale_list = [(2 ** (blocks - 1 - i)) / scale for i in range(blocks)]
        params = inspect.signature(flownet.forward).parameters
        self.kwargs = {}
        if "training" in params:
            self.kwargs["training"] = False
        if "fastmode" in params:
            self.kwargs["fastmode"] = True
        if "ensemble" in params:
            self.kwargs["ensemble"] = ensemble
        elif ensemble:
            raise RuntimeError("this model version does not support ensemble")
        self.accepts_scale_list = "scale_list" in params or "scale" in params
        self.scale_arg = "scale_list" if "scale_list" in params else "scale"

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        img0 = x[:, 0:3]
        img1 = x[:, 3:6]
        # Practical-RIFE versions that accept a tensor timestep expect a
        # [N, 1, 1, 1] value and expand it to the frame internally.  The
        # renderer still supplies channel 6 as a full plane, so take its
        # first value here instead of letting IFNet repeat an HxW tensor to
        # H^2xW^2.
        timestep = x[:, 6:7, :1, :1]
        _GridState.grid = x[:, 7:9]
        _GridState.multiplier = x[:, 9:11, :1, :1]

        kwargs = dict(self.kwargs)
        if self.accepts_scale_list:
            kwargs[self.scale_arg] = self.scale_list
        result = self.flownet(torch.cat((img0, img1), 1), timestep, **kwargs)

        merged = _pick_merged(result)
        _GridState.grid = None
        _GridState.multiplier = None
        return merged.clamp(0.0, 1.0)


def _pick_merged(result) -> torch.Tensor:
    """IFNet.forward returns (flow_list, mask, merged[, ...]) in most versions; find the frame list."""
    if isinstance(result, torch.Tensor):
        return result
    candidates = []
    for item in result:
        if isinstance(item, (list, tuple)) and item and isinstance(item[-1], torch.Tensor) and item[-1].shape[1] == 3:
            candidates.append(item[-1])
    if not candidates:
        for item in result:
            if isinstance(item, torch.Tensor) and item.dim() == 4 and item.shape[1] == 3:
                candidates.append(item)
    if not candidates:
        raise RuntimeError("could not find the merged frame in the IFNet output")
    return candidates[-1]


def export(args: argparse.Namespace) -> None:
    model_dir = Path(args.model_dir).resolve()
    install_model_packages(model_dir)
    sys.path.insert(0, str(model_dir))

    ifnet_cls = load_ifnet_class(model_dir)
    flownet = ifnet_cls()
    load_weights(flownet, model_dir / "flownet.pkl")
    flownet.eval()

    wrapper = RifeExportWrapper(flownet, args.scale, args.ensemble).eval()

    h, w = args.height, args.width
    dummy = make_input(h, w)
    with torch.no_grad():
        reference = wrapper(dummy)
    print(f"blocks: {count_blocks(flownet)}, scale_list: {wrapper.scale_list}, output: {tuple(reference.shape)}")

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    export_kwargs = {}
    if "dynamo" in inspect.signature(torch.onnx.export).parameters:
        export_kwargs["dynamo"] = False  # the TorchScript exporter handles the dynamic grid_sample graph
    with torch.no_grad():
        torch.onnx.export(
            wrapper,
            dummy,
            str(output),
            opset_version=args.opset,
            input_names=["input"],
            output_names=["output"],
            dynamic_axes={"input": {2: "height", 3: "width"}, "output": {2: "height", 3: "width"}},
            do_constant_folding=True,
            **export_kwargs,
        )
    print(f"written {output}")

    import onnx

    model = onnx.load(str(output))
    onnx.checker.check_model(model)
    ops = sorted({node.op_type for node in model.graph.node})
    print("ops:", ", ".join(ops))

    if not args.no_simplify:
        try:
            import onnxsim

            simplified, ok = onnxsim.simplify(model)
            if ok:
                onnx.save(simplified, str(output))
                print("simplified with onnxsim")
            else:
                print("onnxsim could not validate the simplified model, keeping the original")
        except ImportError:
            print("onnxsim not installed, skipping simplification")

    if args.check:
        check_with_onnxruntime(output, dummy, reference)


def make_input(h: int, w: int, timestep: float = 0.5) -> torch.Tensor:
    torch.manual_seed(0)
    img0 = torch.rand(1, 3, h, w)
    img1 = (img0 + 0.05 * torch.randn(1, 3, h, w)).clamp(0, 1)
    t = torch.full((1, 1, h, w), timestep)
    xs = torch.linspace(-1.0, 1.0, w).view(1, 1, 1, w).expand(1, 1, h, w)
    ys = torch.linspace(-1.0, 1.0, h).view(1, 1, h, 1).expand(1, 1, h, w)
    mul_w = torch.full((1, 1, h, w), 2.0 / (w - 1))
    mul_h = torch.full((1, 1, h, w), 2.0 / (h - 1))
    return torch.cat((img0, img1, t, xs, ys, mul_w, mul_h), 1)


def check_with_onnxruntime(output: Path, dummy: torch.Tensor, reference: torch.Tensor) -> None:
    try:
        import numpy as np
        import onnxruntime as ort
    except ImportError:
        print("onnxruntime not installed, skipping the check")
        return
    session = ort.InferenceSession(str(output), providers=["CPUExecutionProvider"])
    result = session.run(["output"], {"input": dummy.numpy()})[0]
    diff = np.abs(result - reference.numpy())
    print(f"onnxruntime vs torch: max abs diff {diff.max():.5f}, mean {diff.mean():.6f}")
    # a second size proves that the dynamic axes work
    other = make_input(dummy.shape[2] + 64, dummy.shape[3] - 64)
    result = session.run(["output"], {"input": other.numpy()})[0]
    print(f"dynamic size check: input {tuple(other.shape)} -> output {result.shape}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model-dir", required=True, help="directory with flownet.pkl and IFNet_HDv3.py")
    parser.add_argument("--output", required=True, help="output .onnx path")
    parser.add_argument("--opset", type=int, default=16, help="ONNX opset (GridSample needs 16+)")
    parser.add_argument("--scale", type=float, default=1.0, help="flow scale, 1.0 = native (0.5 for 4K)")
    parser.add_argument("--ensemble", action="store_true", help="export the ensemble variant if supported")
    parser.add_argument("--height", type=int, default=320, help="dummy input height (multiple of 64)")
    parser.add_argument("--width", type=int, default=576, help="dummy input width (multiple of 64)")
    parser.add_argument("--no-simplify", action="store_true", help="do not run onnxsim")
    parser.add_argument("--check", action="store_true", help="compare with onnxruntime after export")
    args = parser.parse_args()
    if args.height % 32 or args.width % 32:
        parser.error("--height and --width must be multiples of 32")
    export(args)


if __name__ == "__main__":
    main()

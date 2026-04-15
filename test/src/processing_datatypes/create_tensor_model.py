#!/usr/bin/env python3
"""
Generate a small MLP model for generic tensor vector processing (1D).
"""

import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TensorMLP(nn.Module):
    def __init__(self, input_size=64, hidden_size=32, output_size=64):
        super().__init__()
        self.fc1 = nn.Linear(input_size, hidden_size)
        self.fc2 = nn.Linear(hidden_size, output_size)
        self.relu = nn.ReLU()

    def forward(self, x):
        x = self.relu(self.fc1(x))
        x = self.fc2(x)
        return x


def main():
    parser = argparse.ArgumentParser(description="Create a tiny tensor ONNX model and sample input.")
    parser.add_argument("--width", type=int, default=256, help="Input width (total elements)")
    parser.add_argument("--patch", type=int, default=64, help="Patch length (block_len)")
    parser.add_argument("--stride", type=int, default=32, help="Stride in elements")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="tensor_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="tensor_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="tensor_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="tensor_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    rng = np.random.default_rng(0)

    model = TensorMLP(args.patch, args.patch // 2, args.patch).eval()

    test_input = rng.normal(size=(args.width,)).astype(np.float32)
    raw_path = os.path.join(args.out_dir, args.raw)
    test_input.tofile(raw_path)

    dummy_input = torch.randn(1, args.patch)
    onnx_path = os.path.join(args.out_dir, args.onnx)
    torch.onnx.export(
        model,
        dummy_input,
        onnx_path,
        export_params=True,
        opset_version=11,
        do_constant_folding=True,
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch_size"}, "output": {0: "batch_size"}}
    )

    output_accum = np.zeros((args.width,), dtype=np.float32)
    weight_accum = np.zeros((args.width,), dtype=np.float32)

    with torch.no_grad():
        starts = list(range(0, max(1, args.width - args.patch + 1), max(1, args.stride)))
        if not starts or starts[-1] != args.width - args.patch:
            if args.width > args.patch:
                starts.append(args.width - args.patch)
            else:
                starts = [0]

        for start in starts:
            patch = np.zeros((args.patch,), dtype=np.float32)
            end_pos = min(start + args.patch, args.width)
            patch[:end_pos - start] = test_input[start:end_pos]

            patch_tensor = torch.from_numpy(patch).unsqueeze(0)
            patch_output = model(patch_tensor).squeeze(0).numpy()

            out_start = start
            for i in range(args.patch):
                dst_pos = out_start + i
                if dst_pos >= args.width:
                    break
                output_accum[dst_pos] += patch_output[i]
                weight_accum[dst_pos] += 1.0

    reference_output = np.divide(output_accum, weight_accum, where=weight_accum > 0)
    ref_path = os.path.join(args.out_dir, args.ref)
    reference_output.tofile(ref_path)

    pt_path = os.path.join(args.out_dir, args.pt)
    torch.save(model.state_dict(), pt_path)

    print(f"Wrote ONNX model: {onnx_path}")
    print(f"Wrote raw input: {raw_path} (length {args.width})")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

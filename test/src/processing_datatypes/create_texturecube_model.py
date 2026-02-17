#!/usr/bin/env python3
"""
Generate a tiny Conv2D model for textureCube processing and reference outputs.
"""

import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyCubeNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Conv2d(3, 4, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.Conv2d(4, 3, kernel_size=3, padding=1)
        )

    def forward(self, x):
        return self.net(x)


def main():
    parser = argparse.ArgumentParser(description="Create a tiny textureCube ONNX model and sample input.")
    parser.add_argument("--width", type=int, default=64, help="Face width")
    parser.add_argument("--height", type=int, default=64, help="Face height")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="texturecube_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="texturecube_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="texturecube_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="texturecube_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    rng = np.random.default_rng(0)

    model = TinyCubeNet().eval()

    onnx_path = os.path.join(args.out_dir, args.onnx)
    dummy = torch.randn(1, 3, args.height, args.width, dtype=torch.float32)
    torch.onnx.export(
        model,
        dummy,
        onnx_path,
        input_names=["input"],
        output_names=["output"],
        opset_version=11
    )

    faces = []
    for _ in range(6):
        face = rng.integers(0, 256, size=(args.height, args.width, 3), dtype=np.uint8)
        faces.append(face)

    raw_path = os.path.join(args.out_dir, args.raw)
    with open(raw_path, "wb") as f:
        for face in faces:
            f.write(face.tobytes())

    outputs = []
    with torch.no_grad():
        for face in faces:
            face_tensor = torch.from_numpy(face.astype(np.float32) / 1.0)
            face_tensor = face_tensor.permute(2, 0, 1).unsqueeze(0)
            output = model(face_tensor).cpu().numpy().astype(np.float32)
            outputs.append(output)

    ref_path = os.path.join(args.out_dir, args.ref)
    with open(ref_path, "wb") as f:
        for output in outputs:
            f.write(output.tobytes())

    pt_path = os.path.join(args.out_dir, args.pt)
    torch.save(model.state_dict(), pt_path)

    print(f"Wrote ONNX model: {onnx_path}")
    print(f"Wrote raw input: {raw_path} (6 faces)")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

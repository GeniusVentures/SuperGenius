import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyConv1D(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Conv1d(1, 4, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.Conv1d(4, 2, kernel_size=3, padding=1)
        )

    def forward(self, x):
        return self.net(x)


def main():
    parser = argparse.ArgumentParser(description="Create a tiny texture1D ONNX model and sample input.")
    parser.add_argument("--length", type=int, default=256, help="Input length")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="texture1d_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="texture1d_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="texture1d_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="texture1d_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    model = TinyConv1D().eval()
    dummy = torch.randn(1, 1, args.length, dtype=torch.float32)

    onnx_path = os.path.join(args.out_dir, args.onnx)
    torch.onnx.export(
        model,
        dummy,
        onnx_path,
        input_names=["input"],
        output_names=["output"],
        opset_version=11
    )

    raw_path = os.path.join(args.out_dir, args.raw)
    np_input = dummy.numpy().astype(np.float32).reshape(-1)
    np_input.tofile(raw_path)

    with torch.no_grad():
        output = model(dummy).cpu().numpy().astype(np.float32).reshape(-1)

    ref_path = os.path.join(args.out_dir, args.ref)
    output.tofile(ref_path)

    pt_path = os.path.join(args.out_dir, args.pt)
    torch.save(model.state_dict(), pt_path)

    print(f"Wrote ONNX model: {onnx_path}")
    print(f"Wrote raw input: {raw_path} (length {args.length})")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

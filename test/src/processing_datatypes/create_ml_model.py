import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyMLPNet(nn.Module):
    """Small multi-layer perceptron for ML processing."""
    def __init__(self, dim):
        super().__init__()
        self.fc1 = nn.Linear(dim, dim * 2)
        self.act1 = nn.ReLU()
        self.fc2 = nn.Linear(dim * 2, dim)
        self.act2 = nn.ReLU()
        self.fc3 = nn.Linear(dim, dim)

    def forward(self, x):
        x = x.float()
        x = self.fc1(x)
        x = self.act1(x)
        x = self.fc2(x)
        x = self.act2(x)
        x = self.fc3(x)
        return x


def main():
    parser = argparse.ArgumentParser(description="Create a tiny ML ONNX model and sample input.")
    parser.add_argument("--dim", type=int, default=64, help="Input dimension")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="ml_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="ml_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="ml_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="ml_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    model = TinyMLPNet(args.dim).eval()

    # Deterministic float32 pattern
    rng = np.random.RandomState(42)
    input_data = rng.randn(args.dim).astype(np.float32)
    dummy = torch.from_numpy(input_data).reshape(1, args.dim)

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
    input_data.tofile(raw_path)

    with torch.no_grad():
        output = model(dummy).cpu().numpy().astype(np.float32).reshape(-1)

    ref_path = os.path.join(args.out_dir, args.ref)
    output.tofile(ref_path)

    pt_path = os.path.join(args.out_dir, args.pt)
    torch.save(model.state_dict(), pt_path)

    print(f"Wrote ONNX model: {onnx_path}")
    print(f"Wrote raw input: {raw_path} (dim {args.dim})")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

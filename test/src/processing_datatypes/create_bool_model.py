import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyBoolNet(nn.Module):
    def __init__(self, length):
        super().__init__()
        self.fc1 = nn.Linear(length, length)
        self.act = nn.ReLU()
        self.fc2 = nn.Linear(length, length)

    def forward(self, x):
        return self.fc2(self.act(self.fc1(x)))


def main():
    parser = argparse.ArgumentParser(description="Create a tiny bool ONNX model and sample input.")
    parser.add_argument("--length", type=int, default=8, help="Input length")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="bool_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="bool_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="bool_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="bool_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    model = TinyBoolNet(args.length).eval()

    # Create a deterministic 0/1 pattern
    input_bits = np.array([0, 1, 1, 0, 1, 0, 0, 1], dtype=np.float32)
    if args.length != input_bits.size:
        input_bits = np.resize(input_bits, args.length).astype(np.float32)

    dummy = torch.from_numpy(input_bits).reshape(1, args.length)

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
    input_bits.astype(np.float32).tofile(raw_path)

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

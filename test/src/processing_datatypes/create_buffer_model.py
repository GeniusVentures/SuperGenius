import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyBufferNet(nn.Module):
    def __init__(self, length):
        super().__init__()
        self.fc1 = nn.Linear(length, length)
        self.act = nn.ReLU()
        self.fc2 = nn.Linear(length, length)

    def forward(self, x):
        x = x.float()
        return self.fc2(self.act(self.fc1(x)))


def main():
    parser = argparse.ArgumentParser(description="Create a tiny buffer ONNX model and sample input.")
    parser.add_argument("--length", type=int, default=16, help="Input length")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="buffer_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="buffer_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="buffer_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="buffer_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    model = TinyBufferNet(args.length).eval()

    # Deterministic int8 pattern
    input_bytes = np.array([0, 1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, 13, -14, 15], dtype=np.int8)
    if args.length != input_bytes.size:
        input_bytes = np.resize(input_bytes, args.length).astype(np.int8)

    dummy = torch.from_numpy(input_bytes.astype(np.float32)).reshape(1, args.length)

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
    input_bytes.tofile(raw_path)

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

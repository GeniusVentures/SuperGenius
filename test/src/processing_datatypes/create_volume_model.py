import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyVolumeNet(nn.Module):
    """3D convolution net for volume processing."""
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv3d(1, 4, kernel_size=3, padding=1)
        self.act = nn.ReLU()
        self.conv2 = nn.Conv3d(4, 1, kernel_size=3, padding=1)

    def forward(self, x):
        # x shape: (batch, 1, D, H, W)
        x = self.conv1(x)
        x = self.act(x)
        x = self.conv2(x)
        return x


def main():
    parser = argparse.ArgumentParser(description="Create a tiny volume ONNX model and sample input.")
    parser.add_argument("--depth", type=int, default=16, help="Volume depth")
    parser.add_argument("--height", type=int, default=16, help="Volume height")
    parser.add_argument("--width", type=int, default=16, help="Volume width")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="volume_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="volume_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="volume_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="volume_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    model = TinyVolumeNet().eval()

    # Deterministic float32 pattern for 3D volume
    rng = np.random.RandomState(42)
    input_data = rng.randn(1, args.depth, args.height, args.width).astype(np.float32)
    dummy = torch.from_numpy(input_data).reshape(1, 1, args.depth, args.height, args.width)

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
    print(f"Wrote raw input: {raw_path} ({args.depth}x{args.height}x{args.width})")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyImageNet(nn.Module):
    """2D convolution net for image processing."""
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 8, kernel_size=3, padding=1)
        self.act = nn.ReLU()
        self.conv2 = nn.Conv2d(8, 3, kernel_size=3, padding=1)

    def forward(self, x):
        # x shape: (batch, 3, H, W)
        x = self.conv1(x)
        x = self.act(x)
        x = self.conv2(x)
        return x


def main():
    parser = argparse.ArgumentParser(description="Create a tiny image ONNX model and sample input.")
    parser.add_argument("--width", type=int, default=32, help="Image width")
    parser.add_argument("--height", type=int, default=32, help="Image height")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="image_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="image_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="image_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="image_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    model = TinyImageNet().eval()

    # Deterministic float32 pattern for RGB image
    rng = np.random.RandomState(42)
    input_data = rng.randn(3, args.height, args.width).astype(np.float32)
    dummy = torch.from_numpy(input_data).reshape(1, 3, args.height, args.width)

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
    print(f"Wrote raw input: {raw_path} ({args.height}x{args.width}x3)")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

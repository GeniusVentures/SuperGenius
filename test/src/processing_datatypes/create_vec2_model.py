#!/usr/bin/env python3
"""
Create a tiny vec2 ONNX model and reference output for testing.
Generates vec2 input, runs PyTorch inference, and exports to ONNX.
"""

import torch
import torch.nn as nn
import torch.onnx
import numpy as np
import argparse


class TinyVec2Net(nn.Module):
    """Simple 1D conv-based model for vec2 processing"""
    def __init__(self):
        super(TinyVec2Net, self).__init__()
        # Input: (1, 2, N) where 2 is vec2 components, N is vector count
        self.conv1 = nn.Conv1d(2, 8, kernel_size=3, padding=1)
        self.relu = nn.ReLU()
        self.conv2 = nn.Conv1d(8, 1, kernel_size=3, padding=1)

    def forward(self, x):
        x = self.conv1(x)
        x = self.relu(x)
        x = self.conv2(x)
        return x.view(-1)  # Flatten to 1D output


def main():
    parser = argparse.ArgumentParser(description="Create a tiny vec2 ONNX model and reference output.")
    parser.add_argument("--vectors", type=int, default=16, help="Number of vec2 entries")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for reproducibility")
    parser.add_argument("--onnx", default="vec2_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="vec2_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="vec2_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="vec2_output_pt.raw", help="Reference output filename")

    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    # Create model
    model = TinyVec2Net().eval()
    print(f"Model created: {model}")

    # Create input: vec2 array (vectors, 2)
    input_data = np.random.randn(args.vectors, 2).astype(np.float32)
    print(f"Input shape (vectors, components): {input_data.shape}")
    print(f"Input range: [{input_data.min():.4f}, {input_data.max():.4f}]")

    # Save raw input
    with open(args.raw, "wb") as f:
        input_data.astype(np.float32).tofile(f)
    print(f"Saved raw input to {args.raw}")

    # Convert to model format: (batch=1, channels=2, length=vectors)
    model_input = torch.from_numpy(input_data.T[np.newaxis, :, :])  # (1, 2, vectors)
    print(f"Model input tensor shape: {model_input.shape}")

    # Run inference
    with torch.no_grad():
        output = model(model_input)
    output_np = output.cpu().numpy().astype(np.float32)
    print(f"Output shape: {output_np.shape}")
    print(f"Output range: [{output_np.min():.4f}, {output_np.max():.4f}]")

    # Save reference output
    with open(args.ref, "wb") as f:
        output_np.astype(np.float32).tofile(f)
    print(f"Saved reference output to {args.ref}")

    # Save model state
    torch.save(model.state_dict(), args.pt)
    print(f"Saved model state to {args.pt}")

    # Export to ONNX
    dummy_input = torch.randn(1, 2, args.vectors)
    torch.onnx.export(
        model,
        dummy_input,
        args.onnx,
        input_names=["input"],
        output_names=["output"],
        opset_version=13,
        do_constant_folding=True,
        verbose=False,
    )
    print(f"Exported ONNX model to {args.onnx}")
    print("\nConvert ONNX to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {args.onnx} --MNNModel {args.onnx.replace('.onnx', '.mnn')} --bizCode biz")
    print("\nStats:")
    print(f"  Input: min={input_data.min():.3f}, max={input_data.max():.3f}, mean={input_data.mean():.3f}")
    print(f"  Reference: min={output_np.min():.3f}, max={output_np.max():.3f}, mean={output_np.mean():.3f}")


if __name__ == "__main__":
    main()

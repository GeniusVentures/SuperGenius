import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyMat3Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Conv1d(9, 9, kernel_size=1),
            nn.ReLU(),
            nn.Conv1d(9, 9, kernel_size=1)
        )

    def forward(self, x):
        return self.net(x)


def main():
    parser = argparse.ArgumentParser(description="Create a tiny mat3 ONNX model and reference output.")
    parser.add_argument("--matrices", type=int, default=12, help="Number of mat3 entries")
    parser.add_argument("--patch", type=int, default=6, help="Patch length (block_len)")
    parser.add_argument("--stride", type=int, default=3, help="Stride in matrices")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="mat3_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="mat3_input.raw", help="Raw input filename")
    parser.add_argument("--pt", default="mat3_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="mat3_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    rng = np.random.default_rng(0)

    model = TinyMat3Net().eval()

    input_mats = rng.normal(size=(args.matrices, 9)).astype(np.float32)
    raw_path = os.path.join(args.out_dir, args.raw)
    input_mats.reshape(-1).tofile(raw_path)

    dummy = torch.randn(1, 9, args.patch, dtype=torch.float32)
    onnx_path = os.path.join(args.out_dir, args.onnx)
    torch.onnx.export(
        model,
        dummy,
        onnx_path,
        input_names=["input"],
        output_names=["output"],
        opset_version=11
    )

    output_accum = np.zeros((9, args.matrices), dtype=np.float32)
    weight_accum = np.zeros((args.matrices,), dtype=np.float32)

    with torch.no_grad():
        starts = list(range(0, max(1, args.matrices - args.patch + 1), max(1, args.stride)))
        if not starts or starts[-1] != args.matrices - args.patch:
            if args.matrices > args.patch:
                starts.append(args.matrices - args.patch)
            else:
                starts = [0]

        for start in starts:
            patch = np.zeros((9, args.patch), dtype=np.float32)
            for i in range(args.patch):
                idx = start + i
                if idx >= args.matrices:
                    break
                patch[:, i] = input_mats[idx, :]

            patch_tensor = torch.from_numpy(patch).unsqueeze(0)
            patch_output = model(patch_tensor).squeeze(0).cpu().numpy()

            for i in range(args.patch):
                out_index = start + i
                if out_index >= args.matrices:
                    break
                output_accum[:, out_index] += patch_output[:, i]
                weight_accum[out_index] += 1.0

    for i in range(args.matrices):
        if weight_accum[i] > 0.0:
            output_accum[:, i] /= weight_accum[i]

    ref_path = os.path.join(args.out_dir, args.ref)
    output_accum.reshape(-1).tofile(ref_path)

    pt_path = os.path.join(args.out_dir, args.pt)
    torch.save(model.state_dict(), pt_path)

    print(f"Wrote ONNX model: {onnx_path}")
    print(f"Wrote raw input: {raw_path} (matrices {args.matrices})")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

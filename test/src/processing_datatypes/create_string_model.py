import argparse
import os
import numpy as np

try:
    import torch
    import torch.nn as nn
except ImportError as exc:
    raise SystemExit("This script requires torch. Install with: pip install torch") from exc


class TinyStringEmbeddingNet(nn.Module):
    """Character-level embedding model — tiny (~100 params) for CI speed."""
    def __init__(self, vocab_size=32, embed_dim=8, seq_len=16):
        super().__init__()
        self.embedding = nn.Embedding(vocab_size, embed_dim)
        self.fc = nn.Linear(seq_len * embed_dim, 4)
        self.act = nn.ReLU()

    def forward(self, x):
        # x shape: (batch, seq_len) — int64 token ids
        x = self.embedding(x)              # (batch, seq_len, embed_dim)
        x = x.reshape(x.size(0), -1)       # (batch, seq_len * embed_dim)
        x = self.fc(x)
        x = self.act(x)
        return x


def main():
    parser = argparse.ArgumentParser(description="Create a tiny string embedding ONNX model and sample input.")
    parser.add_argument("--vocab-size", type=int, default=32, help="Vocabulary size")
    parser.add_argument("--embed-dim", type=int, default=8, help="Embedding dimension")
    parser.add_argument("--seq-len", type=int, default=16, help="Sequence length")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--onnx", default="string_tiny.onnx", help="ONNX filename")
    parser.add_argument("--raw", default="string_input.raw", help="Raw input filename (int32 tokens)")
    parser.add_argument("--pt", default="string_tiny_state.pt", help="Torch state dict filename")
    parser.add_argument("--ref", default="string_output_pt.raw", help="Reference output filename")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    torch.manual_seed(0)
    model = TinyStringEmbeddingNet(args.vocab_size, args.embed_dim, args.seq_len).eval()

    # Deterministic int32 token pattern
    rng = np.random.RandomState(42)
    input_tokens = rng.randint(0, args.vocab_size, size=args.seq_len, dtype=np.int32)
    dummy = torch.from_numpy(input_tokens.astype(np.int64)).reshape(1, args.seq_len)

    onnx_path = os.path.join(args.out_dir, args.onnx)
    torch.onnx.export(
        model,
        dummy,
        onnx_path,
        input_names=["input"],
        output_names=["output"],
        opset_version=11,
        dynamic_axes={
            "input": {1: "seq_len"}
        }
    )

    raw_path = os.path.join(args.out_dir, args.raw)
    input_tokens.tofile(raw_path)

    with torch.no_grad():
        output = model(dummy).cpu().numpy().astype(np.float32).reshape(-1)

    ref_path = os.path.join(args.out_dir, args.ref)
    output.tofile(ref_path)

    pt_path = os.path.join(args.out_dir, args.pt)
    torch.save(model.state_dict(), pt_path)

    print(f"Wrote ONNX model: {onnx_path}")
    print(f"Wrote raw input (int32 tokens): {raw_path} (seq_len {args.seq_len})")
    print(f"Wrote reference output: {ref_path}")
    print(f"Wrote PyTorch state dict: {pt_path}")
    print("Convert to MNN with:")
    print(f"  MNNConvert -f ONNX --modelFile {onnx_path} --MNNModel {os.path.splitext(onnx_path)[0]}.mnn")


if __name__ == "__main__":
    main()

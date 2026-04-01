#!/usr/bin/env python3
"""
Generate a small MLP model for integer vector processing (1D).
This processes int32 input vectors with a simple feed-forward network.
"""

import torch
import torch.nn as nn
import numpy as np
import sys

class IntMLP(nn.Module):
    """Simple MLP for integer vector processing"""
    def __init__(self, input_size=64, hidden_size=32, output_size=64):
        super().__init__()
        self.fc1 = nn.Linear(input_size, hidden_size)
        self.fc2 = nn.Linear(hidden_size, output_size)
        self.relu = nn.ReLU()
        
    def forward(self, x):
        x = self.relu(self.fc1(x))
        x = self.fc2(x)
        return x

def main():
    print("Creating int MLP model...")
    
    # Model parameters
    input_size = 64
    hidden_size = 32
    output_size = 64
    
    # Create model
    model = IntMLP(input_size, hidden_size, output_size)
    model.eval()
    
    # Create dummy input (as float for model)
    dummy_input = torch.randn(1, input_size)
    
    # Test model
    with torch.no_grad():
        output = model(dummy_input)
    print(f"Model test: input shape {dummy_input.shape} -> output shape {output.shape}")
    
    # Export to ONNX
    onnx_path = "int_model.onnx"
    torch.onnx.export(
        model,
        dummy_input,
        onnx_path,
        export_params=True,
        opset_version=11,
        do_constant_folding=True,
        input_names=['input'],
        output_names=['output'],
        dynamic_axes={'input': {0: 'batch_size'}, 'output': {0: 'batch_size'}}
    )
    print(f"Saved ONNX model to {onnx_path}")
    
    # Generate test input (512 int32 values for sliding window test)
    test_width = 512
    test_input_int = np.random.randint(-100, 100, size=test_width, dtype=np.int32)
    test_input_bytes = test_input_int.tobytes()
    
    with open('int_input.bin', 'wb') as f:
        f.write(test_input_bytes)
    print(f"Saved test input ({test_width} int32s) to int_input.bin")
    
    # Convert to float for model inference
    test_input_float = test_input_int.astype(np.float32)
    
    # Generate reference output using sliding window with stride
    patch_width = input_size
    stride = patch_width // 2  # Default stride
    num_patches = ((test_width - patch_width) // stride) + 1
    output_width = (num_patches - 1) * stride + output_size
    
    print(f"Reference generation: {num_patches} patches, output width {output_width}")
    
    output_accum = np.zeros(output_width, dtype=np.float32)
    weight_accum = np.zeros(output_width, dtype=np.float32)
    
    with torch.no_grad():
        for p in range(num_patches):
            start_pos = p * stride
            end_pos = min(start_pos + patch_width, test_width)
            
            patch_input = np.zeros(patch_width, dtype=np.float32)
            patch_input[:end_pos - start_pos] = test_input_float[start_pos:end_pos]
            
            patch_tensor = torch.from_numpy(patch_input).unsqueeze(0)
            patch_output = model(patch_tensor).squeeze(0).numpy()
            
            out_start = start_pos
            for w in range(output_size):
                dst_pos = out_start + w
                if dst_pos < output_width:
                    output_accum[dst_pos] += patch_output[w]
                    weight_accum[dst_pos] += 1.0
    
    # Average overlapping regions
    reference_output = np.divide(output_accum, weight_accum, where=weight_accum > 0)
    reference_bytes = reference_output.tobytes()
    
    with open('int_reference.bin', 'wb') as f:
        f.write(reference_bytes)
    print(f"Saved reference output ({output_width} floats) to int_reference.bin")
    
    print("\nConvert ONNX to MNN with:")
    print("  MNNConvert -f ONNX --modelFile int_model.onnx --MNNModel int_model.mnn --bizCode biz")
    print("\nStats:")
    print(f"  Input (int32): min={test_input_int.min()}, max={test_input_int.max()}, mean={test_input_int.mean():.3f}")
    print(f"  Reference (float): min={reference_output.min():.3f}, max={reference_output.max():.3f}, mean={reference_output.mean():.3f}")

if __name__ == '__main__':
    main()

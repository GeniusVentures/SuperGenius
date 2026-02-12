import sys
from pathlib import Path

import nibabel as nib
import numpy as np


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: python convert_nii_to_raw.py <input_nii.gz> <output_raw>")
        return 1

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])

    if not input_path.exists():
        print(f"Input not found: {input_path}")
        return 1

    img = nib.load(str(input_path))
    data = img.get_fdata(dtype=np.float32)
    print(f"shape: {data.shape}, dtype: {data.dtype}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    data.tofile(str(output_path))
    print(f"wrote raw: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

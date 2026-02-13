import argparse
from pathlib import Path

import nibabel as nib
import numpy as np


def _load_with_monai(input_path: Path) -> np.ndarray:
    try:
        from monai.transforms import (
            Compose,
            LoadImaged,
            EnsureChannelFirstd,
            Orientationd,
            Spacingd,
            ScaleIntensityRanged,
            EnsureTyped,
        )
        import torch
    except Exception as exc:
        raise RuntimeError("monai is required for --monai: pip install monai") from exc

    preprocessing = Compose([
        LoadImaged(keys=["image"]),
        EnsureChannelFirstd(keys=["image"]),
        Orientationd(keys=["image"], axcodes="RAS"),
        Spacingd(keys=["image"], pixdim=[1.5, 1.5, 2.0], mode="bilinear"),
        ScaleIntensityRanged(keys=["image"], a_min=-57, a_max=164, b_min=0.0, b_max=1.0, clip=True),
        EnsureTyped(keys=["image"]),
    ])

    sample = preprocessing({"image": str(input_path)})
    data = sample["image"]
    if isinstance(data, torch.Tensor):
        data = data.cpu().numpy()
    return np.asarray(data, dtype=np.float32)


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert NIfTI to raw float32")
    parser.add_argument("input_nii", help="Path to input .nii or .nii.gz")
    parser.add_argument("output_raw", help="Path to output .raw")
    parser.add_argument("--monai", action="store_true", help="Apply MONAI preprocessing for spleen_ct_segmentation")
    args = parser.parse_args()

    input_path = Path(args.input_nii)
    output_path = Path(args.output_raw)

    if not input_path.exists():
        print(f"Input not found: {input_path}")
        return 1

    if args.monai:
        data = _load_with_monai(input_path)
        # MONAI output is typically (C, H, W, D). Drop channel and keep (H, W, D).
        if data.ndim == 4 and data.shape[0] == 1:
            data = data[0]
    else:
        img = nib.load(str(input_path))
        data = img.get_fdata(dtype=np.float32)

    print(f"shape: {data.shape}, dtype: {data.dtype}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    data.tofile(str(output_path))
    print(f"wrote raw: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

import argparse
from pathlib import Path

import numpy as np
import nibabel as nib
import torch

try:
    from monai.transforms import (
        Compose,
        LoadImaged,
        EnsureChannelFirstd,
        Orientationd,
        Spacingd,
        ScaleIntensityRanged,
        EnsureTyped,
        Invertd,
        AsDiscreted,
        Activationsd,
    )
    from monai.data import MetaTensor
except Exception as exc:
    raise RuntimeError("monai is required: pip install monai") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert stitched MNN logits to NIfTI using MONAI inverse transforms")
    parser.add_argument("--image", required=True, help="Path to original input .nii.gz")
    parser.add_argument("--logits", required=True, help="Path to stitched_logits.raw")
    parser.add_argument("--h", type=int, required=True, help="Height of preprocessed volume")
    parser.add_argument("--w", type=int, required=True, help="Width of preprocessed volume")
    parser.add_argument("--d", type=int, required=True, help="Depth of preprocessed volume")
    parser.add_argument("--out", required=True, help="Output .nii.gz path")
    parser.add_argument("--channels", type=int, default=2, help="Number of output channels")
    parser.add_argument("--no-invert", action="store_true", help="Skip Invertd and save in preprocessed space")
    args = parser.parse_args()

    logits_path = Path(args.logits)
    if not logits_path.exists():
        raise FileNotFoundError(f"Logits not found: {logits_path}")

    data = np.fromfile(str(logits_path), dtype=np.float32)
    expected = args.channels * args.h * args.w * args.d
    if data.size != expected:
        raise ValueError(f"Expected {expected} floats, got {data.size}")

    logits = data.reshape((args.channels, args.h, args.w, args.d))
    logits = torch.from_numpy(logits)

    preprocessing = Compose([
        LoadImaged(keys=["image"]),
        EnsureChannelFirstd(keys=["image"]),
        Orientationd(keys=["image"], axcodes="RAS"),
        Spacingd(keys=["image"], pixdim=[1.5, 1.5, 2.0], mode="bilinear"),
        ScaleIntensityRanged(keys=["image"], a_min=-57, a_max=164, b_min=0.0, b_max=1.0, clip=True),
        EnsureTyped(keys=["image"]),
    ])

    sample = preprocessing({"image": args.image})

    if args.no_invert:
        post = Compose([
            Activationsd(keys="pred", softmax=True),
            AsDiscreted(keys="pred", argmax=True),
        ])
    else:
        post = Compose([
            Activationsd(keys="pred", softmax=True),
            Invertd(keys="pred", transform=preprocessing, orig_keys="image", nearest_interp=False, to_tensor=True),
            AsDiscreted(keys="pred", argmax=True),
        ])

    image_meta = sample["image"]
    if isinstance(image_meta, MetaTensor):
        pred = MetaTensor(logits, meta=image_meta.meta)
    else:
        pred = logits

    output = post({"pred": pred, "image": image_meta})
    pred = output["pred"]
    if isinstance(pred, torch.Tensor):
        pred_np = pred.cpu().numpy().astype(np.uint8)
    else:
        pred_np = np.asarray(pred, dtype=np.uint8)

    if pred_np.ndim == 4 and pred_np.shape[0] == 1:
        pred_np = pred_np[0]

    print(f"Output shape: {pred_np.shape}")

    img = nib.load(args.image)
    if args.no_invert and isinstance(image_meta, MetaTensor) and "affine" in image_meta.meta:
        affine = image_meta.meta["affine"]
    else:
        affine = img.affine
    out_img = nib.Nifti1Image(pred_np, affine=affine)
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    nib.save(out_img, str(out_path))
    print(f"Wrote NIfTI: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

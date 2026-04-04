
#need to install exiftool to use the micasense python library. Download from https://exiftool.org/ and add to PATH. 
import os
import glob
import cv2
import numpy as np
from micasense.capture import Capture
import micasense.imageutils as imageutils


def align_layers_to_rgb(capture_prefix, output_filename="aligned_rgb.png", repo_folder=None, skip_alignment=False):
    if repo_folder is None:
        repo_folder = os.path.dirname(os.path.abspath(__file__))
    filelist = sorted(glob.glob(os.path.join(repo_folder, f"{capture_prefix}_*.tif")))
    
    if not filelist:
        raise FileNotFoundError(f"No TIFF files found for prefix '{capture_prefix}' in {repo_folder}")
    cap = Capture.from_filelist(filelist)
    
    if skip_alignment:
        im_aligned = np.dstack([img.radiance() for img in cap.images])
    else:
        match_index = 1
        max_alignment_iterations = 15
        warp_mode = cv2.MOTION_HOMOGRAPHY
        pyramid_levels = 1
        warp_matrices, alignment_pairs = imageutils.align_capture(
            cap,
            ref_index=match_index,
            max_iterations=max_alignment_iterations,
            warp_mode=warp_mode,
            pyramid_levels=pyramid_levels
        )
        cropped_dimensions, edges = imageutils.find_crop_bounds(
            cap, 
            warp_matrices, 
            warp_mode=warp_mode
        )
        img_type = "reflectance" if cap.images[0].has_reflectance() else "radiance"
        im_aligned = imageutils.aligned_capture(
            cap,
            warp_matrices,
            warp_mode,
            cropped_dimensions,
            match_index,
            img_type=img_type
        )
    
    blue = im_aligned[:, :, 0]
    green = im_aligned[:, :, 1]
    red = im_aligned[:, :, 2]
    rgb = np.dstack((red, green, blue))
    rgb = rgb.astype(np.float32)
    rgb_max = np.max(rgb)
    if rgb_max > 0:
        rgb /= rgb_max
    rgb = np.nan_to_num(rgb, nan=0.0, posinf=1.0, neginf=0.0)
    rgb = np.clip(rgb, 0, 1)
    gamma = 1 / 2.2
    rgb = np.power(rgb, gamma)
    rgb_8bit = (rgb * 255).clip(0, 255).astype(np.uint8)
    output_path = os.path.join(repo_folder, output_filename)
    cv2.imwrite(output_path, cv2.cvtColor(rgb_8bit, cv2.COLOR_RGB2BGR))
    if not os.path.exists(output_path):
        raise IOError(f"Failed to save image to {output_path}")
    return rgb_8bit


if __name__ == "__main__":
    capture_prefix = "IMG_0003"
    try:
        align_layers_to_rgb(capture_prefix, skip_alignment=True)
    except Exception as e:
        raise
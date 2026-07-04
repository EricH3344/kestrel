
#need to install exiftool to use the micasense python library. Download from https://exiftool.org/ and add to PATH. 
import os
import re
import glob
import shutil
import argparse
import cv2
import numpy as np

if not hasattr(np, "mat"):
    np.mat = np.asmatrix

from micasense.capture import Capture
import micasense.imageutils as imageutils

try:
    cv2.utils.logging.setLogLevel(cv2.utils.logging.LOG_LEVEL_ERROR)
except Exception:
    pass


def _ensure_exiftool_on_path():
    """MicaSense shells out to the 'exiftool' executable. If it isn't already on
    PATH (e.g. a terminal opened before exiftool was installed), try to locate it
    and prepend its folder so this process can find it without a shell restart."""
    if shutil.which("exiftool"):
        return
    candidates = []
    env_path = os.environ.get("EXIFTOOL_PATH")  # may be the exe or its folder
    if env_path:
        candidates.append(env_path if os.path.isdir(env_path) else os.path.dirname(env_path))
    candidates.append(os.path.join(os.path.expanduser("~"), "exiftool"))
    for folder in candidates:
        if folder and os.path.isfile(os.path.join(folder, "exiftool.exe")):
            os.environ["PATH"] = folder + os.pathsep + os.environ.get("PATH", "")
            return


MATCH_INDEX = 1                       # reference band for alignment (1 = green on RedEdge)
WARP_MODE = cv2.MOTION_HOMOGRAPHY
MAX_ALIGNMENT_ITERATIONS = 50         # ECC iterations; too few leaves residual band offset
PYRAMID_LEVELS = None                 # None -> micasense auto-picks from image size


def load_capture(capture_prefix, repo_folder):
    """Load a MicaSense Capture from its PREFIX_<band>.tif files."""
    _ensure_exiftool_on_path()  # reading capture metadata shells out to exiftool
    filelist = sorted(glob.glob(os.path.join(repo_folder, f"{capture_prefix}_*.tif")))
    if not filelist:
        raise FileNotFoundError(f"No TIFF files found for prefix '{capture_prefix}' in {repo_folder}")
    return Capture.from_filelist(filelist)


def compute_alignment(cap):
    """Solve band-to-band registration for one capture. The result (warp matrices + crop)
    is reusable across every capture from the same camera, since the lens geometry is fixed."""
    warp_matrices, _ = imageutils.align_capture(
        cap,
        ref_index=MATCH_INDEX,
        max_iterations=MAX_ALIGNMENT_ITERATIONS,
        warp_mode=WARP_MODE,
        pyramid_levels=PYRAMID_LEVELS,
    )
    cropped_dimensions, _ = imageutils.find_crop_bounds(cap, warp_matrices, warp_mode=WARP_MODE)
    return warp_matrices, cropped_dimensions


def capture_to_rgb_stack(cap, alignment=None):
    """Return an (H, W, 3) float array in R,G,B order for the capture.
    If alignment (warp_matrices, cropped_dimensions) is given, bands are registered; otherwise
    the raw radiance bands are stacked as-is (fast, but misaligned)."""
    if alignment is None:
        im_aligned = np.dstack([img.radiance() for img in cap.images])
    else:
        warp_matrices, cropped_dimensions = alignment
        use_reflectance = all(img.horizontal_irradiance for img in cap.images)
        img_type = "reflectance" if use_reflectance else "radiance"
        im_aligned = imageutils.aligned_capture(
            cap, warp_matrices, WARP_MODE, cropped_dimensions, MATCH_INDEX, img_type=img_type
        )
    blue, green, red = im_aligned[:, :, 0], im_aligned[:, :, 1], im_aligned[:, :, 2]
    return np.dstack((red, green, blue))


def save_rgb_png(rgb, output_path):
    """Normalize, gamma-correct, and write an 8-bit RGB PNG."""
    rgb = rgb.astype(np.float32)
    rgb_max = np.max(rgb)
    if rgb_max > 0:
        rgb /= rgb_max
    rgb = np.nan_to_num(rgb, nan=0.0, posinf=1.0, neginf=0.0)
    rgb = np.clip(rgb, 0, 1)
    rgb = np.power(rgb, 1 / 2.2)
    rgb_8bit = (rgb * 255).clip(0, 255).astype(np.uint8)
    cv2.imwrite(output_path, cv2.cvtColor(rgb_8bit, cv2.COLOR_RGB2BGR))
    if not os.path.exists(output_path):
        raise IOError(f"Failed to save image to {output_path}")
    return rgb_8bit


def align_layers_to_rgb(capture_prefix, output_filename="aligned_rgb.png", repo_folder=None,
                        skip_alignment=False, alignment=None):
    """Process a single capture into an RGB PNG. If `alignment` is provided it is reused
    (fast); if omitted and not skipping, alignment is solved from this capture."""
    if repo_folder is None:
        repo_folder = os.path.dirname(os.path.abspath(__file__))
    cap = load_capture(capture_prefix, repo_folder)  # ensures exiftool is on PATH
    if not skip_alignment and alignment is None:
        alignment = compute_alignment(cap)
    rgb = capture_to_rgb_stack(cap, alignment=None if skip_alignment else alignment)
    return save_rgb_png(rgb, os.path.join(repo_folder, output_filename))


def find_capture_prefixes(repo_folder):
    """Return sorted unique capture prefixes (e.g. 'IMG_0003') found from PREFIX_<band>.tif files."""
    prefixes = set()
    for path in glob.glob(os.path.join(repo_folder, "*.tif")):
        match = re.match(r"^(.*)_\d+\.tif$", os.path.basename(path), re.IGNORECASE)
        if match:
            prefixes.add(match.group(1))
    return sorted(prefixes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Align MicaSense band TIFFs into an RGB PNG for each capture in a folder."
    )
    parser.add_argument(
        "folder", nargs="?", default=None,
        help="Folder containing PREFIX_<band>.tif files (default: this script's folder)",
    )
    parser.add_argument(
        "--prefix", default=None,
        help="Process only this capture prefix (e.g. IMG_0003); default: every capture found",
    )
    parser.add_argument(
        "--skip-align", action="store_true",
        help="Skip band alignment (fast preview, but RGB bands will be misregistered)",
    )
    parser.add_argument(
        "--ref", default=None,
        help="Capture prefix to compute alignment from (e.g. IMG_0100); default: auto-pick "
             "a mid-flight capture that converges. Reused for all captures.",
    )
    args = parser.parse_args()

    repo_folder = args.folder or os.path.dirname(os.path.abspath(__file__))
    prefixes = [args.prefix] if args.prefix else find_capture_prefixes(repo_folder)

    if not prefixes:
        raise SystemExit(f"No capture TIFFs (PREFIX_<band>.tif) found in {repo_folder}")

    print(f"Found {len(prefixes)} capture(s) in {repo_folder}: {prefixes[0]} .. {prefixes[-1]}")

    alignment = None
    if not args.skip_align:
        if args.ref:
            candidates = [args.ref]
        else:
            mid = len(prefixes) // 2
            candidates = sorted(prefixes, key=lambda p: abs(prefixes.index(p) - mid))
        for ref_prefix in candidates:
            try:
                alignment = compute_alignment(load_capture(ref_prefix, repo_folder))
                print(f"Alignment solved from {ref_prefix}; reusing for all captures.")
                break
            except Exception as exc:
                print(f"  alignment ref {ref_prefix} did not converge ({exc}); trying another...")
        if alignment is None:
            raise SystemExit("Could not solve band alignment from any capture. "
                             "Try --ref <a sharp, high-texture capture>, or --skip-align.")

    failures = []
    for prefix in prefixes:
        output_filename = f"{prefix}_rgb.png"
        try:
            align_layers_to_rgb(
                prefix,
                output_filename=output_filename,
                repo_folder=repo_folder,
                skip_alignment=args.skip_align,
                alignment=alignment,
            )
            print(f"  [ok]   {prefix} -> {output_filename}")
        except Exception as exc:
            print(f"  [FAIL] {prefix}: {exc}")
            failures.append(prefix)

    done = len(prefixes) - len(failures)
    print(f"Done: {done}/{len(prefixes)} captures written to {repo_folder}")
    if failures:
        raise SystemExit(f"{len(failures)} capture(s) failed: {', '.join(failures)}")
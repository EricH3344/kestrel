
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

import matplotlib
matplotlib.use("Agg")  # headless: render to file, no display needed
import matplotlib.pyplot as plt


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
        multithreaded=False,  # solved once and reused; avoids MKL worker-spawn / paging-file errors
    )
    cropped_dimensions, _ = imageutils.find_crop_bounds(cap, warp_matrices, warp_mode=WARP_MODE)
    return warp_matrices, cropped_dimensions


def capture_to_aligned_bands(cap, alignment=None):
    """Return the full (H, W, N) stack of the capture's bands. If alignment
    (warp_matrices, cropped_dimensions) is given they are registered to each other and
    cropped to the common overlap; otherwise raw radiance bands are stacked as-is."""
    if alignment is None:
        return np.dstack([img.radiance() for img in cap.images])
    warp_matrices, cropped_dimensions = alignment
    # Reflectance needs valid DLS irradiance on every band; else fall back to radiance.
    use_reflectance = all(img.horizontal_irradiance for img in cap.images)
    img_type = "reflectance" if use_reflectance else "radiance"
    return imageutils.aligned_capture(
        cap, warp_matrices, WARP_MODE, cropped_dimensions, MATCH_INDEX, img_type=img_type
    )


def stack_to_rgb(bands):
    """Pick blue/green/red channels and return an (H, W, 3) array in R,G,B order."""
    blue, green, red = bands[:, :, 0], bands[:, :, 1], bands[:, :, 2]
    return np.dstack((red, green, blue))


def _nearest_band(cap, target_nm, default_idx):
    """Index of the band whose center wavelength is closest to target_nm (nm)."""
    try:
        wavelengths = [img.center_wavelength for img in cap.images]
        return min(range(len(wavelengths)), key=lambda i: abs(wavelengths[i] - target_nm))
    except Exception:
        return default_idx


def compute_ndvi(bands, cap):
    """NDVI = (NIR - Red) / (NIR + Red), selecting bands by center wavelength
    (~668 nm Red, ~842 nm NIR). Returns a float array clipped to [-1, 1]."""
    red_idx = _nearest_band(cap, 668, default_idx=2)
    nir_idx = _nearest_band(cap, 842, default_idx=3)
    if max(red_idx, nir_idx) >= bands.shape[2]:
        raise ValueError("Capture lacks NIR/Red bands required for NDVI")
    red = bands[:, :, red_idx].astype(np.float32)
    nir = bands[:, :, nir_idx].astype(np.float32)
    with np.errstate(divide="ignore", invalid="ignore"):
        ndvi = (nir - red) / (nir + red)
    ndvi = np.nan_to_num(ndvi, nan=0.0, posinf=0.0, neginf=0.0)
    return np.clip(ndvi, -1.0, 1.0)


def save_ndvi_png(ndvi, output_path, title=None, vmin=-0.2, vmax=0.9):
    """Save a Pix4D-style colorized NDVI map (RdYlGn) with a colorbar legend."""
    height, width = ndvi.shape
    fig, ax = plt.subplots(figsize=(width / 150, height / 150), dpi=150)
    im = ax.imshow(ndvi, cmap="RdYlGn", vmin=vmin, vmax=vmax)
    ax.axis("off")
    if title:
        ax.set_title(title)
    cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("NDVI")
    fig.savefig(output_path, bbox_inches="tight", dpi=150)
    plt.close(fig)
    if not os.path.exists(output_path):
        raise IOError(f"Failed to save image to {output_path}")


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
    bands = capture_to_aligned_bands(cap, alignment=None if skip_alignment else alignment)
    return save_rgb_png(stack_to_rgb(bands), os.path.join(repo_folder, output_filename))


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
        description="Align MicaSense band TIFFs and export RGB and/or NDVI images per capture."
    )
    parser.add_argument(
        "folder", nargs="?", default=None,
        help="Folder containing PREFIX_<band>.tif files (default: this script's folder)",
    )
    parser.add_argument(
        "--product", choices=["rgb", "ndvi", "both"], default="rgb",
        help="What to export per capture: rgb, ndvi (Pix4D-style colorized map), or both",
    )
    parser.add_argument(
        "--prefix", default=None,
        help="Process only this capture prefix (e.g. IMG_0003); default: every capture found",
    )
    parser.add_argument(
        "--skip-align", action="store_true",
        help="Skip band alignment (fast preview, but bands will be misregistered)",
    )
    parser.add_argument(
        "--ref", default=None,
        help="Capture prefix to compute alignment from (e.g. IMG_0100); default: auto-pick "
             "a mid-flight capture that converges. Reused for all captures.",
    )
    args = parser.parse_args()
    want_rgb = args.product in ("rgb", "both")
    want_ndvi = args.product in ("ndvi", "both")

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
        try:
            cap = load_capture(prefix, repo_folder)
            bands = capture_to_aligned_bands(cap, alignment=None if args.skip_align else alignment)
            written = []
            if want_rgb:
                name = f"{prefix}_rgb.png"
                save_rgb_png(stack_to_rgb(bands), os.path.join(repo_folder, name))
                written.append(name)
            if want_ndvi:
                name = f"{prefix}_ndvi.png"
                save_ndvi_png(compute_ndvi(bands, cap), os.path.join(repo_folder, name), title=prefix)
                written.append(name)
            print(f"  [ok]   {prefix} -> {', '.join(written)}")
        except Exception as exc:
            print(f"  [FAIL] {prefix}: {exc}")
            failures.append(prefix)

    done = len(prefixes) - len(failures)
    print(f"Done: {done}/{len(prefixes)} captures written to {repo_folder}")
    if failures:
        raise SystemExit(f"{len(failures)} capture(s) failed: {', '.join(failures)}")
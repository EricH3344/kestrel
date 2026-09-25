# Kestrel

Kestrel is a cross-platform Qt/QML/C++ desktop application for organizing TIFF imagery into projects and building field mosaics for viewing in the Map screen. Project creation, individual-file/folder import, asynchronous mosaic processing, and map preview are implemented. NDVI is planned work, and the terrain-aware orthorectification core is under active development.

## Quick start (Windows)

### Requirements

- CMake 3.21 or newer
- A C++17 compiler kit supported by Qt (MinGW, MSVC, or GCC)
- Qt 6.8 or newer, installed with **Core**, **Gui**, **Widgets**, **QML**, **Quick**, **Quick Timeline**, and **Shader Tools**
- An internet connection during the first configure, unless OpenCV 4.8 or newer is already installed and discoverable by CMake

Use a compiler that matches the installed Qt kit. For example, a `mingw_64` Qt installation requires the corresponding MinGW toolchain on `PATH`.

### Build and run

From the repository root, configure CMake against the application directory:

```powershell
cmake -S app/Kestrel -B build/kestrel -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build build/kestrel --parallel
& .\build\kestrel\KestrelApp.exe
```

Adjust the Qt path and generator for your machine. With an MSVC Qt kit, use a Visual Studio generator instead, for example:

```powershell
cmake -S app/Kestrel -B build/kestrel-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"
cmake --build build/kestrel-msvc --config Release --parallel
& .\build\kestrel-msvc\Release\KestrelApp.exe
```

If CMake cannot find Qt, confirm `CMAKE_PREFIX_PATH` points to the Qt kit directory (the folder containing `lib/cmake/Qt6`) and that the matching compiler is on `PATH`.

For an existing `app/Kestrel/build-mingw` configuration, the shorter workflow is:

```powershell
cmake --build app/Kestrel/build-mingw -j 2
& .\app\Kestrel\build-mingw\KestrelApp.exe
```

Running `./KestrelApp.exe` is also correct after changing the current directory to `app/Kestrel/build-mingw`.

### Build the OpenMVS quality backend

Kestrel retains its in-house plane-sweep MVS implementation, but can also build
the pinned OpenMVS 2.4 PatchMatch backend for substantially more mature dense
geometry. This option requires CMake 3.24 or newer. Reconfigure the existing
build once with:

```powershell
cmake -S app/Kestrel -B app/Kestrel/build-mingw -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64" `
  -DKESTREL_BUILD_OPENMVS=ON
cmake --build app/Kestrel/build-mingw -j 2
```

The first build downloads checksum/revision-pinned source through CMake, boots
the pinned vcpkg revision used by OpenMVS CI, and compiles OpenMVS plus its
dependencies. It is considerably slower and larger than the normal Kestrel
build; subsequent incremental builds reuse those artifacts. The portable
default is CPU-only. `-DKESTREL_OPENMVS_ENABLE_CUDA=ON` is an optional separate
build variant for machines with a compatible CUDA toolkit.

On Windows, install the Visual Studio 2022 **Desktop development with C++**
workload on the developer machine. CMake builds the OpenMVS command-line tools
with MSVC and static dependencies even when Kestrel itself uses MinGW. The two
parts communicate through files and child processes, so they do not share a C++
ABI. This avoids unreliable MinGW builds of OpenMVS's GMP/CGAL dependency chain
and does not add a Visual Studio requirement for people using a packaged app.
Linux builds use the host GCC/Clang toolchain.

`cmake --install` copies `InterfaceCOLMAP`, `DensifyPointCloud`, and collected
license notices into the Kestrel package. Deployed users therefore do not need
to install OpenMVS, vcpkg, CUDA, Python, or Docker. Windows and Linux still need
separate native release packages; they do not share one executable bundle.

To create a self-contained Windows deployment tree with the required Qt DLLs, QML imports, plugins, and compiler runtimes:

```powershell
cmake --install app/Kestrel/build-mingw `
  --prefix app/Kestrel/build-mingw/package `
  --component KestrelRuntime
& .\app\Kestrel\build-mingw\package\bin\KestrelApp.exe
```

### Build and run on Linux

Install a Qt 6.8+ GCC kit and a C++ build toolchain, then run:

```bash
cmake -S app/Kestrel -B build/kestrel \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
cmake --build build/kestrel --parallel 2
./build/kestrel/KestrelApp
```

Adjust the Qt path for the installed version. A distribution-provided Qt installation may be found automatically and not require `CMAKE_PREFIX_PATH`.

Use the same install step to assemble a Linux deployment tree:

```bash
cmake --install build/kestrel --prefix build/kestrel/package \
  --component KestrelRuntime
./build/kestrel/package/bin/KestrelApp
```

## Use the app

1. Launch `KestrelApp`.
2. Click the **+** button in the project tab bar to open **Create Project**.
3. Enter a project name. By default, projects are created under `Documents/Kestrel Projects/<project name>`.
4. Optionally choose a different project directory.
5. Select one or more `.tif`/`.tiff` files, or import a folder containing TIFFs.
6. Click **Create Project** and wait for the import to complete.

Kestrel copies the selected images into `raw_images`; it does not modify the source images. Use a unique filename for each imported image: images with the same filename would target the same destination and cause project creation to fail.

After a project is created, Kestrel automatically builds a field mosaic and displays the resulting preview in the Map view. The Map view shows the current preparation, overlap-matching, and blending stage while processing. The pipeline is implemented in Kestrel and uses statically linked OpenCV primitives for TIFF decoding, feature extraction, robust homography estimation, spectral-band registration, and image warping. Users do not need Docker, Python, OpenDroneMap, ExifTool, or any separate processing installation.

The current mosaic model targets overlapping, nadir imagery of relatively flat farm fields. It registers MicaSense Blue/Green/Red bands when filenames follow the usual `<capture>_<band>.tif` convention, reads embedded TIFF GPS through the bundled codec, uses geographic nearest neighbours to select likely overlaps, builds a connected overlap graph, solves capture-to-mosaic homographies, and refines those transforms with an in-house robust planar global bundle adjustment. It then normalizes brightness and feather-blends the source-resolution imagery. Very large solved canvases are uniformly reduced to a memory-safe size and the progress message reports that choice.

Alongside that preview path, Kestrel now reconstructs calibrated sparse 3D
cameras and points, jointly refines them with robust reprojection-error bundle
adjustment, robust GPS priors, and shared prior-constrained sensor calibration,
and aligns sufficiently constrained components to GPS/ENU. Sparse points are
Schur-eliminated and the remaining block-sparse camera system is solved with
in-house preconditioned conjugate gradients. These results and their
diagnostics are persisted in `transforms.yml`. An in-house diagnostic path now
also produces dense depth maps, a fused and filtered point cloud, a DSM, and an
occlusion-aware multi-camera orthophoto with optimized seams and multiband
blending. It can export projected float DSM, single-band orthophoto, and
shared-geometry float RGB-radiance GeoTIFF diagnostics without GDAL. The RGB path
terrain-warps the RedEdge blue, green, and red sensors with their independent
camera models while sharing one physical-capture/seam map. The displayed
application mosaic remains planar until that path is wired into normal project
processing.

To rebuild an existing project's mosaic from a terminal without opening the UI:

```powershell
& .\app\Kestrel\build-mingw\KestrelApp.exe --stitch-project `
  "C:\Users\you\Documents\Kestrel Projects\my project"
```

The command prints the same progress stages to the terminal and writes the normal project outputs below `processed_images/kestrel_mosaic`.

The backend-neutral real-flight diagnostic can exercise OpenMVS before it is
wired into normal Map-screen processing. After building with
`KESTREL_BUILD_OPENMVS=ON`, a 10-capture quality gate can be started with:

```powershell
cmake --build app/Kestrel/build-mingw `
  --target KestrelDenseReconstructionRealSmoke -j 2
& .\app\Kestrel\build-mingw\KestrelDenseReconstructionRealSmoke.exe `
  "C:\Users\you\Documents\Kestrel Projects\my project" `
  ".\build\openmvs-smoke-10" 3 10 1280 192 0.05 openmvs
```

Use `plane-sweep` as the final argument to retain the original implementation,
or `prefer-openmvs` to permit an explicitly reported plane-sweep fallback. An
explicit `openmvs` run fails instead of silently changing algorithms. The
diagnostic writes `openmvs-process.log`, OpenMVS restart artifacts, the dense
PLY, filtered cloud/DSM, and Kestrel orthophoto outputs below the selected
output directory.

The normal application output is currently a visually aligned, unreferenced
TIFF mosaic rather than a survey-grade orthomosaic. The separate tested
diagnostic pipeline reaches checkpoint-backed dense terrain reconstruction,
seam-optimized terrain orthorectification at an independently configurable
GSD, WGS 84/UTM georeferencing, and RGB-radiance GeoTIFF export. Kestrel now
applies MicaSense black-level, vignette, row-gradient, exposure/gain, bit-depth,
and camera radiometric coefficients while masking saturated pixels. Panel and
downwelling-light calibration remain before the result can be treated as
surface reflectance; narrow spectral bands are not inherently a color-managed
photograph.

### Project output

Creating a project produces this layout:

```text
<project directory>/
├── <project name>.kproj       # Kestrel binary project metadata
├── raw_images/                # copied source TIFF files
├── processed_images/kestrel_mosaic/
│   ├── field_mosaic.tif       # TIFF mosaic produced by Kestrel
│   ├── field_mosaic.png       # Map view preview
│   └── transforms.yml         # planar transforms and sparse 3D reconstruction
├── output/                    # reserved for exports
└── metadata/                  # reserved for project metadata
```

`.kproj` files are binary Qt data-stream files, intended to be read by Kestrel rather than manually edited. They store the project name, creation details, directory locations, and the original selected-file list.

## Current status

Implemented:

- Desktop shell with Map, Database, Flight Logs, and Reports views
- Project-directory selection and project creation
- Multi-file TIFF (`.tif`, `.tiff`) selection and copying into `raw_images`
- Kestrel project-file creation and validation/loading support in the C++ layer
- In-house flat-field mosaic pipeline with MicaSense band grouping and registration
- Robust planar global bundle adjustment over all retained pairwise inlier observations
- Calibrated sparse camera/point reconstruction with robust GPS/ENU alignment
- True 3D bundle adjustment with shared sensor-calibration refinement, point-block Schur elimination, and a sparse iterative camera solver
- Initial in-house dense-MVS core with calibrated neighbour selection, sparse-guided depth bounds, multi-scale plane-sweep depth maps, occlusion-aware consistency, confidence-weighted point fusion, and robust spatial/elevation filtering
- Selectable dense point-cloud backends: pinned OpenMVS 2.4 through COLMAP/PLY process isolation, the retained in-house plane sweep, or an explicitly reported prefer-OpenMVS fallback mode
- Optional CMake/vcpkg OpenMVS source superbuild and release packaging, with CPU-first Windows/Linux configuration and an opt-in CUDA variant
- Configurable dense-MVS core/halo tiling with signature-validated per-tile depth/confidence checkpoints and deterministic resume
- Checkpoint-backed global consistency that retains only a reference and its selected neighbours, then streams filtered maps into fusion
- Atomic, signature-validated raw and consistency tile replacement with corruption recovery and selective invalidation
- Successful 191-capture reduced-resolution dense-MVS, fusion, DSM, and tiled terrain-orthophoto diagnostic with full tile-resume coverage
- Terrain-aware inverse projection from an output ground grid through a DEM/DSM into a calibrated Brown-Conrady camera model
- Synthetic orthorectification tests for flat, sloped, and missing terrain cells
- WGS-84 GPS/altitude conversion into a local metric ENU reconstruction frame
- Automatic north-up orthophoto-grid derivation from terrain bounds and requested GSD
- Orthophoto resolution independent from DSM spacing, with exact output-grid geometry retained for GIS export
- In-house WGS 84/UTM projection and explicit ENU-to-projected raster transforms with measured local linearization error
- Self-contained projected GeoTIFF export for float DSMs and orthophotos, including EPSG, PixelIsArea, no-data, and validity alpha metadata
- Shared-geometry float RGB-radiance orthophotos with independent blue/green/red camera projection, common capture labels, calibrated camera radiance, saturation masks, and projected GeoTIFF/PNG diagnostics
- Dense-point top-surface DSM rasterization with explicit validity/sample-count masks
- Occlusion-aware multi-camera terrain orthorectification with view/resolution/border/confidence scoring, deterministic seam optimization, Laplacian-pyramid blending, and pyramid-aligned overlap tiles
- Per-band TIFF/EXIF/XMP camera metadata extraction, validation, and persistence
- Conservative bounded DSM-hole interpolation with a synthesized-cell mask

Planned / not yet wired into the application:

- Opening a created `.kproj` through the UI
- NDVI or other vegetation-index products
- Wiring dense MVS, fusion, filtering, and DSM construction into the normal imported-project pipeline
- End-to-end terrain-aware orthomosaic and projected GeoTIFF integration into the Map screen
- Reflectance-panel/DLS calibration and reflectance-calibrated multispectral/vegetation-index exports

The ordered engineering gap between the current implementation and the pinned
ODM comparison is maintained in
[`docs/PHOTOGRAMMETRY_ROADMAP.md`](docs/PHOTOGRAMMETRY_ROADMAP.md). It also
marks which stages can be developed against fast synthetic fixtures so the full
flight dataset is reserved for major integration milestones.

## Third-party boundary

Kestrel implements the application-specific pipeline: capture discovery and spectral grouping, overlap-pair selection, connected-graph/global-transform solving, sparse reconstruction, robust 3D bundle adjustment, shared calibration refinement, GPS/ENU alignment, block-sparse Schur solving, terrain-grid sampling, calibrated terrain-to-image projection, brightness normalization, memory-aware canvas management, feather blending, project output, background execution, and Map-screen integration.

OpenCV 4.8+ is retained for the parts that would be disproportionate capstone work to reproduce safely: TIFF/PNG/JPEG codecs, SIFT feature extraction and matching, RANSAC homography estimation, ECC cross-band registration, perspective warping, and distance transforms. If a compatible installation is unavailable, CMake downloads a checksum-pinned OpenCV 4.10 source archive and statically builds only the required modules and codecs. This makes the initial developer build longer, but deployed users do not need OpenCV, Python, Docker, or OpenDroneMap installed.

OpenMVS 2.4 is an optional quality backend for the particularly difficult dense
multi-view stereo stage. Kestrel continues to own camera calibration, sparse
reconstruction, bundle adjustment, geospatial alignment, point filtering, DSM
construction, orthorectification, seam processing, radiometry, and product
export. OpenMVS is executed as an isolated tool over documented COLMAP and PLY
formats, while the original `PlaneSweepMvsBackend` remains available for future
in-house development. OpenMVS is AGPL-3.0 software; distributing a build that
includes it requires preserving its notices, providing the corresponding
source, and ensuring Kestrel's distribution model satisfies the applicable
AGPL obligations.

Qt and OpenCV (including the codec libraries built with OpenCV) remain third-party software. Release packaging should deploy the required Qt runtime files and include the license notices applicable to the selected Qt license, OpenCV, libtiff, libpng, zlib, and libjpeg-turbo.

## Optional imagery-processing environment

`imgprocessing/` contains an experimental Python utility, `layerstorgb.py`, that aligns a MicaSense capture and writes an RGB PNG. It is separate from the Qt application.

Create its Conda environment with:

```powershell
conda env create -f imgprocessing/environment.yml
conda activate micasense_env
```

The MicaSense package also requires ExifTool to be installed and available on `PATH`. The script currently expects TIFF files named like `<capture-prefix>_*.tif` in its working directory and has a capture prefix set in its `__main__` block; update that value before running it.

## External ODM quality comparison

`tools/odm-comparison/` contains a Docker-based OpenDroneMap benchmark for
comparing Kestrel's mosaic against a full photogrammetry and orthorectification
pipeline. It is intentionally separate from the application and is not a
Kestrel runtime dependency. See
[`tools/odm-comparison/README.md`](tools/odm-comparison/README.md) for setup and
comparison instructions.

## Repository layout

```text
app/Kestrel/        Qt/QML desktop application and C++ project services
app/Kestrel/src/    Application, project, and photogrammetry C++ components
app/Kestrel/tests/  Fast synthetic and component-level C++ tests
imgprocessing/      Standalone Python/MicaSense experiments
build/              Local CMake build output (not source)
```

## Development notes

- Configure CMake with `app/Kestrel` as the source directory; there is no root-level `CMakeLists.txt`.
- Generated Qt/CMake files and build directories are ignored. Keep source edits under `app/Kestrel/` and avoid committing generated output.
- The project uses C++17 and requires the Qt modules declared in `app/Kestrel/CMakeLists.txt`.
- OpenCV 4.8+ supplies the third-party computer-vision primitives. CMake uses a compatible system installation when present or downloads and statically builds OpenCV 4.10 by default. Set `KESTREL_FETCH_OPENCV=OFF` to require an existing `OpenCV_DIR` instead.

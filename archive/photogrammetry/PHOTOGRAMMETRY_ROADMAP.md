# Photogrammetry roadmap

This roadmap tracks both the implementation and the quality gap between
Kestrel and the terrain-aware orthophoto produced by the pinned ODM 3.6.0
comparison run. It is ordered by dependency, not by UI visibility.

The target is the orthomosaic path used by `tools/odm-comparison`, which passes
`--skip-3dmodel`. A textured 3D mesh is therefore intentionally out of scope;
it is not required to improve the field orthophoto.

The implementation target is a class of calibrated, overlapping aerial
surveys rather than one filename layout or flight. Core algorithms must keep
image size, camera geometry, tile size, halo, GSD, and quality thresholds
configurable. A feature is not considered generally validated by the RedEdge
flight alone: it also needs scale/resolution-varied synthetic tests and,
before release-quality claims, at least one unrelated real camera or flight.
Dataset-specific band selection, crops, and reduced diagnostic resolutions
belong in smoke runners or presets, not in the reusable algorithms.

## How to read this roadmap

Historically, a checked box in Stages 1-9 meant that a reusable baseline
implementation existed and passed focused tests. It did **not** mean that the
component matched ODM's reconstruction quality, had been tuned at source
resolution, worked through the normal application workflow, or was validated
on multiple unrelated flights. That distinction was missing and made the
roadmap look much closer to completion than the output actually is.

From this revision onward:

- Stages 1-10 are the **capability checklist**. `[x]` means the path exists and
  has focused evidence; it does not by itself claim ODM parity.
- The **quality-parity gates** below are the completion criteria for an
  ODM-like stitching claim. All applicable gates must pass with recorded
  evidence before that claim is made.
- A reduced-resolution smoke run proves execution and catches regressions. It
  is not a visual-quality or survey-accuracy acceptance test.

Current honest estimate (2026-07-20):

| Dimension | Rough maturity | What that means |
| --- | ---: | --- |
| Reusable pipeline architecture | 75-85% | Most major stages exist as in-house components. |
| Visual orthomosaic quality on the RedEdge flight | 45-55% | Geometry is coherent, but dense depth, surface quality, seam placement, and radiometric leveling remain materially behind ODM. |
| Survey-grade/generalized quality | 25-35% | No independent checkpoints, no unrelated-flight gate, and several camera/radiometric cases remain unsupported. |
| User-facing terrain pipeline | 35-45% | The application still writes the planar feather-blended `field_mosaic`; the terrain pipeline is currently driven by the manual real-smoke runner. |

These percentages measure different things and must not be averaged. The
architecture can be mostly present while the final image remains noticeably
worse, because a few deep algorithms dominate output quality.

## Quality-parity gates

These gates are the authoritative list of work remaining before describing
Kestrel as close to ODM-level stitching. Priority 0 gates come next.

### Q0. Controlled ODM comparison and acceptance metrics (Priority 0)

- [ ] Add a repeatable comparison runner that produces Kestrel and pinned ODM
      outputs from the exact same captures, primary band, crop/cutline, CRS,
      GSD, and radiometric mode. The current 5 cm Kestrel smoke output is not a
      valid pixel-level comparison with the default 1 cm ODM output.
- [ ] Measure coverage/no-data, local edge sharpness, double-edge or ghost
      displacement in overlaps, seam discontinuity, and structural image
      similarity. Save difference images and worst-region crops rather than
      relying only on whole-image visual judgment.
- [ ] Measure sparse reprojection, camera/GPS residuals, dense consistency,
      DSM roughness/gaps, and independent horizontal/vertical checkpoint error
      in the same report. Reprojection RMS alone does not establish map
      accuracy or texture sharpness.
- [ ] Record acceptance thresholds against ODM and ground truth where
      available, then make regressions fail automatically.

### Q1. Normal application pipeline integration (Priority 0)

- [ ] Replace the default planar `StitchingController::buildMosaic` deliverable
      with an orchestrated sparse -> dense -> DSM -> terrain orthophoto ->
      GeoTIFF pipeline. Keep the planar mosaic only as an explicitly labelled
      fast preview/fallback.
- [ ] Persist typed artifacts and dependency fingerprints for every expensive
      stage so the application can resume the first invalid stage rather than
      requiring the manual `KestrelDenseReconstructionRealSmoke` executable.
- [ ] Connect real stage progress, cancellation, errors, quality warnings, and
      completed terrain products to the project UI.

### Q2. Dense geometry quality (Priority 0; largest visual-quality gate)

- [x] Augment the fixed fronto-parallel discrete plane sweep with a pinned
      OpenMVS 2.4 PatchMatch quality backend. Kestrel exports its calibrated,
      bundle-adjusted reconstruction through the documented COLMAP text
      interchange, runs `InterfaceCOLMAP` and `DensifyPointCloud`, imports the
      binary PLY with camera provenance, and rejoins Kestrel at the fused-cloud
      filtering/DSM boundary. `PlaneSweepMvsBackend` remains selectable as the
      in-house fallback and future research implementation.
- [x] Add an opt-in CMake superbuild that pins OpenMVS and the vcpkg revision
      used by its own CI, disables unrelated viewer/Python/SfM dependencies,
      and installs the two runtime tools and available license notices with
      Kestrel. CPU builds are the portable baseline; CUDA remains an explicit
      optional build variant.
- [ ] Run the new OpenMVS backend on the 10- and 36-capture gates and verify
      coordinate-frame preservation, PLY view provenance, dense coverage,
      memory, DSM roughness, and final orthophoto sharpness. Integration being
      complete is not evidence that the selected OpenMVS parameters are yet
      optimal for crop canopy.
- [ ] If in-house dense reconstruction is resumed after application
      integration, replace plane sweep's fixed hypotheses with per-pixel
      slanted planes, spatial/view propagation, randomized refinement, and
      continuous/subpixel depth refinement. Do not remove the OpenMVS baseline
      until the native path meets the same acceptance measurements.
- [ ] Add robust multi-scale matching costs for low-texture crops and exposure
      changes, per-pixel view selection/aggregation, uniqueness checks, and
      calibrated confidence instead of relying primarily on a small fixed ZNCC
      patch and globally sampled depths.
- [ ] Improve geometric consistency and fusion with surface-normal agreement,
      visibility/free-space evidence, adaptive uncertainty, and preservation
      of thin boundaries without retaining speckle.
- [ ] Demonstrate a measured quality gain on the 10- and 36-capture gates at
      full source dimensions before another all-flight quality run. The
      existing 480-by-360 full-flight run is a memory/execution milestone, not
      a full-resolution quality result.

### Q3. DSM/surface fidelity (Priority 1)

- [ ] Replace highest-sample-per-cell DSM construction with a robust,
      confidence/visibility-aware surface estimate that resists canopy and MVS
      outliers without flattening legitimate terrain changes.
- [ ] Add edge-aware gap handling and an optional triangulated 2.5D surface;
      quantify which cells are measured, interpolated, extrapolated, or
      rejected. Do not use large smooth fills to hide missing dense geometry.
- [ ] Compare DSM elevations, slope, roughness, holes, and boundary behavior
      against ODM and independent checkpoints at the requested output GSD.

### Q4. Orthophoto source selection, seams, and sharpness (Priority 1)

- [ ] Estimate overlap-aware per-image/per-band radiometric compensation from
      valid common terrain, rather than passing a constant exposure quality or
      applying only one global brightness scalar.
- [ ] Upgrade local ICM seam selection to a global or hierarchical graph-cut
      style optimizer with gradient/texture, moving-object, blur, incidence,
      resolution, and occlusion costs; retain deterministic tile-boundary
      consistency.
- [ ] Add global and local seam-leveling before Laplacian blending. Limit the
      blend width using source sharpness/GSD so blending does not turn small
      registration errors into broad blur.
- [ ] Add automatic field boundary/cutline generation and inspect worst seam,
      ghosting, and blur regions at 100% output scale.

### Q5. Camera and georeferencing robustness (Priority 1)

- [ ] Import GCPs as optimization constraints and reserve independent
      checkpoints for unbiased horizontal/vertical error reporting. Ingest
      per-image RTK/GPS accuracy rather than using one assumed uncertainty.
- [ ] Add camera models beyond the current five-coefficient Brown model where
      data requires them, plus rolling-shutter pose correction for moving
      rolling-shutter cameras. These are generalization gates, not blockers for
      a confirmed global-shutter RedEdge flight.
- [ ] Import measured multi-sensor rig translations and verify residual band
      alignment across height changes; do not treat the shared optical centre
      as release-quality when a physical baseline matters.
- [ ] Validate calibration stability and absolute/relative accuracy on an
      unrelated camera and flight with independently measured checkpoints.

### Q6. Multispectral product correctness (Priority 1)

- [ ] Detect reflectance-panel captures, measure clean panel ROIs, ingest
      certified per-band reflectance, and match corrected DLS/sun irradiance
      to flight captures when available.
- [ ] Add image-supported residual inter-band alignment after rig projection,
      while preserving physical geometry and reporting when texture is too
      weak to estimate a correction safely.
- [ ] Export validated reflectance, NDVI, and NDRE with wavelength/nodata
      metadata and panel/checkpoint residuals. Camera-calibrated radiance must
      remain labelled as radiance.

### Q7. Release-scale reliability and generalization (Priority 2)

- [ ] Pass full-resolution gates on the RedEdge flight and at least two
      unrelated surveys covering different cameras, terrain relief, crop
      texture, lighting, overlap, and incomplete metadata.
- [ ] Add bounded concurrency, RAM/disk estimates, cancellation, atomic cleanup,
      stage-level resume, and spatial split/merge for projects that exceed one
      machine's practical working set.
- [ ] Generate tiled/overview or Cloud-Optimized GeoTIFF deliverables and pass
      independent GDAL/QGIS interoperability checks without making GDAL a
      separate end-user installation.
- [ ] Produce a permanent processing report and CI regression corpus containing
      quality metrics, provenance, rejected inputs, warnings, and comparison
      artifacts.

## Implemented capability baseline

- Capture discovery, MicaSense spectral grouping, and embedded TIFF GPS reads
- SIFT matching, pairwise RANSAC homographies, and an overlap graph
- Global planar transform solving and robust planar bundle adjustment
- Source-resolution warping, exposure normalization, and feather blending
- A tested terrain-grid sampler and Brown-Conrady camera projection
- DSM-aware inverse resampling for one calibrated source camera
- WGS-84 GPS/altitude conversion to a persisted local metric ENU frame
- Automatic north-up orthophoto-grid sizing from terrain bounds and GSD
- Dense-point DSM rasterization with validity and per-cell sample-count masks
- Per-band TIFF/EXIF/XMP camera metadata extraction and validation
- Physical multi-camera capture grouping from XMP identity with a filename fallback
- Pixel-space camera calibration with distortion and explicit fallback quality
- Fundamental-matrix verification and persistent 3+-view feature tracks
- Calibrated initial-pair pose recovery and quality-filtered sparse 3D points
- Incremental PnP camera registration with multi-view track retriangulation
- Explicit disconnected sparse components and robust GPS/ENU similarity alignment
- Conservative interpolation of only small, enclosed DSM holes

Most of these capabilities are reusable library code. They are not yet one
production pipeline: project creation currently invokes sparse reconstruction
but still renders and displays the planar homography/feather mosaic. Dense MVS,
DSM generation, terrain orthorectification, five-band radiance, and projected
GeoTIFF export are integrated in the manual real-flight diagnostic executable.

The planar optimizer is useful for flat previews, but it cannot explain image
parallax caused by camera translation, crops, vegetation, or terrain relief.
ODM first reconstructs cameras and scene geometry in 3D, then uses that model
to orthorectify the source images.

## Ordered implementation backlog

### 1. Camera and capture metadata foundation

- [x] Read image dimensions during capture preparation and latitude, longitude,
      and GPS altitude from TIFF metadata.
- [x] Parse focal length, orientation, capture time, camera/sensor identity,
      and per-band metadata from TIFF EXIF/XMP.
- [x] Represent one physical exposure as a multi-camera capture instead of
      relying only on filename conventions.
- [x] Add a camera calibration model containing sensor size, focal length,
      principal point, Brown-Conrady distortion, and fixed relative poses
      between spectral sensors.
- [x] Add known MicaSense calibrations plus a safe unknown-camera fallback.
- Conditional calibration capability, not a current processing gate:
  `RelativeCameraPose` can represent an inter-sensor translation, but the
  MicaSense `RigRelatives` metadata supplies rotation only. Kestrel records
  translation as unknown instead of inventing a baseline. A future external
  rig-calibration importer should populate it only when a measured baseline is
  actually supplied.
- [x] Convert WGS-84 latitude/longitude/altitude to a local metric ENU frame
      and persist its origin and per-capture coordinates in `transforms.yml`.
- [x] Detect missing/invalid metadata and inconsistent cross-band capture IDs
      or timestamps, report warnings before processing, and persist them with
      the extracted per-band metadata in `transforms.yml`.

Fast validation: metadata fixtures, coordinate-conversion unit tests, and
synthetic projection/undistortion tests. No full flight is needed.

### 2. Multi-view feature tracks

- [x] Replace isolated pairwise match storage with persistent feature IDs and
      tracks observed across three or more captures.
- [x] Add geometric verification using essential/fundamental matrices, not
      only planar homographies.
- [x] Reject duplicate, weak, and internally inconsistent tracks.
- [x] Use GPS and capture sequence only to choose match candidates; require
      image geometry to accept each candidate instead of forcing GPS neighbours
      to match.
- [x] Use GPS only as a reconstruction initializer and robust,
      uncertainty-weighted bundle-adjustment prior; do not force reconstructed
      cameras to reproduce inaccurate GPS positions exactly.
- [x] Serialize source-resolution tracks, pair matches, fundamental matrices,
      and camera-calibration provenance so later stages can resume quickly.

Fast validation: generated cameras and 3D points with controlled pixel noise,
plus a small 5-12-image fixture.

### 3. Sparse structure from motion

- [x] Initialize a reconstruction from a strong image pair.
- [x] Recover relative camera rotation and translation with essential-matrix
      decomposition and cheirality checks.
- [x] Triangulate tracks into 3D points and reject low-angle/behind-camera
      geometry.
- [x] Add new cameras with PnP RANSAC and retriangulate their observations.
- [x] Report images that cannot be placed in the connected reconstruction.
- [x] Recover additional disconnected components and persist their cameras and
      points without pretending their arbitrary frames are already shared.
- [x] Align each sufficiently constrained component's scale, rotation, and
      translation to GPS/ENU using robust camera-position consensus.
- Deferred robustness capability, not a gate for the connected example flight:
  merge components only when GPS alignment or cross-component constraints
  provide enough support, while retaining weak components separately. Both
  the 36- and 191-capture validation flights reconstructed as one component.

Fast validation: synthetic camera networks with known ground truth and one
small real subset. This is the first stage that needs a real-image smoke test.

Real-image smoke test (2026-07-14): captures `IMG_0084` through `IMG_0095`
from the example RedEdge-M flight registered all 12 cameras in one component,
retained 410 sparse points from 411 multi-view tracks, aligned all 12 GPS
positions with 0.40 m RMS, and left no capture unregistered. PnP reprojection
RMS stayed below 0.8 px. This passes the Stage 3 smoke gate; the full flight
still waits for a successful 25-40 capture Stage 4 run.

Restored-data audit (2026-09-09): `C:\Users\sahil\Documents\krestrel data`
contains 25 captures named `IMG_0000` through `IMG_0024`, not the earlier
191-capture validation flight or its `IMG_0084`-`IMG_0095` smoke subset. All
125 TIFF bands decode and carry calibrated RedEdge-M metadata, but conservative
matching retains only five of 145 candidate-pair edges. Retaining verified
two-view tracks for component initialization increased sparse coverage from
three cameras to seven cameras in three components: `IMG_0007`, `IMG_0008`,
`IMG_0009`, `IMG_0016`, `IMG_0017`, `IMG_0023`, and `IMG_0024`. This folder cannot
replace the earlier regression corpus, but it is retained as a supplementary
hard repetitive-canopy regression case. Aggregate rejection-stage diagnostics
are now emitted so descriptor scarcity can be distinguished from fundamental,
homography, track-building, and PnP failures. Its acceptance gate is increased
registered-camera coverage without admitting geometrically invalid edges; a
full dense reconstruction is not expected until that upstream gate passes.

Next Stage 3 robustness gates:

- [x] Retain geometrically verified two-view tracks for initial-pair and
      disconnected-component bootstrapping instead of discarding them before
      SfM; continue requiring additional observations for incremental PnP.
- [ ] Recover or redownload the complete original validation flight and retain
      it outside disposable Downloads/build directories.
- [x] Add a repeatable per-pair overlap report containing capture identities,
      tentative matches, homography/fundamental inliers, and the rejection
      stage. It is written to
      `processed_images/kestrel_diagnostics/pair_matches.csv` even when the
      reconstruction cannot connect the complete flight. Connected-component
      membership remains available in the reconstruction artifact after SfM.
- [ ] Evaluate a second feature strategy for repetitive crop canopy against
      both the restored 25-capture folder and the original flight; accept it
      only when geometric precision and registered-camera coverage improve.
- [ ] Decouple valid epipolar SfM edges from the planar-preview homography graph
      so terrain/parallax does not discard otherwise useful 3D constraints.

### 4. True global bundle adjustment

- [x] Jointly optimize every non-gauge camera pose and sparse 3D point against pixel
      reprojection error; remove the planar homography assumption.
- [x] Optionally refine focal length, principal point, and Brown-Conrady
      distortion as shared sensor models. Use tight metadata-calibration
      priors, looser approximate-camera priors, and hard plausibility bounds
      to prevent calibration drift.
- [x] Add a Huber loss, iterative outlier removal, explicit gauge fixing, and
      convergence diagnostics.
- [x] Add robust, uncertainty-weighted GPS camera-position priors after ENU
      alignment without forcing poses to reproduce noisy GPS measurements
      exactly. Keep the two seed cameras fixed to preserve a numerically stable
      gauge while the priors resist drift in later registered cameras.
- [x] Eliminate 3D point blocks with an in-house Schur complement.
- [x] Replace the remaining dense camera solve with an in-house sparse 6-by-6
      block Schur system and block-Jacobi preconditioned conjugate-gradient
      solver. No dense reduced-camera matrix is allocated.
- [x] Persist calibrated poses, points, residual statistics, and rejected
      observations as a reconstruction artifact.

Completed real-image check (2026-07-14): on the 12-capture smoke subset, 3D
bundle adjustment kept all 12 cameras and 410 points while reducing calibrated
reprojection RMS from 0.4083 px to 0.3844 px. All 12 camera-position priors were
used and GPS RMS improved from 0.3966 m to 0.3960 m. One shared MicaSense sensor
model was refined within its metadata priors, the sparse PCG camera solve used
90 iterations, and no observation exceeded the 5 px outlier threshold. Stage 4
is ready for the 25-40 capture field-subset test.

Stage 4 field-subset gate (2026-07-14): captures `IMG_0083` through `IMG_0118`
processed 36 complete five-band captures in 187.74 seconds. All 36 cameras
registered in one GPS-aligned ENU component with 4,471 points and 17,296
observations; none were unregistered or rejected. Bundle-adjustment RMS changed
from 0.3922 px to 0.3917 px and GPS RMS from 0.7486 m to 0.7454 m. The shared
MicaSense calibration remained close to metadata, the sparse PCG solve used 75
iterations, and the 2,207-by-4,502 preview remained below the 50-million-pixel
canvas limit. This clears the Stage 4 full-flight gate for all 191 captures.

Stage 4 full-flight milestone (2026-07-14): all 191 complete captures processed
in 1,392.15 seconds (23 minutes 12 seconds) with an observed working-set high
point of about 891 MiB. Every camera registered in one GPS-aligned ENU component
containing 31,591 sparse points and 150,369 observations. Bundle-adjustment RMS
remained subpixel (0.4639 px to 0.4643 px), GPS RMS improved from 0.8136 m to
0.8129 m, one 5.18 px feature observation was rejected, and the shared MicaSense
calibration stayed close to metadata. The sparse PCG camera solve used 91
iterations. The 4,069-by-5,517 preview remained below the 50-million-pixel
canvas limit. The independent planar preview measured 66.0 px RMS and could not
improve it, confirming that this full-flight image must not be treated as an
orthomosaic or compared visually with ODM. The 3D Stage 4 milestone passes;
visual improvement now depends on Stages 5-8.

Fast validation: deterministic synthetic scenes with perturbed initial poses,
an incorrect approximate calibration, and a 32-camera local-overlap network.

### 5. Dense multi-view stereo baseline

The boxes in this stage establish a complete baseline data path and bounded
execution. Q2, Q3, and Q0 determine whether its depth and final imagery are
actually competitive with ODM.

- [x] Select useful neighbouring cameras for each reference view using shared
      sparse support, component membership, and robust triangulation-angle
      scoring while rejecting excessive baselines.
- [x] Compute initial calibrated depth maps with deterministic multi-scale
      plane-sweep matching, inverse-depth hypotheses, local normalized cross
      correlation, sparse-supported depth bounds, confidence, and validity
      masks. PatchMatch refinement remains optional after real-image tuning.
- [x] Apply forward/backward and multi-view depth consistency checks with
      round-trip reprojection limits, explicit occlusion handling, per-pixel
      consistent/occluded-view counts, and confidence filtering.
- [x] Fuse consistent depth maps into a metric world-frame point cloud using
      confidence-weighted voxels, distinct-camera support, and bounded
      per-camera source-pixel provenance.
- [x] Filter low-confidence, spatially isolated, statistically sparse, and
      locally implausible elevation points while preserving source-camera and
      source-pixel provenance on every survivor.
- Spectral sampling is deferred to Stage 9: geometry already preserves bounded
  source-camera/source-pixel provenance, while orthorectification samples the
  original images directly. Calibrated band values become a gate for
  multispectral exports and NDVI, not for DSM geometry.
- [x] Process reference depth maps in deterministic non-overlapping core tiles
      with automatically derived pyramid/patch halos. Persist signature-checked
      raw depth/confidence tile checkpoints so interrupted or repeated runs can
      resume without accepting stale imagery, calibration, pose, range, or
      algorithm settings. Global consistency now releases raw maps, reloads
      only one reference and its selected neighbours, writes a separately
      fingerprinted filtered-tile stage, and leaves lightweight descriptors
      for one-map-at-a-time fusion. Raw and consistent writes use temporary
      files plus rename, and truncated/corrupt tiles are rejected and safely
      regenerated. This bounds plane-sweep and consistency depth-map memory;
      the fused voxel accumulator and later products remain resident.

This is one of the hardest components. OpenCV primitives are appropriate, but
a proven MVS library may be justified if the in-house implementation cannot
reach reliable depth on low-texture crops after substantial effort.

Fast validation: rendered height fields, planes, boxes, and a cropped real
subset. Do not use the full flight while tuning inner depth-map loops.

Initial Stage 5 check (2026-07-14): the backend-neutral in-house MVS interface
passed a deterministic textured-plane scene. It retained the two supported
neighbours, rejected an excessive baseline, derived a robust range containing
the true depth, recovered the plane within 0.12 m, and emitted typed depth,
confidence, validity, and source-view provenance artifacts. Real-image depth
quality is intentionally deferred until the consistency batch is validated.

Consistency check (2026-07-14): deterministic synthetic geometry passed for a
shared plane, a partially occluded plane, and contradictory depths. Raw finite
depth hypotheses now survive until geometric consistency, after which
depth-error, round-trip reprojection, minimum-view, and confidence thresholds
produce the final validity mask. The focused dense-reconstruction test and all
13 project tests pass.

Real-flight MVS smoke check (2026-07-14): the reusable manual smoke runner
loaded the saved Stage 3 reconstruction and processed the central 75 percent of
green-band captures `IMG_0087` through `IMG_0091` at 480-by-360 pixels. All five
depth maps completed in 9.47 seconds. Consistency retained 149,086 of 229,803
raw hypotheses (64.9 percent); per-image final coverage was 4.2-28.2 percent,
and median camera-space depth stayed within 13.43-13.53 m. Valid regions form
the expected bands where at least two other captures overlap the cropped
linear flight, while edge captures have less support. This passes the first
real-data execution and geometry-consistency gate, not an ODM-quality gate.
Diagnostic depth, confidence, validity PNGs, and `summary.yml` are written by
`KestrelDenseReconstructionRealSmoke`. Filtering and DSM handoff validation are
recorded below; no full-flight MVS run is warranted yet.

Initial fusion check (2026-07-14): deterministic tests preserve a known plane,
verify confidence-weighted positions, retain unique camera provenance, and
reject voxels supported by only one camera. On the same five-capture real
smoke subset, 149,086 consistent depth samples formed 7,934 candidate 0.10 m
voxels; 5,237 voxels passed the two-distinct-camera requirement with an average
2.54 camera observations and 27.45 contributing pixels. The reusable runner
exports this ENU/world-frame result as `fused_point_cloud.ply`. Its elevation
spread contains terrain/depth outliers, so filtering remains explicit instead
of being hidden inside fusion.

Filtering and DSM handoff check (2026-07-14): synthetic sloped-terrain tests
retain valid boundary points and camera provenance while rejecting a distant
point, a sparse satellite cluster, a low-confidence point, and a local height
spike. On the real smoke cloud, robust neighbourhood filtering retained 4,379
of 5,237 fused points after rejecting 14 isolated points, 677 mean-distance
outliers, and 167 local elevation outliers. The survivors rasterized into a
95-by-81 diagnostic 0.10 m DSM with 1,971 directly measured cells. The runner
writes `filtered_point_cloud.ply`, `filtered_dsm.yml.gz`, and a height-preview
PNG. This validates the point-cloud-to-DSM handoff; spectral sampling, bounded
hole policy tuning on real data, and application orchestration remain open.

Tiled/resumable MVS check (2026-07-14): a 96-by-72 synthetic plane produced
identical untiled and nine-tile depth, confidence, and validity outputs. A
second run resumed all nine signature-matched checkpoints, and an undersized
explicit halo was rejected. The five-capture real diagnostic processed 30
192-pixel core tiles with a derived 16-pixel halo and exactly reproduced the
previous 310,430 raw and 218,236 consistent pixels. Its second invocation
resumed all 30 tiles; end-to-end diagnostic runtime fell from 22.4 seconds on
the checkpoint-writing pass to 7.1 seconds on the resumed pass.

Higher-resolution medium gate (2026-07-14): ten consecutive captures
`IMG_0086` through `IMG_0095` ran at the complete 960-by-720 central crop (a
1,024-pixel cap caused no downscaling) in 60 384-pixel core tiles. Dense MVS
took 122.73 seconds, retaining 1,161,852 of 1,914,668 raw hypotheses (60.7
percent). Fusion produced 18,204 points and filtering retained 17,073; 6,049
of 6,057 measured DSM cells received orthophoto imagery. The last two subset
captures had under one-percent coverage because the artificial window omitted
their later flight neighbours, while interior captures retained 11.7-38.2
percent. A repeated invocation resumed all 60 tiles and reduced dense-stage
time to 9.33 seconds with identical output.

Full-flight dense milestone (2026-07-14): all 191 captures completed end to
end at the memory-safe 480-by-360 diagnostic resolution. The first pass wrote
1,146 192-pixel-core checkpoints and completed dense reconstruction in 854.33
seconds (14 minutes 14 seconds), retaining 19,691,838 of 26,684,311 raw
hypotheses (73.8 percent). Fusion produced 306,856 points; filtering retained
296,875 after rejecting 778 isolated, 9,056 statistical, and 147 local-height
outliers. The resulting 367-by-459 DSM had 130,046 measured cells, all of
which received imagery in the tiled orthophoto. A second pass resumed all
1,146 tiles and reduced the dense stage to 113.90 seconds; global consistency
over all maps was then the dominant non-checkpointed dense cost. The output is a
coherent field footprint with some internal no-data holes, but its noisy
single-band texture is still not an ODM-quality or survey-deliverable result.
The previous all-resident 960-by-720 flight would have required about 1.45 GB
for depth maps before imagery and downstream structures.

Checkpoint-backed consistency milestone (2026-07-14): synthetic tests exactly
match resident depth, confidence, validity, consistent-view, and occlusion
outputs; repeat runs resume both stages, changed consistency settings reuse raw
tiles while invalidating only filtered tiles, and a deliberately truncated
tile is regenerated without crashing. The 10-capture 960-by-720 gate retained
the same 1,161,852 pixels and 18,204 fused points, wrote 60 filtered tiles, and
resumed all 60 on its next run. The 191-capture 480-by-360 flight then resumed
all 1,146 raw tiles, wrote 1,146 filtered tiles in a bounded working set, and
exactly reproduced 19,691,838 consistent pixels, 306,856 fused points, and
296,875 filtered points. Its first checkpoint-backed dense stage took 390.54
seconds; a repeat resumed all raw and filtered tiles in 124.06 seconds. The
full-resolution flight is no longer blocked by resident depth-map memory, but
should follow an I/O optimization pass because repeated compressed neighbour
loads and diagnostic reloads remain expensive.

### 6. DSM construction baseline

This stage currently produces a diagnostic raster surface. Q3 contains the
remaining surface-fidelity criteria for a release orthophoto.

- [x] Rasterize a dense point cloud into a configurable-resolution top-surface
      DSM while ignoring non-finite and out-of-bounds points.
- [x] Reject statistical spatial outliers and robust local-elevation spikes
      before DSM rasterization.
- [x] Interpolate only small, fully enclosed DSM holes; retain large and
      border-connected gaps as invalid and record synthesized cells in a mask.
- [x] Preserve explicit validity and per-cell sample-count masks instead of
      inventing elevations in missing or undersampled regions.
- [ ] Optionally classify ground and generate a DTM later; a DSM is sufficient
      for the first terrain-aware orthophoto.
- [x] Store the DSM as a projected float GeoTIFF with explicit no-data and
      validity-alpha metadata.

Fast validation: analytic surfaces, deliberate holes/outliers, and a small
dense cloud fixture.

### 7. Multi-camera terrain orthorectification baseline

Projection geometry, ownership, and blending are implemented. Q4 remains the
quality gate for source leveling, seam optimality, ghost suppression, and
sharpness.

- [x] Sample a DSM for every output ground pixel.
- [x] Project terrain through calibrated pose, intrinsics, and distortion.
- [x] Build a validity mask and resample a single image.
- [x] Derive a north-up output extent and ground sample distance from terrain
      bounds, with configurable metric padding.
- [x] Keep the orthophoto GSD independent from DSM spacing and retain the exact
      output grid (size, metric origin, and signed pixel spacing) with every
      result so downstream writers do not reconstruct georeferencing from
      implicit state.
- [x] Reject terrain points occluded from a source camera using bounded DSM
      ray sampling with explicit unknown-terrain policy and clearance.
- [x] Score all covering cameras by view angle, resolution, distance to image
      borders, depth confidence, and exposure quality.
- [x] Select a deterministic bounded set of strongest sources and combine them
      with score-weighted float accumulation while retaining primary-source,
      contribution-count, validity, and accumulated-quality diagnostics.
- [x] Find deterministic spatially coherent seams with a quality,
      smoothness, and photometric-disagreement objective; blend the selected
      sources with Gaussian-mask/Laplacian-image pyramids while retaining the
      weighted float path as a diagnostic fallback.
- [x] Process output in deterministic non-overlapping ownership tiles with an
      automatically derived overlap halo. Snap expanded regions to the
      coarsest pyramid lattice, optimize/blend with the halo present, and copy
      only each core so multiband and seam results remain continuous while
      per-source warp/blend scratch memory is bounded. The final output and
      diagnostic rasters remain resident until the GeoTIFF writer becomes
      tile-streaming in Stage 8.

Fast validation: synthetic DSM/camera arrangements and tiny image tiles. The
higher-resolution medium gate and reduced-resolution 191-capture full-flight
gate now pass; visual parity still depends on the remaining GIS, radiometric,
and quality stages.

Initial multi-camera orthorectification check (2026-07-14): deterministic
tests pass for equal-score blending, quality-weighted selection, stable tie
breaking, RGB type preservation, and a terrain ridge that occludes only the
oblique camera. On the five-capture real smoke subset, 1,969 of 1,971 directly
measured DSM cells received imagery. The selector retained 5,778 contributions
from 5,912 projected candidates and rejected three candidates as occluded.
The diagnostic orthophoto still follows three sparse overlap strips and is not
an ODM-quality visual result, but the DSM-to-multi-camera geometry, scoring,
occlusion, and weighted blending path executes end to end.

Seam/blending check (2026-07-14): focused tests pass for deterministic ties,
a single coherent boundary across crossing source-quality fields, preservation
of source interiors through five-band pyramid reconstruction, invalid inputs,
and the complete camera/DSM integration. A repeat five-capture diagnostic on
the saved Stage 4 subset textured all 3,062 measured DSM cells. Seam ICM
converged in five passes after 523 label changes and marked 1,088 boundary
pixels for four-level multiband blending. The diagnostic remains three sparse
overlap strips because the subset DSM itself is sparse. Calibrated spectral
sampling remains required before multispectral/NDVI output, but no longer
blocks a single-band geometric full-flight test.

Overlap-tile check (2026-07-14): non-divisible synthetic output grids preserve
untiled validity, selected-source labels, weighted pixels, seam labels, and
multiband pixels across tile boundaries. The five-capture diagnostic processed
the 98-by-84 DSM in six 48-pixel core tiles with a derived 38-pixel halo while
preserving all 3,062 textured cells and the existing 1,088-pixel seam mask.
On the 191-capture diagnostic DSM, 80 overlap-halo tiles textured all 130,046
measured cells, selected 390,136 contributions from 1,164,057 projected
candidates, rejected 58 occluded candidates, and marked 81,417 seam pixels.

Independent-GSD architecture check (2026-07-14): the real-flight runner now
accepts `orthophoto-gsd-metres` separately from dense-image resolution, fusion
voxel size, and the 0.10 m DSM. Reusing the five-capture checkpoints at 0.05 m
produced a 195-by-167 orthophoto instead of the DSM's 98-by-84 grid, with
11,199 valid output pixels across 20 overlap-halo tiles. All orthophoto results
now retain their grid, and synthetic tiled/untiled tests verify the geometry is
preserved exactly. This clears the resolution-coupling gate; it does not yet
provide projected-CRS or GeoTIFF tags.

### 8. Georeferencing and GIS output baseline

- [x] Choose the WGS 84 UTM zone from the saved flight origin and explicitly
      linearize local ENU into that projected CRS. Preserve grid convergence
      and scale in a raster-to-model transformation, record the EPSG code, and
      report projection-curvature error over the output footprint.
- [x] Write GeoTIFF 1.1 model transformation, projected-CRS keys,
      PixelIsArea semantics, no-data, validity alpha, sample format, and
      photometric metadata for the DSM and orthophoto. Arbitrary ordered float
      bands now retain names and centre wavelengths in GDAL metadata.
- [ ] Add GCP/checkpoint import and residual reporting; support RTK accuracy
      values when present.
- [ ] Generate overviews and optional Cloud-Optimized GeoTIFF output.
- [ ] Add project boundary/cutline generation and crop the final raster.

Fast validation: round trips through known coordinate fixtures and inspection
with GDAL-based tests; the deployed app does not need a separate GDAL install.

Initial GIS-output check (2026-07-14): WGS 84 central-meridian, hemisphere,
zone, EPSG, ENU orientation, pixel-corner, and affine-curvature fixtures pass.
The bundled-libtiff round trip verifies ModelTransformationTag,
GeoKeyDirectoryTag (projected model, PixelIsArea, and EPSG), GDAL no-data,
16-bit samples, and validity alpha. On the five-capture field subset Kestrel
wrote both a float DSM and 16-bit orthophoto in EPSG:32618; the measured local
ENU-to-UTM affine error across the orthophoto was 0.000000215 m. The writer is
self-contained and requires no GDAL installation. Independent GDAL/QGIS
inspection remains a release-validation step, not a runtime dependency.

### 9. Multispectral radiometry and products baseline

- [x] Preserve the original unsigned samples through calibration input, use
      the TIFF bit depth in normalization, and retain calibrated radiance as
      float throughout terrain projection, blending, and GeoTIFF export.
- [x] Apply black-level, radial-vignette, row-gradient, exposure/gain, and
      sensor-specific radiometric-coefficient corrections from MicaSense
      metadata. Saturated/model-invalid pixels remain masked. `BandSensitivity`
      is retained for diagnostics but is not multiplied twice because the
      calibrated `a1` coefficient already carries sensor response.
- [ ] Support reflectance-panel and downwelling-light-sensor calibration.
  - [x] Implement the physical `reflectance = pi * radiance / irradiance`
        boundary, panel-radiance irradiance relation, strict ordered-band
        matching, and rejection of missing/unmeasured irradiance.
  - [ ] Detect panel captures, measure the certified panel ROI, and ingest
        time-matched/corrected DLS irradiance for arbitrary flights.
- [x] Warp every band using the same terrain solution while honoring calibrated
      inter-sensor poses.
  - [x] Blue/green/red sensors now use independent intrinsics, distortion, and
        rig-relative rotations while sharing the green-band physical-capture
        labels and seam decisions. Missing rig translation remains explicitly
        unknown and uses the shared-optical-centre approximation.
  - [x] Extend the shared-geometry path to ordered Blue, Green, Red, NIR, and
        Red-edge radiance, with independent sensor warps, common physical-
        capture ownership, aligned tile halos, and arbitrary-channel blending.
  - [ ] Populate measured rig translations when camera-specific calibration
        supplies them.
- [ ] Export a co-registered multiband reflectance GeoTIFF.
  - [x] Export the initial co-registered three-band float RGB-radiance GeoTIFF
        with projected CRS, validity alpha, and a separately stretched PNG
        preview. It is camera-calibrated radiance, not surface reflectance.
  - [x] Export an ordered five-band float radiance GeoTIFF with projected CRS,
        validity alpha, band names, centre wavelengths, and a shared-scale RGB
        preview. Reflectance remains gated on measured irradiance.
- [ ] Compute NDVI/NDRE only after reflectance calibration and co-registration.

Fast validation: per-pixel radiometric fixtures, metadata examples, and
synthetic band transforms.

Initial shared-geometry RGB check (2026-07-14): synthetic fixtures verified
independent sensor projection, original-sample preservation, shared capture
ownership, known/unknown rig-baseline handling, and tiled/untiled equivalence.

Camera-radiance check (2026-07-15): MicaSense reference-formula fixtures verify
black-level subtraction, radial vignette and row-gradient compensation,
exposure/gain/bit-depth scaling, crop-coordinate preservation, the documented
legacy exposure correction, saturation masks, and radiometric source fallback.
The real five-capture run produced a 177-by-165 EPSG:32618 float RGB-radiance
orthophoto with 21,961 valid pixels; four pixels required an availability
fallback from the green-selected capture. Its display stretch remains
green/cyan because narrow spectral radiance bands are not a color-managed RGB
photograph, and panel/DLS irradiance calibration is still required before
calling the values surface reflectance.

Five-band radiance check (2026-07-15): the flight contains 191 complete sets
of Blue, Green, Red, NIR, and Red-edge TIFFs (955 files total), but none of the
files contains panel or DLS irradiance metadata. Kestrel therefore reports
reflectance and NDVI as unavailable instead of inventing irradiance. Synthetic
tests verify arbitrary-channel terrain warping/blending, ordered float GeoTIFF
samples plus band metadata, the reflectance formula, panel irradiance, strict
band matching, and invalid-pixel propagation. The five-capture real run wrote
a 195-by-167, 0.05 m EPSG:32618 five-band radiance GeoTIFF with 11,197 valid
pixels, 15 availability fallbacks, and all five complete captures. Its
shared-scale RGB preview is spatially coherent but green-biased as expected
for unnormalized narrow-band radiance.

### 10. Reliability, scale, and quality reporting

- [ ] Cache every expensive stage with input/configuration fingerprints.
- [ ] Resume from the first invalid stage after a crash or setting change.
- [x] Cache raw plane-sweep and global-consistency tiles with exact dependency
      fingerprints, atomic replacement, corruption rejection, selective
      invalidation, and independent downstream loading.
- [ ] Add cancellation, disk/RAM estimates, bounded concurrency, and cleanup.
- [ ] Split large projects into overlapping spatial submodels and merge them.
- [ ] Produce a processing report with coverage, GSD, track counts, camera/GPS
      residuals, sparse/dense confidence, DSM gaps, and orthophoto provenance.
- [ ] Add regression thresholds and image-difference artifacts to CI tests.

## Recommended development/test cadence

1. Implement and run unit/synthetic tests continuously; they should finish in
   seconds and are not a substitute for the real flight.
2. Keep a hard-linked or copied 5-12 capture fixture for integration tests.
3. Use a 25-40 capture field subset at the end of each major stage.
4. Run all 191 captures only at major gates. The next all-flight quality run
   waits until Q2 demonstrates a dense-depth improvement at full source
   dimensions on the 10- and 36-capture subsets; another reduced-resolution
   run would mostly repeat the already-passed execution milestone.
5. Compare Kestrel with the pinned ODM output using the same crop, ground
   resolution, CRS, and 100% pixel zoom. Record runtime separately from quality.

This cadence lets several dependent features mature before an hours-long run,
without postponing all validation until failures become difficult to isolate.

## Third-party boundary

Continue implementing pipeline policy and data flow in Kestrel. Retain OpenCV
for codecs, calibrated geometry primitives, feature extraction, robust model
estimation, and resampling. Kestrel now has an in-house block-sparse bundle-
adjustment solver; keep measuring it on larger flights before considering a
numerical-library fallback. The remaining area where an additional native,
cross-platform dependency may be justified is dense multi-view stereo if the
in-house depth solver cannot meet correctness requirements.

Any added dependency must be built and packaged by CMake, require no separate
end-user installation, follow a measured in-house prototype, and include its
license in the deployment package.

## References

- [OpenDroneMap pipeline and source](https://github.com/OpenDroneMap/ODM)
- [ODM pipeline flowchart](https://docs.opendronemap.org/flowchart/)
- [ODM high-quality orthophoto guidance](https://docs.opendronemap.org/tutorials/)
- [ODM 3.6.0 outputs](https://docs.opendronemap.org/outputs/)
- [ODM multispectral processing](https://docs.opendronemap.org/multispectral/)
- [ODM map-accuracy guidance](https://docs.opendronemap.org/map-accuracy/)
- [ODM large-dataset split/merge](https://docs.opendronemap.org/large/)

# Kestrel

Kestrel is a Qt desktop app for organizing farm imagery into projects. It imports TIFF images, runs OpenDroneMap (ODM) to build an orthophoto, and displays an RGB preview in the Map view. NDVI and other analysis features are planned.

## Build

Use Qt 6.8+, CMake 3.21+, and a C++17 compiler matching your Qt kit. The active app build no longer compiles the experimental in-house photogrammetry engine or OpenMVS.

Windows with MinGW, from the repository root:

```powershell
cmake -S app/Kestrel -B app/Kestrel/build-mingw -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build app/Kestrel/build-mingw -j 2
& .\app\Kestrel\build-mingw\KestrelApp.exe
```

Adjust the Qt path for your installation. An existing configured build only needs the `cmake --build` command. On Linux, configure with your Qt GCC kit and run `./build/kestrel/KestrelApp`.

## Native ODM runtime

Docker Desktop is no longer needed. On Windows, install the [native ODM release](https://github.com/OpenDroneMap/ODM/releases) at its default `C:\ODM` location. If installed elsewhere, set `KESTREL_ODM_HOME` to that folder before launching Kestrel. Kestrel calls ODM's `run.bat` non-interactively and uses the GDAL included with ODM for the Map preview. On Linux, point `KESTREL_ODM_HOME` at a native ODM checkout/install containing `run.sh` and have `gdal_translate` on `PATH`.

A normal CMake build compiles Kestrel only; it does not build or bundle ODM yet. The development setup therefore still requires a separate native ODM installation. Full flights can take hours and require substantial disk space. Kestrel stages imported TIFFs in ODM's required `images` folder using hard links where possible (copying if hard links are unavailable). ODM output stays in the Kestrel project and failed runs can be retried.

## Use the app

1. Launch Kestrel and click **+** in the project tab bar.
2. Name the project and choose TIFF files or a folder of TIFFs.
3. Click **Create Project**. Kestrel copies the selected files into the project's `raw_images` folder and starts ODM automatically.
4. Watch the progress window during import and ODM processing. Its ODM portion follows ODM's own progress broadcasts; the bar becomes indeterminate if those broadcasts are unavailable. When processing finishes, the Map view displays the orthophoto preview.

To reopen an existing project, choose **File → Open Project** and select its `.kproj` file. Kestrel restores the project tab and refreshes the display preview from an existing ODM GeoTIFF without rerunning ODM. **File → Create Project** opens the new-project dialog.

The **Field History** panel lists flights by capture date, newest first. Click a row to view that flight; the newest is selected when a project opens. Use **+** in that panel to import another folder of TIFFs into the same field. Each flight keeps its own raw images and ODM output. An amber warning symbol means the sampled camera/band layout (rig, band name, wavelength, image dimensions, band count, and pixel type) differs from the earliest flight. This compares structure, not the number of photos or the appearance of the field. The date comes from image EXIF when available, otherwise from import time. Older single-flight `.kproj` files remain readable (they initially show the project's creation date) and are upgraded when a flight is added.

In the Map view, use the mouse wheel or the zoom buttons to zoom, drag to pan, and use the fit button to reset the view. The bottom bar shows the cursor's projected map coordinates, ground units per display pixel, and the orthophoto CRS when GDAL georeferencing is available. Kestrel displays the PNG overview immediately and loads a sharper visible crop from the ODM GeoTIFF when zoomed in; zooming beyond the GeoTIFF's native pixels cannot add detail.

Each imported file must have a unique filename; project creation currently flattens selected folders into `raw_images`.

To reprocess an existing project from a terminal:

```powershell
& .\app\Kestrel\build-mingw\KestrelApp.exe --stitch-project `
  "C:\Users\you\Documents\Kestrel Projects\my project"
```

To regenerate only its PNG preview from a completed ODM GeoTIFF (no photogrammetry run):

```powershell
& .\app\Kestrel\build-mingw\KestrelApp.exe --refresh-preview `
  "C:\Users\you\Documents\Kestrel Projects\my project"
```

Project products are written to:

```text
<project>/
  <project-name>.kproj                        flight history and project metadata
  raw_images/                                imported TIFFs
  processed_images/odm/
    odm-console.log                          ODM run log
    odm_orthophoto/odm_orthophoto.tif        full georeferenced ODM output
    odm_orthophoto/odm_rgb_preview.png      Map view preview
  flights/<flight-id>/
    raw_images/                              additional flight TIFFs
    processed_images/odm/                    that flight's ODM products
```

The PNG is an 8-bit display preview using output bands 1–3, resampled from the GeoTIFF's full-resolution pixels. If ODM supplies an alpha band, Kestrel preserves it so the area outside the mosaic footprint is transparent. Use the GeoTIFF for GIS and spectral analysis. The preview supports Byte and UInt16 ODM orthophotos with at least three color bands. The Map view uses the GeoTIFF for zoom detail; NDVI is not yet available in the UI.

## Archived research

The previous in-house photogrammetry engine, OpenMVS integration work, and diagnostic tests are preserved under [`archive/photogrammetry`](archive/photogrammetry). They are excluded from the active desktop build. The prior README and CMake configuration are saved there as well. The separate [`tools/odm-comparison`](tools/odm-comparison) benchmark remains available for independent ODM comparisons.

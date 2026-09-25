# Archived Kestrel photogrammetry research

This directory preserves the in-house photogrammetry pipeline as it stood when the desktop app switched to ODM. It includes the previous mosaic controller, camera and TIFF metadata handling, sparse reconstruction and bundle adjustment, dense MVS and OpenMVS integration, terrain orthorectification, radiometry, and the corresponding diagnostic tests. The files are not part of the active desktop build.

`legacy-CMakeLists.txt` and `legacy-App-CMakeLists.txt` capture the previous build wiring, including local changes that had not been committed. `legacy-README.md` and `PHOTOGRAMMETRY_ROADMAP.md` preserve the prior documentation. To revive the research, restore its source tree under `app/Kestrel/src/photogrammetry`, its tests under `app/Kestrel/tests`, and adapt the saved CMake files to the current app. This archive is source code, not a maintained ODM fallback.

The active import-to-ODM adapter is `app/Kestrel/src/processing/StitchingController.cpp`.

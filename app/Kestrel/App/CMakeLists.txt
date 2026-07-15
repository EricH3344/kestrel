
target_sources(${CMAKE_PROJECT_NAME} PUBLIC
    "main.cpp"
    "windowcontroller.h"
    "windowcontroller.cpp"
    "../src/application/SystemAlert.h"
    "../src/application/SystemAlert.cpp"
    "../src/application/CreateProjectController.h"
    "../src/application/CreateProjectController.cpp"
    "../src/application/PathHelper.h"
    "../src/application/PathHelper.cpp"
    "../src/application/FileDialogHelper.h"
    "../src/application/FileDialogHelper.cpp"
    "../src/project/ProjectCreator.h"
    "../src/project/ProjectCreator.cpp"
    "../src/project/ProjectLoader.h"
    "../src/project/ProjectLoader.cpp"
    "../src/photogrammetry/geo/GeoCoordinates.h"
    "../src/photogrammetry/geo/GeoCoordinates.cpp"
    "../src/photogrammetry/geo/ProjectedCoordinates.h"
    "../src/photogrammetry/geo/ProjectedCoordinates.cpp"
    "../src/photogrammetry/camera/CameraCalibration.h"
    "../src/photogrammetry/camera/CameraCalibration.cpp"
    "../src/photogrammetry/camera/RigCameraModel.h"
    "../src/photogrammetry/camera/RigCameraModel.cpp"
    "../src/photogrammetry/io/CaptureGrouping.h"
    "../src/photogrammetry/io/CaptureGrouping.cpp"
    "../src/photogrammetry/io/TiffImageMetadata.h"
    "../src/photogrammetry/io/TiffImageMetadata.cpp"
    "../src/photogrammetry/io/GeoTiffWriter.h"
    "../src/photogrammetry/io/GeoTiffWriter.cpp"
    "../src/photogrammetry/radiometry/MicaSenseRadiometricCalibrator.h"
    "../src/photogrammetry/radiometry/MicaSenseRadiometricCalibrator.cpp"
    "../src/photogrammetry/radiometry/ReflectanceCalibrator.h"
    "../src/photogrammetry/radiometry/ReflectanceCalibrator.cpp"
    "../src/photogrammetry/sfm/FeatureTracks.h"
    "../src/photogrammetry/sfm/FeatureTracks.cpp"
    "../src/photogrammetry/sfm/SparseReconstruction.h"
    "../src/photogrammetry/sfm/SparseReconstruction.cpp"
    "../src/photogrammetry/sfm/ReconstructionAlignment.h"
    "../src/photogrammetry/sfm/ReconstructionAlignment.cpp"
    "../src/photogrammetry/sfm/BundleAdjustment.h"
    "../src/photogrammetry/sfm/BundleAdjustment.cpp"
    "../src/photogrammetry/mvs/DenseReconstruction.h"
    "../src/photogrammetry/mvs/DenseReconstruction.cpp"
    "../src/photogrammetry/mvs/DepthMapFusion.h"
    "../src/photogrammetry/mvs/DepthMapFusion.cpp"
    "../src/photogrammetry/mvs/DensePointCloudFilter.h"
    "../src/photogrammetry/mvs/DensePointCloudFilter.cpp"
    "../src/photogrammetry/tiling/TileLayout.h"
    "../src/photogrammetry/tiling/TileLayout.cpp"
    "../src/photogrammetry/mosaic/StitchingController.h"
    "../src/photogrammetry/mosaic/StitchingController.cpp"
    "../src/photogrammetry/terrain/TerrainOrthorectifier.h"
    "../src/photogrammetry/terrain/TerrainOrthorectifier.cpp"
    "../src/photogrammetry/terrain/MultiCameraOrthorectifier.h"
    "../src/photogrammetry/terrain/MultiCameraOrthorectifier.cpp"
    "../src/photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h"
    "../src/photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.cpp"
    "../src/photogrammetry/terrain/SharedGeometryRgbOrthorectifier.h"
    "../src/photogrammetry/terrain/SharedGeometryRgbOrthorectifier.cpp"
    "../src/photogrammetry/terrain/OrthophotoSeamBlender.h"
    "../src/photogrammetry/terrain/OrthophotoSeamBlender.cpp"
    "../src/photogrammetry/terrain/TerrainModelBuilder.h"
    "../src/photogrammetry/terrain/TerrainModelBuilder.cpp"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    ${KESTREL_OPENCV_INCLUDE_DIRS}
    ${KESTREL_LIBTIFF_INCLUDE_DIRS}
)

if (KESTREL_LIBTIFF_TARGET)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        KESTREL_HAS_LIBTIFF_GPS KESTREL_HAS_LIBTIFF_GEOTIFF)
endif()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_features2d
    opencv_calib3d
    opencv_flann
    opencv_video
    ${KESTREL_LIBTIFF_TARGET})

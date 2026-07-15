
cmake_minimum_required(VERSION 3.21.1)

option(LINK_INSIGHT "Link Qt Insight Tracker library" ON)
option(BUILD_QDS_COMPONENTS "Build design studio components" ON)
option(KESTREL_FETCH_OPENCV "Download and build OpenCV when it is not installed" ON)

project(KestrelApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(_kestrel_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT _kestrel_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "CMake build type" FORCE)
endif()

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive- /Zc:__cplusplus")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)
set(QML_IMPORT_PATH ${QT_QML_OUTPUT_DIRECTORY}
    CACHE STRING "Import paths for Qt Creator's code model"
    FORCE
)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent Gui Widgets Qml Quick QuickTimeline ShaderTools)
qt_standard_project_setup()

# Kestrel owns the mosaic pipeline. OpenCV is used only for the difficult
# computer-vision primitives (features, robust homographies, warping and TIFF
# codecs). Static linking keeps the installed application self-contained.
find_package(OpenCV 4.8 QUIET COMPONENTS core imgproc imgcodecs features2d calib3d flann video)
if (OpenCV_FOUND)
    set(KESTREL_OPENCV_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
else()
    if (NOT KESTREL_FETCH_OPENCV)
        message(FATAL_ERROR "OpenCV 4.8+ is required. Enable KESTREL_FETCH_OPENCV or provide OpenCV_DIR.")
    endif()

    include(FetchContent)
    set(BUILD_LIST "core,imgproc,imgcodecs,features2d,calib3d,flann,video" CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
    set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_gapi OFF CACHE BOOL "" FORCE)
    set(OPENCV_PYTHON_SKIP_DETECTION ON CACHE BOOL "" FORCE)
    set(WITH_ADE OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
    set(WITH_JASPER OFF CACHE BOOL "" FORCE)
    set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
    set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
    set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)
    set(WITH_PROTOBUF OFF CACHE BOOL "" FORCE)
    set(WITH_VTK OFF CACHE BOOL "" FORCE)
    set(WITH_WEBP OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBJPEG_TURBO_SIMD OFF CACHE BOOL "" FORCE)
    # CMake's standard URL handler extracts to a temporary directory and then
    # renames it. Real-time scanners can hold the large OpenCV tree open on
    # Windows and make that rename fail, so extract directly to the final
    # FetchContent source directory instead.
    set(_kestrel_opencv_source_dir "${FETCHCONTENT_BASE_DIR}/opencv-src")
    FetchContent_Declare(opencv
        SOURCE_DIR "${_kestrel_opencv_source_dir}"
        SOURCE_SUBDIR opencv-4.10.0
        DOWNLOAD_COMMAND
            "${CMAKE_COMMAND}"
            "-DOPENCV_SOURCE_DIR=${_kestrel_opencv_source_dir}"
            "-DOPENCV_DOWNLOAD_DIR=${CMAKE_BINARY_DIR}/downloads"
            -DOPENCV_VERSION=4.10.0
            -DOPENCV_SHA256=b2171af5be6b26f7a06b1229948bbb2bdaa74fcf5cd097e0af6378fce50a6eb9
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchOpenCV.cmake"
    )
    # Do not let Kestrel's Qt code-generation settings leak into OpenCV's
    # plain C/C++ codec targets.
    set(_kestrel_automoc "${CMAKE_AUTOMOC}")
    set(CMAKE_AUTOMOC OFF)
    FetchContent_MakeAvailable(opencv)
    set(CMAKE_AUTOMOC "${_kestrel_automoc}")
    set(KESTREL_BUNDLED_OPENCV_SOURCE_DIR
        "${_kestrel_opencv_source_dir}/opencv-4.10.0")

    function(_kestrel_disable_qt_codegen directory)
        get_property(directory_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(directory_target IN LISTS directory_targets)
            set_target_properties("${directory_target}" PROPERTIES
                AUTOMOC OFF
                AUTOUIC OFF
                AUTORCC OFF
            )
        endforeach()

        get_property(child_directories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(child_directory IN LISTS child_directories)
            _kestrel_disable_qt_codegen("${child_directory}")
        endforeach()
    endfunction()
    _kestrel_disable_qt_codegen("${_kestrel_opencv_source_dir}/opencv-4.10.0")

    foreach(opencv_target IN ITEMS
            opencv_core opencv_imgproc opencv_imgcodecs opencv_features2d
            opencv_calib3d opencv_flann opencv_video
            zlib libpng libtiff libjpeg-turbo jpeg12-static jpeg16-static)
        if (TARGET "${opencv_target}")
            set_target_properties("${opencv_target}" PROPERTIES
                AUTOMOC OFF
                AUTOUIC OFF
                AUTORCC OFF
            )
        endif()
    endforeach()

    set(KESTREL_OPENCV_INCLUDE_DIRS
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/core/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/imgproc/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/imgcodecs/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/features2d/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/calib3d/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/flann/include"
        "${_kestrel_opencv_source_dir}/opencv-4.10.0/modules/video/include"
        "${FETCHCONTENT_BASE_DIR}/opencv-build"
    )
endif()

if (NOT KESTREL_BUNDLED_OPENCV_SOURCE_DIR
    AND OpenCV_SOURCE_DIR
    AND EXISTS "${OpenCV_SOURCE_DIR}/3rdparty/libtiff/tiffio.h")
    # On a subsequent configure, OpenCV's generated package is found before
    # FetchContent runs. Preserve the source location so bundled GPS metadata
    # support remains enabled in incremental builds too.
    set(KESTREL_BUNDLED_OPENCV_SOURCE_DIR "${OpenCV_SOURCE_DIR}")
endif()

if (TARGET libtiff AND KESTREL_BUNDLED_OPENCV_SOURCE_DIR)
    set(KESTREL_LIBTIFF_TARGET libtiff)
    set(KESTREL_LIBTIFF_INCLUDE_DIRS
        "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/3rdparty/libtiff"
        "${FETCHCONTENT_BASE_DIR}/opencv-build/3rdparty/libtiff")
endif()


qt_add_executable(${CMAKE_PROJECT_NAME})
qt_add_resources(${CMAKE_PROJECT_NAME} "configuration"
    PREFIX "/"
    FILES
        qtquickcontrols2.conf)

include(qds)

if (BUILD_QDS_COMPONENTS)
    include(qmlcomponents OPTIONAL)
endif()

if (LINK_INSIGHT)
    include(insight OPTIONAL)
endif ()

include(GNUInstallDirs)
install(TARGETS ${CMAKE_PROJECT_NAME}
  BUNDLE DESTINATION . COMPONENT KestrelRuntime
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT KestrelRuntime
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT KestrelRuntime
)

qt_generate_deploy_qml_app_script(
    TARGET ${CMAKE_PROJECT_NAME}
    OUTPUT_SCRIPT kestrel_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${kestrel_deploy_script} COMPONENT KestrelRuntime)

include(CTest)
if (BUILD_TESTING)
    add_executable(KestrelTerrainOrthorectifierTest
        tests/TerrainOrthorectifierTest.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.h)
    target_include_directories(KestrelTerrainOrthorectifierTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelTerrainOrthorectifierTest PRIVATE
        opencv_core opencv_imgproc)
    set_target_properties(KestrelTerrainOrthorectifierTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME terrain_orthorectification
             COMMAND KestrelTerrainOrthorectifierTest)

    add_executable(KestrelMultiCameraOrthorectifierTest
        tests/MultiCameraOrthorectifierTest.cpp
        src/photogrammetry/terrain/MultiCameraOrthorectifier.cpp
        src/photogrammetry/terrain/MultiCameraOrthorectifier.h
        src/photogrammetry/terrain/OrthophotoSeamBlender.cpp
        src/photogrammetry/terrain/OrthophotoSeamBlender.h
        src/photogrammetry/tiling/TileLayout.cpp
        src/photogrammetry/tiling/TileLayout.h
        src/photogrammetry/terrain/TerrainOrthorectifier.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.h)
    target_include_directories(KestrelMultiCameraOrthorectifierTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelMultiCameraOrthorectifierTest PRIVATE
        opencv_core opencv_imgproc)
    set_target_properties(KestrelMultiCameraOrthorectifierTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME multi_camera_orthorectification
             COMMAND KestrelMultiCameraOrthorectifierTest)

    add_executable(KestrelSharedGeometryRgbOrthorectifierTest
        tests/SharedGeometryRgbOrthorectifierTest.cpp
        src/photogrammetry/terrain/SharedGeometryRgbOrthorectifier.cpp
        src/photogrammetry/terrain/SharedGeometryRgbOrthorectifier.h
        src/photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.cpp
        src/photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h
        src/photogrammetry/terrain/MultiCameraOrthorectifier.cpp
        src/photogrammetry/terrain/MultiCameraOrthorectifier.h
        src/photogrammetry/terrain/OrthophotoSeamBlender.cpp
        src/photogrammetry/terrain/OrthophotoSeamBlender.h
        src/photogrammetry/terrain/TerrainOrthorectifier.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.h
        src/photogrammetry/tiling/TileLayout.cpp
        src/photogrammetry/tiling/TileLayout.h)
    target_include_directories(KestrelSharedGeometryRgbOrthorectifierTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelSharedGeometryRgbOrthorectifierTest PRIVATE
        opencv_core opencv_imgproc)
    set_target_properties(KestrelSharedGeometryRgbOrthorectifierTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME shared_geometry_rgb_orthorectification
             COMMAND KestrelSharedGeometryRgbOrthorectifierTest)

    add_executable(KestrelOrthophotoSeamBlenderTest
        tests/OrthophotoSeamBlenderTest.cpp
        src/photogrammetry/terrain/OrthophotoSeamBlender.cpp
        src/photogrammetry/terrain/OrthophotoSeamBlender.h)
    target_include_directories(KestrelOrthophotoSeamBlenderTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelOrthophotoSeamBlenderTest PRIVATE
        opencv_core opencv_imgproc)
    set_target_properties(KestrelOrthophotoSeamBlenderTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME orthophoto_seam_blending
             COMMAND KestrelOrthophotoSeamBlenderTest)

    add_executable(KestrelGeoCoordinatesTest
        tests/GeoCoordinatesTest.cpp
        src/photogrammetry/geo/GeoCoordinates.cpp
        src/photogrammetry/geo/GeoCoordinates.h
        src/photogrammetry/geo/ProjectedCoordinates.cpp
        src/photogrammetry/geo/ProjectedCoordinates.h)
    target_include_directories(KestrelGeoCoordinatesTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelGeoCoordinatesTest PRIVATE opencv_core)
    set_target_properties(KestrelGeoCoordinatesTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME geo_coordinates COMMAND KestrelGeoCoordinatesTest)

    add_executable(KestrelGeoTiffWriterTest
        tests/GeoTiffWriterTest.cpp
        src/photogrammetry/io/GeoTiffWriter.cpp
        src/photogrammetry/io/GeoTiffWriter.h
        src/photogrammetry/geo/ProjectedCoordinates.cpp
        src/photogrammetry/geo/ProjectedCoordinates.h
        src/photogrammetry/geo/GeoCoordinates.cpp
        src/photogrammetry/geo/GeoCoordinates.h)
    target_include_directories(KestrelGeoTiffWriterTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS}
        ${KESTREL_LIBTIFF_INCLUDE_DIRS})
    target_link_libraries(KestrelGeoTiffWriterTest PRIVATE
        opencv_core opencv_imgcodecs ${KESTREL_LIBTIFF_TARGET})
    if (KESTREL_LIBTIFF_TARGET)
        target_compile_definitions(KestrelGeoTiffWriterTest PRIVATE
            KESTREL_HAS_LIBTIFF_GEOTIFF)
    endif()
    set_target_properties(KestrelGeoTiffWriterTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME geotiff_writer COMMAND KestrelGeoTiffWriterTest)

    add_executable(KestrelTerrainModelBuilderTest
        tests/TerrainModelBuilderTest.cpp
        src/photogrammetry/terrain/TerrainModelBuilder.cpp
        src/photogrammetry/terrain/TerrainModelBuilder.h
        src/photogrammetry/terrain/TerrainOrthorectifier.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.h)
    target_include_directories(KestrelTerrainModelBuilderTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelTerrainModelBuilderTest PRIVATE
        opencv_core opencv_imgproc)
    set_target_properties(KestrelTerrainModelBuilderTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME terrain_model_builder COMMAND KestrelTerrainModelBuilderTest)

    add_executable(KestrelTiffImageMetadataTest
        tests/TiffImageMetadataTest.cpp
        src/photogrammetry/io/TiffImageMetadata.cpp
        src/photogrammetry/io/TiffImageMetadata.h
        src/photogrammetry/geo/GeoCoordinates.cpp
        src/photogrammetry/geo/GeoCoordinates.h)
    target_include_directories(KestrelTiffImageMetadataTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS}
        ${KESTREL_LIBTIFF_INCLUDE_DIRS})
    target_link_libraries(KestrelTiffImageMetadataTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_imgproc
        opencv_imgcodecs ${KESTREL_LIBTIFF_TARGET})
    if (KESTREL_LIBTIFF_TARGET)
        target_compile_definitions(KestrelTiffImageMetadataTest PRIVATE
            KESTREL_HAS_LIBTIFF_GPS)
    endif()
    set_target_properties(KestrelTiffImageMetadataTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME tiff_image_metadata COMMAND KestrelTiffImageMetadataTest)

    add_executable(KestrelMicaSenseRadiometricCalibratorTest
        tests/MicaSenseRadiometricCalibratorTest.cpp
        src/photogrammetry/radiometry/MicaSenseRadiometricCalibrator.cpp
        src/photogrammetry/radiometry/MicaSenseRadiometricCalibrator.h
        src/photogrammetry/io/TiffImageMetadata.cpp
        src/photogrammetry/io/TiffImageMetadata.h
        src/photogrammetry/geo/GeoCoordinates.cpp
        src/photogrammetry/geo/GeoCoordinates.h)
    target_include_directories(KestrelMicaSenseRadiometricCalibratorTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelMicaSenseRadiometricCalibratorTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core)
    set_target_properties(KestrelMicaSenseRadiometricCalibratorTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME micasense_radiometric_calibration
             COMMAND KestrelMicaSenseRadiometricCalibratorTest)

    add_executable(KestrelReflectanceCalibratorTest
        tests/ReflectanceCalibratorTest.cpp
        src/photogrammetry/radiometry/ReflectanceCalibrator.cpp
        src/photogrammetry/radiometry/ReflectanceCalibrator.h)
    target_include_directories(KestrelReflectanceCalibratorTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelReflectanceCalibratorTest PRIVATE
        opencv_core)
    set_target_properties(KestrelReflectanceCalibratorTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME reflectance_calibration
             COMMAND KestrelReflectanceCalibratorTest)

    add_executable(KestrelCameraCalibrationTest
        tests/CameraCalibrationTest.cpp
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h)
    target_include_directories(KestrelCameraCalibrationTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelCameraCalibrationTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core)
    set_target_properties(KestrelCameraCalibrationTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME camera_calibration COMMAND KestrelCameraCalibrationTest)

    add_executable(KestrelRigCameraModelTest
        tests/RigCameraModelTest.cpp
        src/photogrammetry/camera/RigCameraModel.cpp
        src/photogrammetry/camera/RigCameraModel.h
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h
        src/photogrammetry/terrain/TerrainOrthorectifier.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.h)
    target_include_directories(KestrelRigCameraModelTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelRigCameraModelTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_imgproc)
    set_target_properties(KestrelRigCameraModelTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME rig_camera_model COMMAND KestrelRigCameraModelTest)

    add_executable(KestrelCaptureGroupingTest
        tests/CaptureGroupingTest.cpp
        src/photogrammetry/io/CaptureGrouping.cpp
        src/photogrammetry/io/CaptureGrouping.h
        src/photogrammetry/io/TiffImageMetadata.cpp
        src/photogrammetry/io/TiffImageMetadata.h
        src/photogrammetry/geo/GeoCoordinates.cpp
        src/photogrammetry/geo/GeoCoordinates.h)
    target_include_directories(KestrelCaptureGroupingTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS}
        ${KESTREL_LIBTIFF_INCLUDE_DIRS})
    target_link_libraries(KestrelCaptureGroupingTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_imgproc
        opencv_imgcodecs ${KESTREL_LIBTIFF_TARGET})
    if (KESTREL_LIBTIFF_TARGET)
        target_compile_definitions(KestrelCaptureGroupingTest PRIVATE
            KESTREL_HAS_LIBTIFF_GPS)
    endif()
    set_target_properties(KestrelCaptureGroupingTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME capture_grouping COMMAND KestrelCaptureGroupingTest)

    add_executable(KestrelFeatureTracksTest
        tests/FeatureTracksTest.cpp
        src/photogrammetry/sfm/FeatureTracks.cpp
        src/photogrammetry/sfm/FeatureTracks.h)
    target_include_directories(KestrelFeatureTracksTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelFeatureTracksTest PRIVATE opencv_core)
    set_target_properties(KestrelFeatureTracksTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME feature_tracks COMMAND KestrelFeatureTracksTest)

    add_executable(KestrelSparseReconstructionTest
        tests/SparseReconstructionTest.cpp
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h
        src/photogrammetry/sfm/SparseReconstruction.cpp
        src/photogrammetry/sfm/SparseReconstruction.h)
    target_include_directories(KestrelSparseReconstructionTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelSparseReconstructionTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_calib3d)
    set_target_properties(KestrelSparseReconstructionTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME sparse_reconstruction COMMAND KestrelSparseReconstructionTest)

    add_executable(KestrelReconstructionAlignmentTest
        tests/ReconstructionAlignmentTest.cpp
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h
        src/photogrammetry/sfm/ReconstructionAlignment.cpp
        src/photogrammetry/sfm/ReconstructionAlignment.h)
    target_include_directories(KestrelReconstructionAlignmentTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelReconstructionAlignmentTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core)
    set_target_properties(KestrelReconstructionAlignmentTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME reconstruction_alignment
             COMMAND KestrelReconstructionAlignmentTest)

    add_executable(KestrelBundleAdjustmentTest
        tests/BundleAdjustmentTest.cpp
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h
        src/photogrammetry/sfm/BundleAdjustment.cpp
        src/photogrammetry/sfm/BundleAdjustment.h)
    target_include_directories(KestrelBundleAdjustmentTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelBundleAdjustmentTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_calib3d)
    set_target_properties(KestrelBundleAdjustmentTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME bundle_adjustment COMMAND KestrelBundleAdjustmentTest)

    add_executable(KestrelDenseReconstructionTest
        tests/DenseReconstructionTest.cpp
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h
        src/photogrammetry/mvs/DenseReconstruction.cpp
        src/photogrammetry/mvs/DenseReconstruction.h
        src/photogrammetry/tiling/TileLayout.cpp
        src/photogrammetry/tiling/TileLayout.h)
    target_include_directories(KestrelDenseReconstructionTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelDenseReconstructionTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_imgproc opencv_calib3d)
    set_target_properties(KestrelDenseReconstructionTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME dense_reconstruction COMMAND KestrelDenseReconstructionTest)

    add_executable(KestrelDepthMapFusionTest
        tests/DepthMapFusionTest.cpp
        src/photogrammetry/mvs/DepthMapFusion.cpp
        src/photogrammetry/mvs/DepthMapFusion.h)
    target_include_directories(KestrelDepthMapFusionTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelDepthMapFusionTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core)
    set_target_properties(KestrelDepthMapFusionTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME depth_map_fusion COMMAND KestrelDepthMapFusionTest)

    add_executable(KestrelDensePointCloudFilterTest
        tests/DensePointCloudFilterTest.cpp
        src/photogrammetry/mvs/DensePointCloudFilter.cpp
        src/photogrammetry/mvs/DensePointCloudFilter.h)
    target_include_directories(KestrelDensePointCloudFilterTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS})
    target_link_libraries(KestrelDensePointCloudFilterTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core)
    set_target_properties(KestrelDensePointCloudFilterTest PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME dense_point_cloud_filter
             COMMAND KestrelDensePointCloudFilterTest)

    # Manual integration diagnostic. It is intentionally not registered with
    # CTest because it requires a user-supplied project containing real TIFFs
    # and a saved transforms.yml sparse reconstruction.
    add_executable(KestrelDenseReconstructionRealSmoke
        tests/DenseReconstructionRealSmoke.cpp
        src/photogrammetry/camera/CameraCalibration.cpp
        src/photogrammetry/camera/CameraCalibration.h
        src/photogrammetry/camera/RigCameraModel.cpp
        src/photogrammetry/camera/RigCameraModel.h
        src/photogrammetry/mvs/DenseReconstruction.cpp
        src/photogrammetry/mvs/DenseReconstruction.h
        src/photogrammetry/mvs/DepthMapFusion.cpp
        src/photogrammetry/mvs/DepthMapFusion.h
        src/photogrammetry/mvs/DensePointCloudFilter.cpp
        src/photogrammetry/mvs/DensePointCloudFilter.h
        src/photogrammetry/terrain/TerrainModelBuilder.cpp
        src/photogrammetry/terrain/TerrainModelBuilder.h
        src/photogrammetry/terrain/TerrainOrthorectifier.cpp
        src/photogrammetry/terrain/TerrainOrthorectifier.h
        src/photogrammetry/terrain/MultiCameraOrthorectifier.cpp
        src/photogrammetry/terrain/MultiCameraOrthorectifier.h
        src/photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.cpp
        src/photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h
        src/photogrammetry/terrain/SharedGeometryRgbOrthorectifier.cpp
        src/photogrammetry/terrain/SharedGeometryRgbOrthorectifier.h
        src/photogrammetry/terrain/OrthophotoSeamBlender.cpp
        src/photogrammetry/terrain/OrthophotoSeamBlender.h
        src/photogrammetry/geo/GeoCoordinates.cpp
        src/photogrammetry/geo/GeoCoordinates.h
        src/photogrammetry/geo/ProjectedCoordinates.cpp
        src/photogrammetry/geo/ProjectedCoordinates.h
        src/photogrammetry/io/GeoTiffWriter.cpp
        src/photogrammetry/io/GeoTiffWriter.h
        src/photogrammetry/io/TiffImageMetadata.cpp
        src/photogrammetry/io/TiffImageMetadata.h
        src/photogrammetry/radiometry/MicaSenseRadiometricCalibrator.cpp
        src/photogrammetry/radiometry/MicaSenseRadiometricCalibrator.h
        src/photogrammetry/radiometry/ReflectanceCalibrator.cpp
        src/photogrammetry/radiometry/ReflectanceCalibrator.h
        src/photogrammetry/tiling/TileLayout.cpp
        src/photogrammetry/tiling/TileLayout.h)
    target_include_directories(KestrelDenseReconstructionRealSmoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${KESTREL_OPENCV_INCLUDE_DIRS}
        ${KESTREL_LIBTIFF_INCLUDE_DIRS})
    target_link_libraries(KestrelDenseReconstructionRealSmoke PRIVATE
        Qt${QT_VERSION_MAJOR}::Core opencv_core opencv_imgproc
        opencv_imgcodecs opencv_calib3d ${KESTREL_LIBTIFF_TARGET})
    if (KESTREL_LIBTIFF_TARGET)
        target_compile_definitions(KestrelDenseReconstructionRealSmoke PRIVATE
            KESTREL_HAS_LIBTIFF_GEOTIFF KESTREL_HAS_LIBTIFF_GPS)
    endif()
    set_target_properties(KestrelDenseReconstructionRealSmoke PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()

if (KESTREL_BUNDLED_OPENCV_SOURCE_DIR)
    set(_kestrel_license_dir "${CMAKE_INSTALL_DATADIR}/licenses/Kestrel")
    install(FILES "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/LICENSE"
        DESTINATION "${_kestrel_license_dir}" RENAME OpenCV-LICENSE.txt
        COMPONENT KestrelRuntime)
    install(FILES "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/3rdparty/libjpeg-turbo/LICENSE.md"
        DESTINATION "${_kestrel_license_dir}" RENAME libjpeg-turbo-LICENSE.md
        COMPONENT KestrelRuntime)
    install(FILES "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/3rdparty/libjpeg-turbo/README.ijg"
        DESTINATION "${_kestrel_license_dir}" RENAME libjpeg-turbo-README.ijg
        COMPONENT KestrelRuntime)
    install(FILES "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/3rdparty/libpng/LICENSE"
        DESTINATION "${_kestrel_license_dir}" RENAME libpng-LICENSE.txt
        COMPONENT KestrelRuntime)
    install(FILES "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/3rdparty/libtiff/COPYRIGHT"
        DESTINATION "${_kestrel_license_dir}" RENAME libtiff-COPYRIGHT.txt
        COMPONENT KestrelRuntime)
    install(FILES "${KESTREL_BUNDLED_OPENCV_SOURCE_DIR}/3rdparty/zlib/README"
        DESTINATION "${_kestrel_license_dir}" RENAME zlib-README.txt
        COMPONENT KestrelRuntime)
endif()

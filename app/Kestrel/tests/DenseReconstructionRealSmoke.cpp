#include "photogrammetry/mvs/DenseReconstruction.h"
#include "photogrammetry/mvs/DensePointCloudFilter.h"
#include "photogrammetry/mvs/DepthMapFusion.h"
#include "photogrammetry/camera/RigCameraModel.h"
#include "photogrammetry/geo/GeoCoordinates.h"
#include "photogrammetry/geo/ProjectedCoordinates.h"
#include "photogrammetry/io/GeoTiffWriter.h"
#include "photogrammetry/io/TiffImageMetadata.h"
#include "photogrammetry/radiometry/MicaSenseRadiometricCalibrator.h"
#include "photogrammetry/terrain/MultiCameraOrthorectifier.h"
#include "photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h"
#include "photogrammetry/terrain/TerrainModelBuilder.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

cv::Matx33d matrix33(const cv::FileNode &node)
{
    cv::Mat matrix;
    node >> matrix;
    cv::Mat converted;
    matrix.convertTo(converted, CV_64F);
    cv::Matx33d result = cv::Matx33d::eye();
    if (converted.rows == 3 && converted.cols == 3) {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                result(row, column) = converted.at<double>(row, column);
            }
        }
    }
    return result;
}

template <int Count>
cv::Vec<double, Count> vector(const cv::FileNode &node)
{
    cv::Vec<double, Count> result = cv::Vec<double, Count>::all(0.0);
    int index = 0;
    for (auto value = node.begin(); value != node.end() && index < Count;
         ++value, ++index) {
        result[index] = static_cast<double>(*value);
    }
    return result;
}

double median(std::vector<float> values)
{
    if (values.empty()) {
        return 0.0;
    }
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return values[middle];
}

bool loadSavedReconstruction(const fs::path &projectRoot,
                             int firstCapture, int captureCount,
                             kestrel::DenseReconstructionInput *input,
                             std::vector<kestrel::MultispectralOrthophotoSource> *multispectralSources,
                             std::map<int, std::string> *captureNames,
                             kestrel::GeoCoordinate *frameOrigin,
                             std::string *error)
{
    const fs::path transformsPath = projectRoot / "processed_images"
        / "kestrel_mosaic" / "transforms.yml";
    cv::FileStorage storage(transformsPath.string(), cv::FileStorage::READ);
    if (!storage.isOpened()) {
        *error = "Could not open " + transformsPath.string();
        return false;
    }

    const cv::FileNode coordinateFrame = storage["coordinate_frame"];
    if (!frameOrigin || coordinateFrame.empty()
        || static_cast<int>(coordinateFrame["has_origin"]) == 0) {
        *error = "Saved reconstruction has no WGS-84 ENU origin.";
        return false;
    }
    *frameOrigin = {
        static_cast<double>(coordinateFrame["latitude_degrees"]),
        static_cast<double>(coordinateFrame["longitude_degrees"]),
        static_cast<double>(coordinateFrame["altitude_metres"])};
    if (!frameOrigin->isValid()) {
        *error = "Saved reconstruction has an invalid WGS-84 ENU origin.";
        return false;
    }

    std::map<int, std::map<int, kestrel::CameraCalibration>> bandCalibrations;
    std::map<int, std::map<std::string, int>> semanticBandIndices;
    std::map<int, std::map<int, double>> bandWavelengths;
    int captureIndex = 0;
    for (const cv::FileNode &capture : storage["captures"]) {
        const std::string name = static_cast<std::string>(capture["name"]);
        (*captureNames)[captureIndex] = name;
        for (const cv::FileNode &band : capture["bands"]) {
            const int bandIndex = static_cast<int>(band["band_index"]);
            kestrel::CameraCalibration calibration;
            calibration.imageSize = {
                static_cast<int>(band["image_width"]),
                static_cast<int>(band["image_height"])};
            calibration.intrinsic = matrix33(band["intrinsic_matrix"]);
            calibration.distortion = vector<5>(band["distortion_opencv"]);
            calibration.quality = kestrel::CalibrationQuality::MetadataCalibrated;
            calibration.source = "saved spectral-band metadata";
            calibration.rigPose.hasRotation =
                static_cast<int>(band["rig_has_rotation"]) != 0;
            calibration.rigPose.hasTranslation =
                static_cast<int>(band["rig_has_translation"]) != 0;
            if (calibration.rigPose.hasRotation) {
                calibration.rigPose.rotationToReference = matrix33(
                    band["rig_rotation_to_reference"]);
            }
            const cv::FileNode rigTranslation =
                band["rig_translation_to_reference"];
            if (calibration.rigPose.hasTranslation
                && !rigTranslation.empty()) {
                calibration.rigPose.translationToReference =
                    vector<3>(rigTranslation);
            } else {
                calibration.rigPose.hasTranslation = false;
            }
            bandCalibrations[captureIndex][bandIndex] = calibration;
            std::string bandName = static_cast<std::string>(band["band_name"]);
            bandWavelengths[captureIndex][bandIndex] =
                static_cast<double>(band["central_wavelength_nm"]);
            std::transform(bandName.begin(), bandName.end(), bandName.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
            if (bandName == "blue" || bandName == "green"
                || bandName == "red" || bandName == "nir"
                || bandName == "red edge" || bandName == "rededge") {
                if (bandName == "rededge") bandName = "red edge";
                semanticBandIndices[captureIndex][bandName] = bandIndex;
            }
        }
        // MicaSense's stable legacy suffix remains a compatibility fallback
        // for old transforms files that did not persist semantic band names.
        semanticBandIndices[captureIndex].try_emplace("blue", 1);
        semanticBandIndices[captureIndex].try_emplace("green", 2);
        semanticBandIndices[captureIndex].try_emplace("red", 3);
        semanticBandIndices[captureIndex].try_emplace("nir", 4);
        semanticBandIndices[captureIndex].try_emplace("red edge", 5);
        ++captureIndex;
    }

    const cv::FileNode sparse = storage["sparse_reconstruction"];
    const cv::FileNode adjustment = sparse["sparse_bundle_adjustment"];
    for (const cv::FileNode &refined : adjustment["refined_calibrations"]) {
        const cv::Matx33d intrinsic = matrix33(
            refined["final_intrinsic_matrix"]);
        const cv::Vec<double, 5> distortion = vector<5>(
            refined["final_distortion_opencv"]);
        for (const cv::FileNode &indexNode : refined["capture_indices"]) {
            const int index = static_cast<int>(indexNode);
            auto captureCalibrations = bandCalibrations.find(index);
            const int greenIndex = semanticBandIndices[index]["green"];
            if (captureCalibrations != bandCalibrations.end()
                && captureCalibrations->second.count(greenIndex)) {
                kestrel::CameraCalibration &calibration =
                    captureCalibrations->second[greenIndex];
                calibration.intrinsic = intrinsic;
                calibration.distortion = distortion;
                calibration.source = "saved bundle-adjusted reference calibration";
            }
        }
    }

    std::map<int, kestrel::SparseCameraPose> cameras;
    for (const cv::FileNode &camera : sparse["cameras"]) {
        kestrel::SparseCameraPose pose;
        pose.imageIndex = static_cast<int>(camera["capture_index"]);
        pose.componentId = static_cast<int>(camera["component_id"]);
        pose.worldToCameraRotation = matrix33(
            camera["world_to_camera_rotation"]);
        pose.worldToCameraTranslation = vector<3>(
            camera["world_to_camera_translation"]);
        pose.pnpInliers = static_cast<int>(camera["pnp_inliers"]);
        pose.pnpReprojectionRmsPixels = static_cast<double>(
            camera["pnp_reprojection_rms_pixels"]);
        cameras[pose.imageIndex] = pose;
    }

    for (const cv::FileNode &point : sparse["points"]) {
        kestrel::SparsePoint value;
        value.trackId = static_cast<int>(point["track_id"]);
        value.componentId = static_cast<int>(point["component_id"]);
        value.position = {
            static_cast<double>(point["x"]),
            static_cast<double>(point["y"]),
            static_cast<double>(point["z"])};
        value.reprojectionRmsPixels = static_cast<double>(
            point["reprojection_rms_pixels"]);
        value.triangulationAngleDegrees = static_cast<double>(
            point["triangulation_angle_degrees"]);
        input->sparsePoints.push_back(value);
    }

    for (const cv::FileNode &trackNode : storage["feature_tracks"]) {
        kestrel::FeatureTrack track;
        track.id = static_cast<int>(trackNode["id"]);
        for (const cv::FileNode &observation : trackNode["observations"]) {
            track.observations.push_back({
                static_cast<int>(observation["capture_index"]),
                static_cast<int>(observation["feature_index"]),
                {static_cast<double>(observation["x"]),
                 static_cast<double>(observation["y"])}});
        }
        input->tracks.push_back(std::move(track));
    }

    const fs::path rawImages = projectRoot / "raw_images";
    for (int index = firstCapture;
         index < firstCapture + captureCount; ++index) {
        const auto camera = cameras.find(index);
        const auto captureCalibrations = bandCalibrations.find(index);
        const auto name = captureNames->find(index);
        if (camera == cameras.end()
            || captureCalibrations == bandCalibrations.end()
            || name == captureNames->end()) {
            continue;
        }
        const int greenBandIndex = semanticBandIndices[index]["green"];
        const auto greenCalibrationIt =
            captureCalibrations->second.find(greenBandIndex);
        if (greenCalibrationIt == captureCalibrations->second.end()) {
            continue;
        }
        const fs::path imagePath = rawImages /
            (name->second + "_" + std::to_string(greenBandIndex) + ".tif");
        cv::Mat image = cv::imread(imagePath.string(), cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            *error = "Could not decode " + imagePath.string();
            return false;
        }

        // Retain the central 75% of the sensor. This reduces work while still
        // exercising calibrated reprojection on real overlapping content.
        const int width = std::max(32, image.cols * 3 / 4);
        const int height = std::max(32, image.rows * 3 / 4);
        const cv::Rect roi((image.cols - width) / 2,
                           (image.rows - height) / 2, width, height);
        kestrel::CameraCalibration croppedCalibration =
            greenCalibrationIt->second;
        croppedCalibration.imageSize = roi.size();
        croppedCalibration.intrinsic(0, 2) -= roi.x;
        croppedCalibration.intrinsic(1, 2) -= roi.y;
        input->views.push_back({index, image(roi).clone(),
                                croppedCalibration, camera->second});

        if (!multispectralSources) {
            continue;
        }
        kestrel::PinholeCamera referenceCamera;
        referenceCamera.intrinsic = croppedCalibration.intrinsic;
        referenceCamera.distortion = croppedCalibration.distortion;
        referenceCamera.worldToCameraRotation =
            camera->second.worldToCameraRotation;
        referenceCamera.worldToCameraTranslation =
            camera->second.worldToCameraTranslation;
        const auto loadBand = [&](const char *role, const char *outputName,
                                  double defaultWavelength,
                                  kestrel::SpectralOrthophotoBand *output)
            -> bool {
            const int bandIndex = semanticBandIndices[index][role];
            const auto savedCalibration =
                captureCalibrations->second.find(bandIndex);
            if (savedCalibration == captureCalibrations->second.end()) {
                return false;
            }
            const fs::path bandPath = rawImages /
                (name->second + "_" + std::to_string(bandIndex) + ".tif");
            cv::Mat decoded = cv::imread(
                bandPath.string(), cv::IMREAD_UNCHANGED);
            if (decoded.empty() || decoded.channels() != 1) {
                return false;
            }
            const int bandWidth = std::max(32, decoded.cols * 3 / 4);
            const int bandHeight = std::max(32, decoded.rows * 3 / 4);
            const cv::Rect bandRoi((decoded.cols - bandWidth) / 2,
                                   (decoded.rows - bandHeight) / 2,
                                   bandWidth, bandHeight);
            kestrel::TiffImageMetadata radiometricMetadata;
            QString metadataError;
            if (!kestrel::TiffMetadataReader::read(
                    QString::fromStdString(bandPath.string()),
                    &radiometricMetadata, &metadataError)) {
                std::cerr << "Could not read radiometric metadata from "
                          << bandPath.filename().string() << ": "
                          << metadataError.toStdString() << '\n';
                return false;
            }
            const kestrel::RadiometricCalibrationResult radiometric =
                kestrel::MicaSenseRadiometricCalibrator::calibrateToRadiance(
                    decoded(bandRoi), radiometricMetadata, bandRoi.tl());
            if (!radiometric.success) {
                std::cerr << "Could not calibrate "
                          << bandPath.filename().string() << ": "
                          << radiometric.message
                          << " [bits=" << radiometricMetadata.bitsPerSample
                          << ", exposure="
                          << radiometricMetadata.exposureTimeSeconds
                          << ", ISO=" << radiometricMetadata.isoSpeed
                          << ", black="
                          << radiometricMetadata.blackLevels.size()
                          << ", vignetteCenter="
                          << radiometricMetadata.vignettingCenter.size()
                          << ", vignettePolynomial="
                          << radiometricMetadata.vignettingPolynomial.size()
                          << ", radiometric="
                          << radiometricMetadata.radiometricCalibration.size()
                          << "]\n";
                return false;
            }
            kestrel::CameraCalibration bandCalibration =
                savedCalibration->second;
            bandCalibration.imageSize = bandRoi.size();
            bandCalibration.intrinsic(0, 2) -= bandRoi.x;
            bandCalibration.intrinsic(1, 2) -= bandRoi.y;
            kestrel::RigCameraDerivation derived;
            std::string rigError;
            if (!kestrel::RigCameraModel::deriveFromReference(
                    referenceCamera, croppedCalibration, bandCalibration,
                    &derived, &rigError)) {
                std::cerr << "Could not derive " << outputName
                          << " rig camera for " << name->second << ": "
                          << rigError << '\n';
                return false;
            }
            const double savedWavelength = bandWavelengths[index][bandIndex];
            output->name = outputName;
            output->centreWavelengthNanometres =
                std::isfinite(savedWavelength) && savedWavelength > 0.0
                    ? savedWavelength : defaultWavelength;
            output->image = radiometric.radiance;
            output->camera = derived.camera;
            output->validityMask = radiometric.validityMask;
            return true;
        };
        struct RequestedBand {
            const char *role;
            const char *name;
            double wavelength;
        };
        const RequestedBand requested[] = {
            {"blue", "Blue", 475.0}, {"green", "Green", 560.0},
            {"red", "Red", 668.0}, {"nir", "NIR", 840.0},
            {"red edge", "Red edge", 717.0}};
        kestrel::MultispectralOrthophotoSource source;
        source.imageIndex = index;
        bool complete = true;
        for (const RequestedBand &band : requested) {
            kestrel::SpectralOrthophotoBand loaded;
            if (!loadBand(band.role, band.name, band.wavelength, &loaded)) {
                complete = false;
                break;
            }
            source.bands.push_back(std::move(loaded));
        }
        if (!complete) {
            std::cerr << "Skipping incomplete five-band capture "
                      << name->second << ".\n";
            continue;
        }
        multispectralSources->push_back(std::move(source));
    }

    if (input->views.size() < 3) {
        *error = "The selected capture range has fewer than three saved cameras.";
        return false;
    }
    return true;
}

void writePointCloud(const fs::path &path,
                     const std::vector<kestrel::FusedDensePoint> &points,
                     const char *comment)
{
    std::ofstream output(path);
    output << "ply\nformat ascii 1.0\n"
           << "comment " << comment << '\n'
           << "element vertex " << points.size() << '\n'
           << "property double x\nproperty double y\nproperty double z\n"
           << "property float confidence\n"
           << "property int observation_count\n"
           << "property int sample_count\nend_header\n";
    output << std::setprecision(10);
    for (const kestrel::FusedDensePoint &point : points) {
        output << point.position.x << ' ' << point.position.y << ' '
               << point.position.z << ' ' << point.confidence << ' '
               << point.observationCount << ' ' << point.sampleCount << '\n';
    }
}

struct DsmDiagnostic
{
    bool success = false;
    std::string message;
    kestrel::TerrainGrid terrain;
    cv::Mat sampleCounts;
};

DsmDiagnostic buildDiagnosticDsm(
    const std::vector<kestrel::FusedDensePoint> &points,
    const fs::path &outputDirectory)
{
    DsmDiagnostic result;
    if (points.empty()) {
        result.message = "Filtered cloud is empty.";
        return result;
    }
    double minimumX = points.front().position.x;
    double maximumX = minimumX;
    double minimumY = points.front().position.y;
    double maximumY = minimumY;
    std::vector<kestrel::TerrainPoint> terrainPoints;
    terrainPoints.reserve(points.size());
    for (const kestrel::FusedDensePoint &point : points) {
        minimumX = std::min(minimumX, point.position.x);
        maximumX = std::max(maximumX, point.position.x);
        minimumY = std::min(minimumY, point.position.y);
        maximumY = std::max(maximumY, point.position.y);
        terrainPoints.push_back({point.position});
    }

    constexpr double spacing = 0.10;
    kestrel::DsmGridDefinition definition;
    definition.origin = {minimumX, maximumY};
    definition.spacing = {spacing, -spacing};
    definition.size = {
        std::max(1, static_cast<int>(std::ceil(
            (maximumX - minimumX) / spacing)) + 1),
        std::max(1, static_cast<int>(std::ceil(
            (maximumY - minimumY) / spacing)) + 1)};
    definition.minimumSamplesPerCell = 1;
    if (definition.size.area() > 10000000) {
        result.message = "Diagnostic DSM would exceed ten million cells.";
        return result;
    }
    result.success = kestrel::TerrainModelBuilder::rasterizeDsm(
        terrainPoints, definition, &result.terrain, &result.sampleCounts,
        &result.message);
    if (!result.success) {
        return result;
    }

    cv::FileStorage storage(
        (outputDirectory / "filtered_dsm.yml.gz").string(),
        cv::FileStorage::WRITE);
    storage << "origin_x" << result.terrain.origin.x
            << "origin_y" << result.terrain.origin.y
            << "spacing_x" << result.terrain.spacing[0]
            << "spacing_y" << result.terrain.spacing[1]
            << "elevation" << result.terrain.elevation
            << "validity_mask" << result.terrain.validityMask
            << "sample_counts" << result.sampleCounts;

    double minimumHeight = 0.0;
    double maximumHeight = 0.0;
    cv::minMaxLoc(result.terrain.elevation, &minimumHeight, &maximumHeight,
                  nullptr, nullptr, result.terrain.validityMask);
    cv::Mat normalized = cv::Mat::zeros(
        result.terrain.elevation.size(), CV_8U);
    if (maximumHeight > minimumHeight) {
        result.terrain.elevation.convertTo(
            normalized, CV_8U, 255.0 / (maximumHeight - minimumHeight),
            -255.0 * minimumHeight / (maximumHeight - minimumHeight));
    }
    cv::Mat colour;
    cv::applyColorMap(normalized, colour, cv::COLORMAP_TURBO);
    colour.setTo(cv::Scalar::all(0), result.terrain.validityMask == 0);
    cv::imwrite((outputDirectory / "filtered_dsm_height.png").string(),
                colour);
    result.message = "Filtered dense cloud rasterized into a diagnostic DSM.";
    return result;
}

kestrel::MultiCameraOrthophotoResult buildDiagnosticOrthophoto(
    const std::vector<kestrel::DenseMvsView> &views,
    const DsmDiagnostic &dsm,
    const fs::path &outputDirectory,
    double groundSampleDistanceMetres,
    const kestrel::GeoCoordinate &frameOrigin,
    kestrel::ProjectedRasterTransform *projectedReference,
    bool *geoTiffWritten,
    std::string *geoTiffMessage)
{
    if (geoTiffWritten) *geoTiffWritten = false;
    kestrel::MultiCameraOrthophotoResult empty;
    if (!dsm.success || views.empty()) {
        empty.message = "DSM or source views are unavailable.";
        return empty;
    }
    std::vector<kestrel::OrthophotoSource> sources;
    sources.reserve(views.size());
    for (const kestrel::DenseMvsView &view : views) {
        kestrel::PinholeCamera camera;
        camera.intrinsic = view.calibration.intrinsic;
        camera.distortion = view.calibration.distortion;
        camera.worldToCameraRotation = view.pose.worldToCameraRotation;
        camera.worldToCameraTranslation = view.pose.worldToCameraTranslation;
        sources.push_back({view.imageIndex, view.image, camera, {}, 1.0});
    }
    kestrel::OrthophotoGrid grid;
    std::string gridError;
    if (!kestrel::TerrainOrthorectifier::createOutputGrid(
            dsm.terrain, groundSampleDistanceMetres, 0.0, &grid,
            &gridError)) {
        empty.message = "Could not plan the orthophoto output grid: "
            + gridError;
        return empty;
    }
    kestrel::MultiCameraOrthophotoOptions options;
    options.minimumViewCosine = 0.10;
    options.borderMarginPixels = 48.0;
    options.occlusionClearanceMetres = 0.25;
    options.occlusionSampleSpacingMetres = 0.10;
    // The smoke DSM intentionally contains large unmeasured gaps between
    // overlap strips. Known terrain can still prove an occlusion, while an
    // unknown cell must not erase every oblique source in this diagnostic.
    options.rejectUnknownTerrainAlongRay = false;
    options.maximumSourcesPerPixel = 3;
    options.interpolation = cv::INTER_LANCZOS4;
    options.useSeamBlending = true;
    options.seamBlendOptions.maximumIterations = 6;
    options.seamBlendOptions.smoothnessCost = 0.35;
    options.seamBlendOptions.photometricSeamCostWeight = 0.75;
    options.seamBlendOptions.multibandLevels = 4;
    options.tileSize = {48, 48};
    kestrel::MultiCameraOrthophotoResult result =
        kestrel::MultiCameraOrthorectifier::orthorectify(
            sources, dsm.terrain, grid, options);
    if (!result.success) {
        return result;
    }
    kestrel::ProjectedRasterTransform reference;
    std::string projectionError;
    if (!kestrel::createProjectedRasterTransform(
            kestrel::LocalTangentPlane(frameOrigin),
            result.outputGrid.origin, result.outputGrid.pixelSize,
            result.outputGrid.size, &reference, &projectionError)) {
        if (geoTiffMessage) *geoTiffMessage = projectionError;
        return result;
    }
    std::string writerError;
    const bool wroteGeoTiff = kestrel::GeoTiffWriter::write(
        outputDirectory / "multi_camera_orthophoto.tif",
        result.orthophoto, result.validityMask, reference, {},
        &writerError);
    if (projectedReference) *projectedReference = reference;
    if (geoTiffWritten) *geoTiffWritten = wroteGeoTiff;
    if (geoTiffMessage) {
        *geoTiffMessage = wroteGeoTiff
            ? "Projected GeoTIFF written in " + reference.crsName + "."
            : writerError;
    }
    cv::imwrite((outputDirectory / "multi_camera_validity.png").string(),
                result.validityMask);
    cv::imwrite((outputDirectory / "multi_camera_seams.png").string(),
                result.seamMask);
    cv::Mat sourceCountPreview;
    result.sourceCount.convertTo(sourceCountPreview, CV_8U, 85.0);
    cv::imwrite((outputDirectory / "multi_camera_source_count.png").string(),
                sourceCountPreview);
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(result.orthophoto, &minimum, &maximum, nullptr, nullptr,
                  result.validityMask);
    cv::Mat preview = cv::Mat::zeros(result.orthophoto.size(), CV_8U);
    if (maximum > minimum) {
        result.orthophoto.convertTo(
            preview, CV_8U, 255.0 / (maximum - minimum),
            -255.0 * minimum / (maximum - minimum));
        preview.setTo(0, result.validityMask == 0);
    }
    cv::imwrite((outputDirectory / "multi_camera_orthophoto.png").string(),
                preview);
    return result;
}

kestrel::SharedGeometryMultispectralResult buildDiagnosticMultispectralOrthophoto(
    const std::vector<kestrel::MultispectralOrthophotoSource> &sources,
    const DsmDiagnostic &dsm,
    const kestrel::OrthophotoGrid &grid,
    const fs::path &outputDirectory,
    const kestrel::ProjectedRasterTransform &projectedReference,
    bool *geoTiffWritten,
    std::string *geoTiffMessage)
{
    if (geoTiffWritten) *geoTiffWritten = false;
    kestrel::SharedGeometryMultispectralResult empty;
    if (!dsm.success || sources.empty() || grid.size.area() <= 0) {
        empty.message = "DSM, five-band captures, or output grid are unavailable.";
        return empty;
    }
    kestrel::SharedGeometryMultispectralOptions options;
    options.referenceBandIndex = 1; // Green reference geometry.
    options.geometryOptions.minimumViewCosine = 0.10;
    options.geometryOptions.borderMarginPixels = 48.0;
    options.geometryOptions.occlusionClearanceMetres = 0.25;
    options.geometryOptions.occlusionSampleSpacingMetres = 0.10;
    options.geometryOptions.rejectUnknownTerrainAlongRay = false;
    options.geometryOptions.maximumSourcesPerPixel = 3;
    options.geometryOptions.interpolation = cv::INTER_LANCZOS4;
    options.geometryOptions.useSeamBlending = true;
    options.geometryOptions.seamBlendOptions.maximumIterations = 6;
    options.geometryOptions.seamBlendOptions.smoothnessCost = 0.35;
    options.geometryOptions.seamBlendOptions.photometricSeamCostWeight = 0.75;
    options.geometryOptions.seamBlendOptions.multibandLevels = 4;
    options.geometryOptions.tileSize = {48, 48};
    options.bandInterpolation = cv::INTER_LANCZOS4;
    options.multibandLevels = 4;
    kestrel::SharedGeometryMultispectralResult result =
        kestrel::SharedGeometryMultispectralOrthorectifier::orthorectify(
            sources, dsm.terrain, grid, options);
    if (!result.success) {
        return result;
    }

    kestrel::GeoTiffWriteOptions writeOptions;
    writeOptions.threeChannelInputIsBgr = false;
    writeOptions.bandNames = result.bandNames;
    writeOptions.centreWavelengthsNanometres =
        result.centreWavelengthsNanometres;
    std::string writerError;
    const bool wroteGeoTiff = kestrel::GeoTiffWriter::write(
        outputDirectory / "multi_camera_multispectral_radiance.tif",
        result.orthophoto, result.validityMask, projectedReference,
        writeOptions,
        &writerError);
    if (geoTiffWritten) *geoTiffWritten = wroteGeoTiff;
    if (geoTiffMessage) {
        *geoTiffMessage = wroteGeoTiff
            ? "Projected five-band float radiance GeoTIFF written in "
                  + projectedReference.crsName + "."
            : writerError;
    }

    std::vector<cv::Mat> channels;
    cv::split(result.orthophoto, channels);
    // The ordered first three bands are Blue, Green, Red, which is directly
    // the BGR ordering expected by OpenCV's PNG writer. A shared scale keeps
    // their relative radiance intact instead of independently stretching
    // each sensor into a misleading colour balance.
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (size_t index = 0; index < std::min<size_t>(3, channels.size()); ++index) {
        double bandMinimum = 0.0;
        double bandMaximum = 0.0;
        cv::minMaxLoc(channels[index], &bandMinimum, &bandMaximum,
                      nullptr, nullptr, result.validityMask);
        minimum = std::min(minimum, bandMinimum);
        maximum = std::max(maximum, bandMaximum);
    }
    std::vector<cv::Mat> previewChannels;
    for (size_t index = 0; index < std::min<size_t>(3, channels.size()); ++index) {
        cv::Mat preview = cv::Mat::zeros(channels[index].size(), CV_8U);
        if (maximum > minimum) {
            channels[index].convertTo(
                preview, CV_8U, 255.0 / (maximum - minimum),
                -255.0 * minimum / (maximum - minimum));
            preview.setTo(0, result.validityMask == 0);
        }
        previewChannels.push_back(std::move(preview));
    }
    cv::Mat preview;
    cv::merge(previewChannels, preview);
    cv::imwrite(
        (outputDirectory / "multi_camera_multispectral_rgb_preview.png").string(),
        preview);
    cv::imwrite((outputDirectory / "multi_camera_multispectral_validity.png").string(),
                result.validityMask);
    return result;
}

void writeDiagnostics(const fs::path &outputDirectory,
                      const kestrel::DenseReconstructionResult &result,
                      const kestrel::DepthMapFusionResult &fusion,
                      const kestrel::DensePointCloudFilterResult &filtered,
                      const DsmDiagnostic &dsm,
                      const kestrel::ProjectedRasterTransform &dsmProjectedReference,
                      bool dsmGeoTiffWritten,
                      const std::string &dsmGeoTiffMessage,
                      const kestrel::MultiCameraOrthophotoResult &orthophoto,
                      const kestrel::ProjectedRasterTransform &projectedReference,
                      bool geoTiffWritten,
                      const std::string &geoTiffMessage,
                      const kestrel::SharedGeometryMultispectralResult &multispectralOrthophoto,
                      bool multispectralGeoTiffWritten,
                      const std::string &multispectralGeoTiffMessage,
                      const std::map<int, std::string> &captureNames,
                      const kestrel::DenseMvsOptions &options,
                      double denseReconstructionSeconds)
{
    fs::create_directories(outputDirectory);
    cv::FileStorage summary(
        (outputDirectory / "summary.yml").string(), cv::FileStorage::WRITE);
    summary << "success" << result.success
            << "message" << result.message
            << "maximum_image_dimension" << options.maximumImageDimension
            << "requested_tile_size_pixels" << options.tileSizePixels
            << "dense_reconstruction_seconds" << denseReconstructionSeconds
            << "raw_valid_pixels" << static_cast<double>(result.rawValidPixelCount)
            << "consistent_pixels" << static_cast<double>(result.consistentPixelCount)
            << "processed_mvs_tiles"
            << static_cast<double>(result.processedTileCount)
            << "resumed_mvs_tiles"
            << static_cast<double>(result.resumedTileCount)
            << "processed_consistency_tiles"
            << static_cast<double>(result.processedConsistencyTileCount)
            << "resumed_consistency_tiles"
            << static_cast<double>(result.resumedConsistencyTileCount)
            << "retained_fraction"
            << (result.rawValidPixelCount > 0
                    ? static_cast<double>(result.consistentPixelCount)
                          / result.rawValidPixelCount
                    : 0.0)
            << "fusion_success" << fusion.success
            << "fusion_message" << fusion.message
            << "fusion_valid_input_samples"
            << static_cast<double>(fusion.validInputSampleCount)
            << "fusion_candidate_voxels"
            << static_cast<double>(fusion.candidateVoxelCount)
            << "fusion_under_supported_voxels"
            << static_cast<double>(fusion.underSupportedVoxelCount)
            << "fused_points" << static_cast<double>(fusion.points.size())
            << "filter_success" << filtered.success
            << "filter_message" << filtered.message
            << "filter_low_confidence_points"
            << static_cast<double>(filtered.lowConfidencePointCount)
            << "filter_isolated_points"
            << static_cast<double>(filtered.isolatedPointCount)
            << "filter_statistical_outliers"
            << static_cast<double>(filtered.statisticalOutlierCount)
            << "filter_elevation_outliers"
            << static_cast<double>(filtered.elevationOutlierCount)
            << "filtered_points" << static_cast<double>(filtered.points.size())
            << "median_mean_neighbour_distance_metres"
            << filtered.medianMeanNeighbourDistanceMetres
            << "spatial_rejection_threshold_metres"
            << filtered.spatialRejectionThresholdMetres
            << "dsm_success" << dsm.success
            << "dsm_message" << dsm.message
            << "dsm_width" << dsm.terrain.elevation.cols
            << "dsm_height" << dsm.terrain.elevation.rows
            << "dsm_valid_cells"
            << (dsm.terrain.validityMask.empty()
                    ? 0 : cv::countNonZero(dsm.terrain.validityMask))
            << "dsm_geotiff_written" << dsmGeoTiffWritten
            << "dsm_geotiff_message" << dsmGeoTiffMessage
            << "dsm_geotiff_epsg" << dsmProjectedReference.epsgCode
            << "orthophoto_success" << orthophoto.success
            << "orthophoto_message" << orthophoto.message
            << "orthophoto_width" << orthophoto.outputGrid.size.width
            << "orthophoto_height" << orthophoto.outputGrid.size.height
            << "orthophoto_origin_x" << orthophoto.outputGrid.origin.x
            << "orthophoto_origin_y" << orthophoto.outputGrid.origin.y
            << "orthophoto_pixel_size_x"
            << orthophoto.outputGrid.pixelSize[0]
            << "orthophoto_pixel_size_y"
            << orthophoto.outputGrid.pixelSize[1]
            << "orthophoto_valid_pixels"
            << (orthophoto.validityMask.empty()
                    ? 0 : cv::countNonZero(orthophoto.validityMask))
            << "orthophoto_projected_candidates"
            << static_cast<double>(orthophoto.projectedCandidateCount)
            << "orthophoto_occluded_candidates"
            << static_cast<double>(orthophoto.occludedCandidateCount)
            << "orthophoto_selected_contributions"
            << static_cast<double>(orthophoto.selectedContributionCount)
            << "orthophoto_used_seam_blending"
            << orthophoto.usedSeamBlending
            << "orthophoto_seam_optimization_iterations"
            << orthophoto.seamOptimizationIterations
            << "orthophoto_seam_label_changes"
            << static_cast<double>(orthophoto.seamLabelChangeCount)
            << "orthophoto_seam_pixels"
            << static_cast<double>(orthophoto.seamPixelCount)
            << "orthophoto_processed_tiles"
            << orthophoto.processedTileCount
            << "orthophoto_tile_halo_pixels"
            << orthophoto.tileHaloPixels
            << "geotiff_written" << geoTiffWritten
            << "geotiff_message" << geoTiffMessage
            << "geotiff_epsg" << projectedReference.epsgCode
            << "geotiff_crs_name" << projectedReference.crsName
            << "geotiff_local_linearization_error_metres"
            << projectedReference.localLinearizationErrorMetres
            << "multispectral_orthophoto_success"
            << multispectralOrthophoto.success
            << "multispectral_orthophoto_message"
            << multispectralOrthophoto.message
            << "multispectral_orthophoto_valid_pixels"
            << (multispectralOrthophoto.validityMask.empty()
                    ? 0 : cv::countNonZero(multispectralOrthophoto.validityMask))
            << "multispectral_orthophoto_capture_fallback_pixels"
            << static_cast<double>(
                   multispectralOrthophoto.unavailableSourceFallbackCount)
            << "multispectral_orthophoto_processed_tiles"
            << multispectralOrthophoto.processedTileCount
            << "multispectral_geotiff_written"
            << multispectralGeoTiffWritten
            << "multispectral_geotiff_message"
            << multispectralGeoTiffMessage
            << "reflectance_available" << false
            << "reflectance_message"
            << "No panel or DLS irradiance metadata was found; radiance is exported without synthesizing reflectance."
            << "multispectral_band_names"
            << multispectralOrthophoto.bandNames
            << "multispectral_centre_wavelengths_nm"
            << multispectralOrthophoto.centreWavelengthsNanometres
            << "depth_maps" << "[";

    for (size_t mapIndex = 0; mapIndex < result.depthMaps.size(); ++mapIndex) {
        const kestrel::DenseDepthMap &descriptor = result.depthMaps[mapIndex];
        kestrel::DenseDepthMap loaded;
        std::string loadError;
        if (descriptor.depth.empty()
            && !kestrel::PlaneSweepMvsBackend::loadConsistentDepthMap(
                result.depthMaps, mapIndex, options, &loaded,
                &loadError)) {
            std::cerr << "Could not load diagnostic depth map: "
                      << loadError << '\n';
            continue;
        }
        const kestrel::DenseDepthMap &map = descriptor.depth.empty()
            ? loaded : descriptor;
        const std::string name = captureNames.count(map.imageIndex)
            ? captureNames.at(map.imageIndex)
            : std::to_string(map.imageIndex);
        const int validPixels = cv::countNonZero(map.validityMask);
        std::vector<float> depths;
        std::vector<float> confidences;
        depths.reserve(validPixels);
        confidences.reserve(validPixels);
        double consistentViewSum = 0.0;
        double occludedViewSum = 0.0;
        for (int row = 0; row < map.depth.rows; ++row) {
            for (int column = 0; column < map.depth.cols; ++column) {
                if (!map.validityMask.at<uchar>(row, column)) {
                    continue;
                }
                depths.push_back(map.depth.at<float>(row, column));
                confidences.push_back(map.confidence.at<float>(row, column));
                consistentViewSum += map.consistentViewCount.at<uchar>(row, column);
                occludedViewSum += map.occludedViewCount.at<uchar>(row, column);
            }
        }

        cv::Mat normalized;
        map.depth.convertTo(
            normalized, CV_8U,
            255.0 / (map.depthRange.maximumDepth - map.depthRange.minimumDepth),
            -255.0 * map.depthRange.minimumDepth
                / (map.depthRange.maximumDepth - map.depthRange.minimumDepth));
        cv::Mat colour;
        cv::applyColorMap(normalized, colour, cv::COLORMAP_TURBO);
        colour.setTo(cv::Scalar::all(0), map.validityMask == 0);
        cv::imwrite((outputDirectory / (name + "_depth.png")).string(), colour);

        cv::Mat confidenceImage;
        map.confidence.convertTo(confidenceImage, CV_8U, 255.0);
        confidenceImage.setTo(0, map.validityMask == 0);
        cv::imwrite((outputDirectory / (name + "_confidence.png")).string(),
                    confidenceImage);
        cv::imwrite((outputDirectory / (name + "_validity.png")).string(),
                    map.validityMask);

        const double validFraction = map.depth.total() > 0
            ? static_cast<double>(validPixels) / map.depth.total() : 0.0;
        const double medianDepth = median(depths);
        const double medianConfidence = median(confidences);
        summary << "{" << "capture_index" << map.imageIndex
                << "capture_name" << name
                << "width" << map.depth.cols << "height" << map.depth.rows
                << "valid_pixels" << validPixels
                << "valid_fraction" << validFraction
                << "median_depth_metres" << medianDepth
                << "median_confidence" << medianConfidence
                << "mean_consistent_views"
                << (validPixels > 0 ? consistentViewSum / validPixels : 0.0)
                << "mean_occluded_views"
                << (validPixels > 0 ? occludedViewSum / validPixels : 0.0)
                << "minimum_depth_metres" << map.depthRange.minimumDepth
                << "maximum_depth_metres" << map.depthRange.maximumDepth
                << "processed_tiles" << map.processedTileCount
                << "resumed_tiles" << map.resumedTileCount
                << "tile_halo_pixels" << map.tileHaloPixels
                << "neighbour_count"
                << static_cast<int>(map.neighbourImageIndices.size()) << "}";

        std::cout << name << ": " << validPixels << "/" << map.depth.total()
                  << " valid (" << std::fixed << std::setprecision(1)
                  << validFraction * 100.0 << "%), median depth "
                  << std::setprecision(2) << medianDepth
                  << " m, median confidence " << medianConfidence << '\n';
    }
    summary << "]";
    writePointCloud(outputDirectory / "fused_point_cloud.ply", fusion.points,
                    "Kestrel confidence-weighted depth-map fusion");
    writePointCloud(outputDirectory / "filtered_point_cloud.ply",
                    filtered.points,
                    "Kestrel spatial and local-elevation filtered cloud");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 8) {
        std::cerr << "Usage: KestrelDenseReconstructionRealSmoke "
                     "<project-root> <output-directory> [first-capture-index] "
                     "[capture-count] [maximum-image-dimension] "
                     "[tile-size-pixels] [orthophoto-gsd-metres]\n";
        return 2;
    }
    const fs::path projectRoot = argv[1];
    const fs::path outputDirectory = argv[2];
    int firstCapture = 3;
    int captureCount = 5;
    int maximumImageDimension = 480;
    int tileSizePixels = 192;
    double orthophotoGroundSampleDistanceMetres = 0.10;
    try {
        firstCapture = argc >= 4 ? std::stoi(argv[3]) : firstCapture;
        captureCount = argc >= 5 ? std::stoi(argv[4]) : captureCount;
        maximumImageDimension = argc >= 6
            ? std::stoi(argv[5]) : maximumImageDimension;
        tileSizePixels = argc >= 7
            ? std::stoi(argv[6]) : tileSizePixels;
        orthophotoGroundSampleDistanceMetres = argc >= 8
            ? std::stod(argv[7]) : orthophotoGroundSampleDistanceMetres;
    } catch (const std::exception &) {
        std::cerr << "Capture indices, counts, dimensions, tile size, and orthophoto GSD are invalid.\n";
        return 2;
    }
    if (firstCapture < 0 || captureCount < 2
        || maximumImageDimension < 32 || tileSizePixels < 16
        || !std::isfinite(orthophotoGroundSampleDistanceMetres)
        || orthophotoGroundSampleDistanceMetres <= 0.0) {
        std::cerr << "Invalid capture range, maximum image dimension, tile size, or orthophoto GSD.\n";
        return 2;
    }

    kestrel::DenseReconstructionInput input;
    std::vector<kestrel::MultispectralOrthophotoSource> multispectralSources;
    std::map<int, std::string> captureNames;
    kestrel::GeoCoordinate frameOrigin;
    std::string error;
    if (!loadSavedReconstruction(projectRoot, firstCapture, captureCount,
                                 &input, &multispectralSources, &captureNames,
                                 &frameOrigin, &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    kestrel::DenseMvsOptions options;
    options.maximumImageDimension = maximumImageDimension;
    options.pyramidLevels = 3;
    options.depthHypotheses = 64;
    options.maximumNeighbours = 4;
    options.minimumSharedSparsePoints = 12;
    options.minimumSupportingViews = 2;
    options.minimumConfidence = 0.01;
    options.tileSizePixels = tileSizePixels;
    options.checkpointDirectory =
        (outputDirectory / "dense_mvs_checkpoints").string();
    options.streamConsistencyFromCheckpoints = true;
    std::cout << "Running dense MVS on " << input.views.size()
              << " cropped real captures, " << input.sparsePoints.size()
              << " sparse points, and " << input.tracks.size()
              << " tracks; source crop="
              << input.views.front().image.cols << 'x'
              << input.views.front().image.rows
              << ", maximum dimension=" << maximumImageDimension
              << ", tile core=" << tileSizePixels << ".\n";

    const kestrel::PlaneSweepMvsBackend backend;
    const auto denseStart = std::chrono::steady_clock::now();
    const kestrel::DenseReconstructionResult result = backend.reconstruct(
        input, options);
    const double denseReconstructionSeconds =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - denseStart).count();
    kestrel::DepthMapFusionOptions fusionOptions;
    fusionOptions.voxelSizeMetres = 0.10;
    fusionOptions.minimumObservations = 2;
    fusionOptions.minimumInputConfidence = 0.01f;
    const kestrel::DepthMapFusionResult fusion = result.success
        ? kestrel::DepthMapFusion::fuseStreaming(
              input.views, result.depthMaps.size(),
              [&result, &options](size_t index,
                                  kestrel::DenseDepthMap *map,
                                  std::string *errorMessage) {
                  return kestrel::PlaneSweepMvsBackend::
                      loadConsistentDepthMap(
                          result.depthMaps, index, options, map,
                          errorMessage);
              }, fusionOptions)
        : kestrel::DepthMapFusionResult{};
    kestrel::DensePointCloudFilterOptions filterOptions;
    filterOptions.minimumConfidence = 0.01f;
    filterOptions.spatialSearchRadiusMetres = 0.35;
    filterOptions.minimumSpatialNeighbours = 5;
    filterOptions.nearestNeighbourCount = 12;
    filterOptions.maximumMeanNeighbourDistanceMetres = 0.25;
    filterOptions.elevationSearchRadiusMetres = 0.45;
    filterOptions.minimumElevationNeighbours = 5;
    filterOptions.minimumElevationToleranceMetres = 0.25;
    const kestrel::DensePointCloudFilterResult filtered = fusion.success
        ? kestrel::DensePointCloudFilter::filter(fusion.points, filterOptions)
        : kestrel::DensePointCloudFilterResult{};
    fs::create_directories(outputDirectory);
    const DsmDiagnostic dsm = filtered.success
        ? buildDiagnosticDsm(filtered.points, outputDirectory)
        : DsmDiagnostic{};
    kestrel::ProjectedRasterTransform dsmProjectedReference;
    bool dsmGeoTiffWritten = false;
    std::string dsmGeoTiffMessage;
    if (dsm.success
        && kestrel::createProjectedRasterTransform(
            kestrel::LocalTangentPlane(frameOrigin), dsm.terrain.origin,
            dsm.terrain.spacing, dsm.terrain.elevation.size(),
            &dsmProjectedReference, &dsmGeoTiffMessage)) {
        kestrel::GeoTiffWriteOptions dsmWriteOptions;
        dsmWriteOptions.noDataValue = "nan";
        dsmGeoTiffWritten = kestrel::GeoTiffWriter::write(
            outputDirectory / "filtered_dsm.tif", dsm.terrain.elevation,
            dsm.terrain.validityMask, dsmProjectedReference,
            dsmWriteOptions, &dsmGeoTiffMessage);
        if (dsmGeoTiffWritten) {
            dsmGeoTiffMessage = "Projected float DSM GeoTIFF written in "
                + dsmProjectedReference.crsName + ".";
        }
    }
    kestrel::ProjectedRasterTransform projectedReference;
    bool geoTiffWritten = false;
    std::string geoTiffMessage;
    const kestrel::MultiCameraOrthophotoResult orthophoto = dsm.success
        ? buildDiagnosticOrthophoto(
              input.views, dsm, outputDirectory,
              orthophotoGroundSampleDistanceMetres, frameOrigin,
              &projectedReference, &geoTiffWritten, &geoTiffMessage)
        : kestrel::MultiCameraOrthophotoResult{};
    bool multispectralGeoTiffWritten = false;
    std::string multispectralGeoTiffMessage;
    const kestrel::SharedGeometryMultispectralResult multispectralOrthophoto =
        orthophoto.success && geoTiffWritten
        ? buildDiagnosticMultispectralOrthophoto(
              multispectralSources, dsm, orthophoto.outputGrid,
              outputDirectory, projectedReference,
              &multispectralGeoTiffWritten, &multispectralGeoTiffMessage)
        : kestrel::SharedGeometryMultispectralResult{};
    writeDiagnostics(outputDirectory, result, fusion, filtered, dsm,
                     dsmProjectedReference, dsmGeoTiffWritten,
                     dsmGeoTiffMessage, orthophoto, projectedReference, geoTiffWritten,
                     geoTiffMessage, multispectralOrthophoto,
                     multispectralGeoTiffWritten,
                     multispectralGeoTiffMessage, captureNames, options,
                     denseReconstructionSeconds);
    std::cout << result.message << '\n'
              << "Raw valid pixels: " << result.rawValidPixelCount << '\n'
              << "Consistent pixels: " << result.consistentPixelCount << '\n'
              << "Dense tiles: " << result.processedTileCount
              << "; resumed=" << result.resumedTileCount
              << "; dense seconds=" << denseReconstructionSeconds << '\n'
              << "Consistency tiles: "
              << result.processedConsistencyTileCount
              << "; resumed=" << result.resumedConsistencyTileCount << '\n'
              << fusion.message << '\n'
              << "Fused points: " << fusion.points.size() << " from "
              << fusion.validInputSampleCount << " valid samples in "
              << fusion.candidateVoxelCount << " candidate voxels.\n"
              << filtered.message << '\n'
              << "Filtered points: " << filtered.points.size()
              << "; isolated=" << filtered.isolatedPointCount
              << ", statistical=" << filtered.statisticalOutlierCount
              << ", elevation=" << filtered.elevationOutlierCount << ".\n"
              << dsm.message << '\n'
              << dsmGeoTiffMessage << '\n'
              << orthophoto.message << '\n'
              << geoTiffMessage << '\n'
              << "Orthophoto grid: " << orthophoto.outputGrid.size.width
              << 'x' << orthophoto.outputGrid.size.height << " at "
              << std::abs(orthophoto.outputGrid.pixelSize[0])
              << " m/pixel.\n"
              << "Orthophoto valid pixels: "
              << (orthophoto.validityMask.empty()
                      ? 0 : cv::countNonZero(orthophoto.validityMask))
              << "; occluded candidates="
              << orthophoto.occludedCandidateCount
              << "; seam pixels=" << orthophoto.seamPixelCount
              << "; tiles=" << orthophoto.processedTileCount << ".\n"
              << multispectralOrthophoto.message << '\n'
              << multispectralGeoTiffMessage << '\n'
              << "Five-band radiance valid pixels: "
              << (multispectralOrthophoto.validityMask.empty()
                      ? 0 : cv::countNonZero(multispectralOrthophoto.validityMask))
              << "; capture fallbacks="
              << multispectralOrthophoto.unavailableSourceFallbackCount
              << "; complete multispectral sources="
              << multispectralSources.size() << ".\n"
              << "Reflectance/NDVI unavailable: no measured panel or DLS irradiance was found.\n";
    return result.success && fusion.success && filtered.success && dsm.success
           && dsmGeoTiffWritten && orthophoto.success && geoTiffWritten
           && multispectralOrthophoto.success
           && multispectralGeoTiffWritten
        ? 0 : 1;
}

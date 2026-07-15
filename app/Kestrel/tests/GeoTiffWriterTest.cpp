#include "photogrammetry/io/GeoTiffWriter.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <chrono>
#include <iostream>
#include <vector>

#ifdef KESTREL_HAS_LIBTIFF_GEOTIFF
#include <tiffio.h>
#endif

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

#ifdef KESTREL_HAS_LIBTIFF_GEOTIFF
constexpr ttag_t kModelTransformationTag = 34264;
constexpr ttag_t kGeoKeyDirectoryTag = 34735;
constexpr ttag_t kGdalMetadataTag = 42112;
constexpr ttag_t kGdalNoDataTag = 42113;
TIFFExtendProc parentExtender = nullptr;

void registerFields(TIFF *tiff)
{
    const TIFFFieldInfo fields[] = {
        {kModelTransformationTag, 16, 16, TIFF_DOUBLE, FIELD_CUSTOM, 1, 0,
         const_cast<char *>("ModelTransformationTag")},
        {kGeoKeyDirectoryTag, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_SHORT,
         FIELD_CUSTOM, 1, 1, const_cast<char *>("GeoKeyDirectoryTag")},
        {kGdalMetadataTag, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_ASCII,
         FIELD_CUSTOM, 1, 0, const_cast<char *>("GDAL_METADATA")},
        {kGdalNoDataTag, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_ASCII,
         FIELD_CUSTOM, 1, 0, const_cast<char *>("GDAL_NODATA")}};
    TIFFMergeFieldInfo(tiff, fields,
                       static_cast<uint32_t>(std::size(fields)));
}

void extendFields(TIFF *tiff)
{
    if (parentExtender) parentExtender(tiff);
    registerFields(tiff);
}
#endif

bool writeAndReadTest()
{
#ifndef KESTREL_HAS_LIBTIFF_GEOTIFF
    return true;
#else
    const std::filesystem::path path =
        std::filesystem::current_path()
        / ("geotiff_writer_test_"
           + std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count())
           + ".tif");
    std::error_code filesystemError;
    std::filesystem::remove(path, filesystemError);
    cv::Mat image(2, 2, CV_16U);
    image.at<uint16_t>(0, 0) = 100;
    image.at<uint16_t>(0, 1) = 200;
    image.at<uint16_t>(1, 0) = 300;
    image.at<uint16_t>(1, 1) = 400;
    cv::Mat mask(2, 2, CV_8U, cv::Scalar(255));
    mask.at<uchar>(0, 1) = 0;
    kestrel::ProjectedRasterTransform reference;
    std::string error;
    if (!kestrel::createProjectedRasterTransform(
            kestrel::LocalTangentPlane({0.0, -81.0, 0.0}),
            {0.0, 0.0}, {1.0, -1.0}, image.size(),
            &reference, &error)
        || !kestrel::GeoTiffWriter::write(
            path, image, mask, reference, {}, &error)) {
        std::cerr << error << '\n';
        return false;
    }
#ifdef _WIN32
    parentExtender = TIFFSetTagExtender(extendFields);
    TIFF *tiff = TIFFOpenW(path.c_str(), "r");
#else
    parentExtender = TIFFSetTagExtender(extendFields);
    TIFF *tiff = TIFFOpen(path.string().c_str(), "r");
#endif
    TIFFSetTagExtender(parentExtender);
    if (!expect(tiff != nullptr, "The written GeoTIFF should reopen.")) {
        return false;
    }
    uint32_t width = 0, height = 0;
    uint16_t samples = 0;
    double *matrix = nullptr;
    uint32_t keyCount = 0;
    uint16_t *keys = nullptr;
    char *noData = nullptr;
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    const int hasTransform = TIFFGetField(
        tiff, kModelTransformationTag, &matrix);
    const int hasKeys = TIFFGetField(
        tiff, kGeoKeyDirectoryTag, &keyCount, &keys);
    const int hasNoData = TIFFGetField(tiff, kGdalNoDataTag, &noData);
    std::vector<uint16_t> row(4);
    const bool readRow = TIFFReadScanline(tiff, row.data(), 0, 0) >= 0;
    const bool metadata = width == 2 && height == 2 && samples == 2
        && hasTransform && matrix && hasKeys && keys && keyCount == 16
        && keys[4] == 1024 && keys[7] == 1
        && keys[8] == 1025 && keys[11] == 1
        && keys[12] == 3072 && keys[15] == 32617
        && hasNoData && noData && std::string(noData) == "0";
    const bool pixels = readRow && row[0] == 100 && row[1] == 65535
        && row[2] == 200 && row[3] == 0;
    TIFFClose(tiff);
    std::filesystem::remove(path, filesystemError);
    return expect(metadata,
                  "GeoTIFF projected CRS, transform, alpha, and no-data metadata should round-trip.")
           && expect(pixels,
                     "GeoTIFF source values and validity alpha should round-trip.");
#endif
}

bool floatRgbRadianceTest()
{
#ifndef KESTREL_HAS_LIBTIFF_GEOTIFF
    return true;
#else
    const std::filesystem::path path = std::filesystem::current_path()
        / ("geotiff_rgb_float_test_"
           + std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count())
           + ".tif");
    std::error_code filesystemError;
    cv::Mat bgr(1, 2, CV_32FC3);
    bgr.at<cv::Vec3f>(0, 0) = {1.0f, 2.0f, 3.0f};
    bgr.at<cv::Vec3f>(0, 1) = {4.0f, 5.0f, 6.0f};
    cv::Mat mask(1, 2, CV_8U, cv::Scalar(255));
    mask.at<uchar>(0, 1) = 0;
    kestrel::ProjectedRasterTransform reference;
    std::string error;
    if (!kestrel::createProjectedRasterTransform(
            kestrel::LocalTangentPlane({0.0, -81.0, 0.0}),
            {0.0, 0.0}, {1.0, -1.0}, bgr.size(), &reference, &error)
        || !kestrel::GeoTiffWriter::write(
            path, bgr, mask, reference, {}, &error)) {
        std::cerr << error << '\n';
        return false;
    }
    parentExtender = TIFFSetTagExtender(extendFields);
#ifdef _WIN32
    TIFF *tiff = TIFFOpenW(path.c_str(), "r");
#else
    TIFF *tiff = TIFFOpen(path.string().c_str(), "r");
#endif
    TIFFSetTagExtender(parentExtender);
    if (!tiff) return false;
    uint16_t samples = 0;
    uint16_t sampleFormat = 0;
    uint16_t photometric = 0;
    TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetField(tiff, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
    TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric);
    std::vector<float> row(8);
    const bool readRow = TIFFReadScanline(tiff, row.data(), 0, 0) >= 0;
    TIFFClose(tiff);
    std::filesystem::remove(path, filesystemError);
    return expect(samples == 4 && sampleFormat == SAMPLEFORMAT_IEEEFP
                      && photometric == PHOTOMETRIC_RGB,
                  "RGB radiance should use three float samples plus alpha.")
           && expect(readRow && row[0] == 3.0f && row[1] == 2.0f
                         && row[2] == 1.0f && row[3] == 1.0f
                         && row[4] == 6.0f && row[5] == 5.0f
                         && row[6] == 4.0f && row[7] == 0.0f,
                     "Float BGR radiance should round-trip as TIFF RGB with validity alpha.");
#endif
}

bool floatFiveBandRadianceTest()
{
#ifndef KESTREL_HAS_LIBTIFF_GEOTIFF
    return true;
#else
    const std::filesystem::path path = std::filesystem::current_path()
        / ("geotiff_five_band_float_test_"
           + std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count())
           + ".tif");
    std::error_code filesystemError;
    cv::Mat radiance(1, 2, CV_MAKETYPE(CV_32F, 5));
    for (int channel = 0; channel < 5; ++channel) {
        radiance.ptr<float>(0)[channel] = static_cast<float>(channel + 1);
        radiance.ptr<float>(0)[5 + channel] = static_cast<float>(channel + 6);
    }
    cv::Mat mask(1, 2, CV_8U, cv::Scalar(255));
    mask.at<uchar>(0, 1) = 0;
    kestrel::ProjectedRasterTransform reference;
    std::string error;
    kestrel::GeoTiffWriteOptions options;
    options.threeChannelInputIsBgr = false;
    options.bandNames = {"Blue", "Green", "Red", "NIR", "Red edge"};
    options.centreWavelengthsNanometres = {475.0, 560.0, 668.0, 840.0, 717.0};
    if (!kestrel::createProjectedRasterTransform(
            kestrel::LocalTangentPlane({0.0, -81.0, 0.0}),
            {0.0, 0.0}, {1.0, -1.0}, radiance.size(), &reference, &error)
        || !kestrel::GeoTiffWriter::write(
            path, radiance, mask, reference, options, &error)) {
        std::cerr << error << '\n';
        return false;
    }
    parentExtender = TIFFSetTagExtender(extendFields);
#ifdef _WIN32
    TIFF *tiff = TIFFOpenW(path.c_str(), "r");
#else
    TIFF *tiff = TIFFOpen(path.string().c_str(), "r");
#endif
    TIFFSetTagExtender(parentExtender);
    if (!tiff) return false;
    uint16_t samples = 0;
    uint16_t sampleFormat = 0;
    uint16_t photometric = 0;
    uint16_t extraCount = 0;
    uint16_t *extraTypes = nullptr;
    char *metadata = nullptr;
    TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetField(tiff, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
    TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric);
    TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes);
    const int hasMetadata = TIFFGetField(tiff, kGdalMetadataTag, &metadata);
    std::vector<float> row(12);
    const bool readRow = TIFFReadScanline(tiff, row.data(), 0, 0) >= 0;
    const std::string metadataText = metadata ? metadata : "";
    const bool layout = samples == 6 && sampleFormat == SAMPLEFORMAT_IEEEFP
        && photometric == PHOTOMETRIC_MINISBLACK && extraCount == 5
        && extraTypes && extraTypes[0] == EXTRASAMPLE_UNSPECIFIED
        && extraTypes[3] == EXTRASAMPLE_UNSPECIFIED
        && extraTypes[4] == EXTRASAMPLE_UNASSALPHA;
    const bool metadataOk = hasMetadata
        && metadataText.find("Blue") != std::string::npos
        && metadataText.find("Red edge") != std::string::npos
        && metadataText.find("CENTRAL_WAVELENGTH_NM") != std::string::npos
        && metadataText.find("840") != std::string::npos;
    bool pixels = readRow;
    for (int channel = 0; channel < 5 && pixels; ++channel) {
        pixels = row[channel] == static_cast<float>(channel + 1)
            && row[6 + channel] == static_cast<float>(channel + 6);
    }
    pixels = pixels && row[5] == 1.0f && row[11] == 0.0f;
    TIFFClose(tiff);
    std::filesystem::remove(path, filesystemError);
    return expect(layout,
                  "Five-band radiance should use ordered float samples plus alpha.")
           && expect(metadataOk,
                     "Five-band names and wavelengths should round-trip as GDAL metadata.")
           && expect(pixels,
                     "Five-band values must retain their declared order and validity alpha.");
#endif
}

} // namespace

int main()
{
    if (writeAndReadTest() && floatRgbRadianceTest()
        && floatFiveBandRadianceTest()) {
        std::cout << "GeoTIFF writer tests passed.\n";
        return 0;
    }
    return 1;
}

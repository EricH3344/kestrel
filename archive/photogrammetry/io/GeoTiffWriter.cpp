#include "photogrammetry/io/GeoTiffWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <sstream>
#include <type_traits>
#include <vector>

#ifdef KESTREL_HAS_LIBTIFF_GEOTIFF
#include <tiffio.h>
#endif

namespace kestrel {

namespace {

void setError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) *errorMessage = message;
}

#ifdef KESTREL_HAS_LIBTIFF_GEOTIFF

constexpr ttag_t kModelTransformationTag = 34264;
constexpr ttag_t kGeoKeyDirectoryTag = 34735;
constexpr ttag_t kGdalMetadataTag = 42112;
constexpr ttag_t kGdalNoDataTag = 42113;

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

TIFF *openTiff(const std::filesystem::path &path, const char *mode)
{
#ifdef _WIN32
    return TIFFOpenW(path.c_str(), mode);
#else
    return TIFFOpen(path.string().c_str(), mode);
#endif
}

template <typename Value>
void writeRow(const cv::Mat &image, const cv::Mat &validityMask,
              int row, int outputChannels, bool threeChannelInputIsBgr,
              std::vector<Value> *scanline)
{
    const int inputChannels = image.channels();
    const Value opaque = std::is_floating_point_v<Value>
        ? static_cast<Value>(1.0)
        : std::numeric_limits<Value>::max();
    scanline->resize(static_cast<size_t>(image.cols) * outputChannels);
    const Value *source = image.ptr<Value>(row);
    const uchar *valid = validityMask.empty()
        ? nullptr : validityMask.ptr<uchar>(row);
    for (int column = 0; column < image.cols; ++column) {
        Value *destination = scanline->data()
            + static_cast<size_t>(column) * outputChannels;
        if (inputChannels == 3 && threeChannelInputIsBgr) {
            // OpenCV stores BGR; TIFF RGB photometric data stores RGB.
            destination[0] = source[column * inputChannels + 2];
            destination[1] = source[column * inputChannels + 1];
            destination[2] = source[column * inputChannels];
        } else {
            for (int channel = 0; channel < inputChannels; ++channel) {
                destination[channel] =
                    source[column * inputChannels + channel];
            }
        }
        if (outputChannels > inputChannels) {
            destination[inputChannels] = valid && !valid[column]
                ? static_cast<Value>(0) : opaque;
        }
    }
}

std::string xmlEscape(const std::string &value)
{
    std::string result;
    for (char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '\"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += character; break;
        }
    }
    return result;
}

std::string gdalBandMetadata(const GeoTiffWriteOptions &options,
                             int channelCount)
{
    if (options.bandNames.empty()
        && options.centreWavelengthsNanometres.empty()) {
        return {};
    }
    std::ostringstream xml;
    xml.precision(12);
    xml << "<GDALMetadata>";
    for (int channel = 0; channel < channelCount; ++channel) {
        if (!options.bandNames.empty()) {
            xml << "<Item name=\"DESCRIPTION\" sample=\"" << channel
                << "\" role=\"description\">"
                << xmlEscape(options.bandNames[channel]) << "</Item>";
        }
        if (!options.centreWavelengthsNanometres.empty()) {
            xml << "<Item name=\"CENTRAL_WAVELENGTH_NM\" sample=\""
                << channel << "\">"
                << options.centreWavelengthsNanometres[channel]
                << "</Item>";
        }
    }
    xml << "</GDALMetadata>";
    return xml.str();
}

#endif

} // namespace

bool GeoTiffWriter::write(const std::filesystem::path &path,
                          const cv::Mat &image,
                          const cv::Mat &validityMask,
                          const ProjectedRasterTransform &reference,
                          const GeoTiffWriteOptions &options,
                          std::string *errorMessage)
{
#ifndef KESTREL_HAS_LIBTIFF_GEOTIFF
    (void)path; (void)image; (void)validityMask; (void)reference;
    (void)options;
    setError(errorMessage,
             "GeoTIFF export requires Kestrel's bundled libtiff build.");
    return false;
#else
    if (path.empty() || image.empty()
        || image.channels() <= 0 || image.channels() > CV_CN_MAX
        || (image.depth() != CV_8U && image.depth() != CV_16U
            && image.depth() != CV_32F)
        || (!validityMask.empty()
            && (validityMask.type() != CV_8UC1
                || validityMask.size() != image.size()))
        || (!options.bandNames.empty()
            && options.bandNames.size()
                   != static_cast<size_t>(image.channels()))
        || (!options.centreWavelengthsNanometres.empty()
            && options.centreWavelengthsNanometres.size()
                   != static_cast<size_t>(image.channels()))
        || !std::all_of(options.centreWavelengthsNanometres.begin(),
                        options.centreWavelengthsNanometres.end(),
                        [](double value) {
                            return std::isfinite(value) && value > 0.0;
                        })
        || !reference.isValid() || reference.epsgCode > 65535) {
        setError(errorMessage, "GeoTIFF image, mask, path, or reference is invalid.");
        return false;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        setError(errorMessage, "Could not create GeoTIFF directory: "
            + filesystemError.message());
        return false;
    }
    const std::filesystem::path temporary =
        path.parent_path() / (path.filename().string() + ".tmp.tif");
    std::filesystem::remove(temporary, filesystemError);
    const uint64_t estimatedBytes = static_cast<uint64_t>(image.total())
        * (image.channels() + (options.writeValidityAsAlpha
                              && !validityMask.empty() ? 1 : 0))
        * image.elemSize1();
    TIFF *tiff = openTiff(temporary,
                          estimatedBytes > 0xffffffffULL ? "w8" : "w");
    if (!tiff) {
        setError(errorMessage, "Could not open the temporary GeoTIFF output.");
        return false;
    }
    registerFields(tiff);
    const bool hasAlpha = options.writeValidityAsAlpha
        && !validityMask.empty();
    const uint16_t samples = static_cast<uint16_t>(
        image.channels() + (hasAlpha ? 1 : 0));
    const uint16_t bits = static_cast<uint16_t>(image.elemSize1() * 8);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH,
                 static_cast<uint32_t>(image.cols));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH,
                 static_cast<uint32_t>(image.rows));
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, samples);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, bits);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC,
                 image.channels() == 3 ? PHOTOMETRIC_RGB
                                       : PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT,
                 image.depth() == CV_32F ? SAMPLEFORMAT_IEEEFP
                                         : SAMPLEFORMAT_UINT);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP,
                 TIFFDefaultStripSize(tiff, 0));
    if (options.useDeflateCompression) {
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
        TIFFSetField(tiff, TIFFTAG_PREDICTOR,
                     image.depth() == CV_32F ? PREDICTOR_FLOATINGPOINT
                                             : PREDICTOR_HORIZONTAL);
    }
    const int photometricSamples = image.channels() == 3 ? 3 : 1;
    const int extraSampleCount = samples - photometricSamples;
    std::vector<uint16_t> extraSampleTypes(
        std::max(0, extraSampleCount), EXTRASAMPLE_UNSPECIFIED);
    if (hasAlpha && !extraSampleTypes.empty()) {
        extraSampleTypes.back() = EXTRASAMPLE_UNASSALPHA;
    }
    if (!extraSampleTypes.empty()) {
        TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES,
                     static_cast<uint16_t>(extraSampleTypes.size()),
                     extraSampleTypes.data());
    }
    double modelTransform[16];
    std::copy(std::begin(reference.rasterToProjected.val),
              std::end(reference.rasterToProjected.val), modelTransform);
    TIFFSetField(tiff, kModelTransformationTag, modelTransform);
    const uint16_t geoKeys[] = {
        1, 1, 1, 3,
        1024, 0, 1, 1, // GTModelTypeGeoKey: projected 2D.
        1025, 0, 1, 1, // GTRasterTypeGeoKey: PixelIsArea.
        3072, 0, 1, static_cast<uint16_t>(reference.epsgCode)};
    TIFFSetField(tiff, kGeoKeyDirectoryTag,
                 static_cast<uint32_t>(std::size(geoKeys)), geoKeys);
    if (!options.noDataValue.empty()) {
        TIFFSetField(tiff, kGdalNoDataTag, options.noDataValue.c_str());
    }
    const std::string bandMetadata = gdalBandMetadata(
        options, image.channels());
    if (!bandMetadata.empty()) {
        TIFFSetField(tiff, kGdalMetadataTag, bandMetadata.c_str());
    }

    bool wrote = true;
    if (image.depth() == CV_8U) {
        std::vector<uint8_t> row;
        for (int index = 0; index < image.rows && wrote; ++index) {
            writeRow(image, validityMask, index, samples,
                     options.threeChannelInputIsBgr, &row);
            wrote = TIFFWriteScanline(tiff, row.data(), index, 0) >= 0;
        }
    } else if (image.depth() == CV_16U) {
        std::vector<uint16_t> row;
        for (int index = 0; index < image.rows && wrote; ++index) {
            writeRow(image, validityMask, index, samples,
                     options.threeChannelInputIsBgr, &row);
            wrote = TIFFWriteScanline(tiff, row.data(), index, 0) >= 0;
        }
    } else {
        std::vector<float> row;
        for (int index = 0; index < image.rows && wrote; ++index) {
            writeRow(image, validityMask, index, samples,
                     options.threeChannelInputIsBgr, &row);
            wrote = TIFFWriteScanline(tiff, row.data(), index, 0) >= 0;
        }
    }
    TIFFClose(tiff);
    if (!wrote) {
        std::filesystem::remove(temporary, filesystemError);
        setError(errorMessage, "libtiff could not write a GeoTIFF scanline.");
        return false;
    }
    std::filesystem::remove(path, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        setError(errorMessage, "Could not finalize GeoTIFF output: "
            + filesystemError.message());
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
#endif
}

} // namespace kestrel

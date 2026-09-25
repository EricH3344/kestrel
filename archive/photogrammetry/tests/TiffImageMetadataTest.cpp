#include "photogrammetry/io/TiffImageMetadata.h"

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool xmpParsingTest()
{
    const QByteArray packet = R"xml(
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description xmlns:Camera="http://pix4d.com/camera/1.0/"
                     xmlns:MicaSense="http://micasense.com/">
      <Camera:BandName>Red edge</Camera:BandName>
      <Camera:RigCameraIndex>3</Camera:RigCameraIndex>
      <Camera:CentralWavelength>717</Camera:CentralWavelength>
      <Camera:PerspectiveFocalLength>5.411</Camera:PerspectiveFocalLength>
      <Camera:PrincipalPoint>2.4, 1.8</Camera:PrincipalPoint>
      <Camera:PerspectiveDistortion><rdf:Seq>
        <rdf:li>-0.1</rdf:li><rdf:li>0.2</rdf:li><rdf:li>-0.03</rdf:li>
        <rdf:li>0.001</rdf:li><rdf:li>-0.002</rdf:li>
      </rdf:Seq></Camera:PerspectiveDistortion>
      <Camera:RigRelatives><rdf:Seq>
        <rdf:li>0.5</rdf:li><rdf:li>0.2</rdf:li><rdf:li>-0.3</rdf:li>
      </rdf:Seq></Camera:RigRelatives>
      <Camera:VignettingCenter><rdf:Seq>
        <rdf:li>649.5</rdf:li><rdf:li>473.9</rdf:li>
      </rdf:Seq></Camera:VignettingCenter>
      <Camera:VignettingPolynomial><rdf:Seq>
        <rdf:li>1e-6</rdf:li><rdf:li>8e-7</rdf:li><rdf:li>-8e-9</rdf:li>
        <rdf:li>2e-11</rdf:li><rdf:li>-3e-14</rdf:li><rdf:li>1e-17</rdf:li>
      </rdf:Seq></Camera:VignettingPolynomial>
      <Camera:BandSensitivity>0.3968</Camera:BandSensitivity>
      <MicaSense:RadiometricCalibration><rdf:Seq>
        <rdf:li>9.59e-5</rdf:li><rdf:li>1.2e-7</rdf:li>
        <rdf:li>1.69e-5</rdf:li>
      </rdf:Seq></MicaSense:RadiometricCalibration>
      <MicaSense:CaptureId>capture-42</MicaSense:CaptureId>
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>)xml";
    kestrel::TiffImageMetadata metadata;
    kestrel::TiffMetadataReader::parseXmpPacket(packet, &metadata);
    return expect(metadata.bandName == "Red edge", "Band name should parse from XMP.")
           && expect(metadata.captureId == "capture-42",
                     "Physical capture ID should parse from XMP.")
           && expect(metadata.rigCameraIndex == 3, "Rig camera index should parse.")
           && expect(metadata.hasCentralWavelength
                         && metadata.centralWavelengthNanometres == 717.0,
                     "Central wavelength should parse.")
           && expect(metadata.hasCalibratedFocalLength
                         && metadata.calibratedFocalLengthMillimetres == 5.411,
                     "Calibrated focal length should parse.")
           && expect(metadata.principalPointMillimetres.size() == 2,
                     "Principal point should contain two values.")
           && expect(metadata.perspectiveDistortion.size() == 5,
                     "Distortion should retain all vendor coefficients.")
           && expect(metadata.rigRelatives.size() == 3,
                     "Rig-relative values should parse.")
           && expect(metadata.vignettingCenter.size() == 2
                         && metadata.vignettingPolynomial.size() == 6,
                     "Vignette calibration should parse from XMP sequences.")
           && expect(metadata.radiometricCalibration.size() == 3,
                     "Radiometric coefficients should parse from XMP.")
           && expect(metadata.hasBandSensitivity,
                     "Band sensitivity should parse for diagnostics.");
}

bool validationTest()
{
    kestrel::TiffImageMetadata complete;
    complete.imageWidth = 1280;
    complete.imageHeight = 960;
    complete.hasOrientation = true;
    complete.bitsPerSample = 16;
    complete.hasBitsPerSample = true;
    complete.cameraMake = "MicaSense";
    complete.cameraModel = "RedEdge-M";
    complete.cameraSerial = "RX-test";
    complete.captureTime = "2026:07:04 21:50:32";
    complete.hasFocalLength = true;
    complete.focalLengthMillimetres = 5.5;
    complete.captureId = "capture-42";
    complete.bandName = "Blue";
    complete.principalPointMillimetres = {2.4, 1.8};
    complete.perspectiveDistortion = {-0.1, 0.2, -0.03, 0.001, -0.002};
    complete.rigRelatives = {0.5, 0.2, -0.3};
    complete.hasGps = true;
    complete.hasGpsAltitude = true;
    complete.gps = {45.3, -75.9, 113.4};

    kestrel::TiffImageMetadata incomplete;
    incomplete.bandName = "Blue";
    return expect(complete.validationWarnings().isEmpty(),
                  "Complete camera metadata should validate cleanly.")
           && expect(incomplete.validationWarnings().size() >= 5,
                     "Missing camera metadata should be reported together.")
           && expect(complete.cameraIdentifier().contains("RedEdge-M"),
                     "Camera identifier should be human-readable.");
}

} // namespace

int main()
{
    const bool success = xmpParsingTest() && validationTest();
    if (success) {
        std::cout << "TIFF/EXIF/XMP metadata tests passed.\n";
        return 0;
    }
    return 1;
}

#ifndef RIGCAMERAMODEL_H
#define RIGCAMERAMODEL_H

#include "photogrammetry/camera/CameraCalibration.h"
#include "photogrammetry/terrain/TerrainOrthorectifier.h"

#include <string>

namespace kestrel {

struct RigCameraDerivation
{
    PinholeCamera camera;
    bool usedRelativeRotation = false;
    bool usedRelativeTranslation = false;
};

class RigCameraModel
{
public:
    // Derives a spectral sensor's world-to-camera pose from the reconstructed
    // reference sensor and each sensor-to-rig calibration. Translation is
    // applied only when both calibrations explicitly provide it; otherwise the
    // shared optical-centre approximation is reported rather than inventing a
    // physical baseline.
    static bool deriveFromReference(
        const PinholeCamera &referenceCamera,
        const CameraCalibration &referenceCalibration,
        const CameraCalibration &bandCalibration,
        RigCameraDerivation *result,
        std::string *errorMessage = nullptr);
};

} // namespace kestrel

#endif // RIGCAMERAMODEL_H

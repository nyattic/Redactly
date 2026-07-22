#pragma once

#include <opencv2/core.hpp>

#include <cmath>
#include <vector>

namespace cloakframe
{
    struct FaceDetection
    {
        cv::Rect2f box;
        float score = 0.0F;
        float rollRadians = 0.0F;
        bool hasPose = false;
    };

    inline bool isValidFacePose(float rollRadians, bool hasPose)
    {
        constexpr float kMaximumSupportedRoll = 1.0471975512F;
        return hasPose && std::isfinite(rollRadians) &&
               std::abs(rollRadians) <= kMaximumSupportedRoll;
    }

    inline bool hasValidFacePose(const FaceDetection &detection)
    {
        return isValidFacePose(detection.rollRadians, detection.hasPose);
    }

    inline bool isValidFaceDetection(const FaceDetection &detection)
    {
        return std::isfinite(detection.score) &&
               std::isfinite(detection.box.x) &&
               std::isfinite(detection.box.y) &&
               std::isfinite(detection.box.width) &&
               std::isfinite(detection.box.height) &&
               detection.box.width > 0.0F && detection.box.height > 0.0F;
    }

    using FaceDetections = std::vector<FaceDetection>;
}

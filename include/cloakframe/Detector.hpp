#pragma once

#include "cloakframe/FaceDetection.hpp"

#include <opencv2/core.hpp>

namespace cloakframe
{
    class Detector
    {
    public:
        virtual ~Detector() = default;

        virtual FaceDetections detect(const cv::Mat &bgrImage, float scoreThreshold, float nmsThreshold) = 0;

        [[nodiscard]] virtual int inputSize() const noexcept { return 0; }

        [[nodiscard]] virtual const char *backendName() const noexcept { return "CPU"; }
    };
}

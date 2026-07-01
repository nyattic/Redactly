#pragma once

#include "faceveil/FaceDetection.hpp"

#include <opencv2/core.hpp>

namespace faceveil
{
    enum class AnonymizationMethod
    {
        Mosaic,
        Blur,
        Fill,
    };

    enum class MaskShape
    {
        Rectangle,
        Ellipse,
    };

    void applyAnonymization(cv::Mat &image, const FaceDetections &detections,
                            AnonymizationMethod method, int blockSize, float paddingRatio,
                            MaskShape shape = MaskShape::Rectangle);

    void applyMosaic(cv::Mat &image, const FaceDetections &detections,
                     int blockSize, float paddingRatio);
}

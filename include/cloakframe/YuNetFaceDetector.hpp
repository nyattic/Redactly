#pragma once

#include "cloakframe/Detector.hpp"

#include <opencv2/objdetect/face.hpp>

#include <QByteArray>

#include <string>

namespace cloakframe
{
    class YuNetFaceDetector final : public Detector
    {
    public:
        explicit YuNetFaceDetector(const std::string &modelPath,
                                   const QByteArray &expectedSha256 = {});

        FaceDetections detect(const cv::Mat &bgrImage, float scoreThreshold,
                              float nmsThreshold) override;

        [[nodiscard]] int inputSize() const noexcept override { return 640; }

        [[nodiscard]] const char *backendName() const noexcept override
        {
            return "OpenCV CPU";
        }

    private:
        cv::Ptr<cv::FaceDetectorYN> detector_;
    };
}

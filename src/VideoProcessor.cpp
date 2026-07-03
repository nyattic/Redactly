#include "redactly/VideoProcessor.hpp"

#include "redactly/PlateDetector.hpp"
#include "redactly/ScrfdFaceDetector.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace redactly
{
    namespace
    {
        QString trVideoProcessor(const char *text)
        {
            return QCoreApplication::translate("redactly::VideoProcessor", text);
        }

        FaceDetections detectFrame(const cv::Mat &frame,
                                   ScrfdFaceDetector *faceDetector,
                                   PlateDetector *plateDetector,
                                   const VideoProcessOptions &options)
        {
            const float detectionThreshold =
                    std::min(options.tracker.lowScoreThreshold, options.scoreThreshold);
            FaceDetections detections;
            if (faceDetector != nullptr)
            {
                detections = faceDetector->detect(frame, detectionThreshold,
                                                  options.nmsThreshold);
            }
            if (plateDetector != nullptr)
            {
                const auto plates = plateDetector->detect(frame, detectionThreshold,
                                                          options.nmsThreshold);
                detections.insert(detections.end(), plates.begin(), plates.end());
            }
            return detections;
        }
    }

    VideoProcessResult processVideo(const FfmpegTools &tools,
                                    const QString &sourcePath,
                                    const QString &destinationPath,
                                    const VideoInfo &info,
                                    const VideoProcessOptions &options,
                                    ScrfdFaceDetector *faceDetector,
                                    PlateDetector *plateDetector,
                                    const std::atomic<bool> &cancelled,
                                    const VideoProgressFn &progress)
    {
        VideoProcessResult result;

        std::vector<FaceDetections> frameDetections;
        if (info.estimatedFrameCount > 0)
        {
            frameDetections.reserve(static_cast<std::size_t>(info.estimatedFrameCount));
        }

        {
            VideoFrameReader reader;
            if (!reader.open(tools, sourcePath, info))
            {
                result.error = reader.errorString();
                return result;
            }

            cv::Mat frame;
            while (reader.readFrame(frame))
            {
                if (cancelled.load(std::memory_order_acquire))
                {
                    result.status = VideoProcessStatus::Cancelled;
                    return result;
                }
                frameDetections.push_back(
                    detectFrame(frame, faceDetector, plateDetector, options));
                if (progress)
                {
                    progress(1, static_cast<qint64>(frameDetections.size()),
                             std::max<qint64>(info.estimatedFrameCount,
                                              static_cast<qint64>(frameDetections.size())));
                }
            }
            if (!reader.errorString().isEmpty())
            {
                result.error = reader.errorString();
                return result;
            }
        }

        const auto frameCount = static_cast<qint64>(frameDetections.size());
        result.frameCount = frameCount;
        if (frameCount == 0)
        {
            result.error = trVideoProcessor("No frames could be decoded.");
            return result;
        }

        TrackerConfig trackerConfig = options.tracker;
        trackerConfig.highScoreThreshold = options.scoreThreshold;
        auto tracks = buildBidirectionalTracks(frameDetections, trackerConfig);
        postProcessTracks(tracks, options.postProcess, static_cast<int>(frameCount));
        result.trackCount = static_cast<int>(tracks.size());
        frameDetections.clear();
        frameDetections.shrink_to_fit();

        VideoFrameReader reader;
        if (!reader.open(tools, sourcePath, info))
        {
            result.error = reader.errorString();
            return result;
        }
        VideoFrameWriter writer;
        if (!writer.open(tools, destinationPath, sourcePath, info, options.crf))
        {
            result.error = writer.errorString();
            return result;
        }

        qint64 frameIndex = 0;
        cv::Mat frame;
        while (reader.readFrame(frame))
        {
            if (cancelled.load(std::memory_order_acquire))
            {
                writer.abort();
                result.status = VideoProcessStatus::Cancelled;
                return result;
            }

            const auto regions =
                    trackRegionsForFrame(tracks, static_cast<int>(frameIndex));
            FaceDetections toRedact;
            toRedact.reserve(regions.size());
            for (const auto &region: regions)
            {
                toRedact.push_back({region, 1.0F});
            }
            applyAnonymization(frame, toRedact, options.method, options.mosaicBlockSize,
                               options.paddingRatio, options.shape, options.softEdges);

            if (!writer.writeFrame(frame))
            {
                result.error = writer.errorString();
                writer.abort();
                return result;
            }
            ++frameIndex;
            if (progress)
            {
                progress(2, frameIndex, frameCount);
            }
        }
        if (!reader.errorString().isEmpty())
        {
            result.error = reader.errorString();
            writer.abort();
            return result;
        }
        if (frameIndex == 0)
        {
            result.error = trVideoProcessor("No frames could be decoded.");
            writer.abort();
            return result;
        }

        if (!writer.finish())
        {
            result.error = writer.errorString();
            return result;
        }

        result.status = VideoProcessStatus::Completed;
        return result;
    }
}

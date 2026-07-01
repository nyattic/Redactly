#include "faceveil/ImageIo.hpp"
#include "faceveil/ImageScanner.hpp"
#include "faceveil/Mosaic.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <set>

#ifdef FACEVEIL_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace
{
    void writeBytes(const QString &path)
    {
        QFile file(path);
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write("x") == 1);
    }

    void testSupportedImageExtensions()
    {
        assert(faceveil::isSupportedImage("photo.jpg"));
        assert(faceveil::isSupportedImage("photo.JPEG"));
        assert(faceveil::isSupportedImage("photo.webp"));
        assert(!faceveil::isSupportedImage("photo.txt"));
    }

    void testScanImagesRecursesAndDeduplicates()
    {
        QTemporaryDir temp;
        assert(temp.isValid());

        QDir root(temp.path());
        assert(root.mkpath("a/nested"));
        assert(root.mkpath("b"));

        const QString first = root.filePath("a/one.JPG");
        const QString second = root.filePath("a/nested/two.png");
        const QString ignored = root.filePath("b/notes.txt");
        writeBytes(first);
        writeBytes(second);
        writeBytes(ignored);

        const auto nonRecursive = faceveil::scanImages({root.filePath("a")}, false);
        assert(nonRecursive.size() == 1);

        const auto recursive = faceveil::scanImages({root.filePath("a"), first}, true);
        assert(recursive.size() == 2);

        std::set<std::string> relativePaths;
        for (const auto &item: recursive)
        {
            relativePaths.insert(item.relativePath.generic_string());
        }
        assert(relativePaths.contains("one.JPG"));
        assert(relativePaths.contains("nested/two.png"));
    }

    void testApplyMosaicTouchesOnlyDetectedRegion()
    {
        cv::Mat image(8, 8, CV_8UC3);
        for (int y = 0; y < image.rows; ++y)
        {
            for (int x = 0; x < image.cols; ++x)
            {
                image.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(x * 20),
                                                      static_cast<uchar>(y * 20),
                                                      static_cast<uchar>((x + y) * 10));
            }
        }

        const cv::Vec3b outsideBefore = image.at<cv::Vec3b>(0, 0);
        const cv::Vec3b insideBefore = image.at<cv::Vec3b>(3, 3);

        faceveil::FaceDetections detections;
        detections.push_back({cv::Rect2f(2.0F, 2.0F, 4.0F, 4.0F), 1.0F});
        faceveil::applyMosaic(image, detections, 4, 0.0F);

        assert(image.at<cv::Vec3b>(0, 0) == outsideBefore);
        assert(image.at<cv::Vec3b>(3, 3) != insideBefore);
    }

    void testOrientationTransforms()
    {
        cv::Mat base(2, 3, CV_8UC1);
        for (int r = 0; r < base.rows; ++r)
        {
            for (int c = 0; c < base.cols; ++c)
            {
                base.at<uchar>(r, c) = static_cast<uchar>(r * 10 + c);
            }
        }

        cv::Mat identity = base.clone();
        faceveil::applyOrientation(identity, 1);
        assert(cv::countNonZero(identity != base) == 0);

        cv::Mat rotated = base.clone();
        faceveil::applyOrientation(rotated, 6);
        assert(rotated.rows == 3 && rotated.cols == 2);
        assert(rotated.at<uchar>(0, 0) == base.at<uchar>(base.rows - 1, 0));

        cv::Mat mirrored = base.clone();
        faceveil::applyOrientation(mirrored, 2);
        assert(mirrored.rows == 2 && mirrored.cols == 3);
        assert(mirrored.at<uchar>(0, 0) == base.at<uchar>(0, base.cols - 1));
    }

    void testEncodeParams()
    {
        const auto jpeg = faceveil::encodeParamsForExtension(".JPG");
        assert(std::find(jpeg.begin(), jpeg.end(), cv::IMWRITE_JPEG_QUALITY) != jpeg.end());
        assert(std::find(jpeg.begin(), jpeg.end(), 100) != jpeg.end());

        const auto png = faceveil::encodeParamsForExtension("png");
        assert(std::find(png.begin(), png.end(), cv::IMWRITE_PNG_COMPRESSION) != png.end());

        assert(faceveil::encodeParamsForExtension(".bmp").empty());
    }

#ifdef FACEVEIL_HAVE_EXIV2
    void testMetadataCopyAndOrientationNormalize()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path src = std::filesystem::path(temp.path().toStdString()) / "src.jpg";
        const std::filesystem::path dst = std::filesystem::path(temp.path().toStdString()) / "dst.jpg";

        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(120, 120, 120));
        assert(cv::imwrite(src.string(), img));
        assert(cv::imwrite(dst.string(), img));

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            image->exifData()["Exif.Image.Artist"] = "TestPhotographer";
            image->exifData()["Exif.Image.Orientation"] = static_cast<uint16_t>(6);
            image->writeMetadata();
        }

        assert(faceveil::readExifOrientation(src) == 6);
        assert(faceveil::copyMetadata(src, dst, true));

        {
            auto image = Exiv2::ImageFactory::open(dst.string());
            image->readMetadata();
            const Exiv2::ExifData &exif = image->exifData();

            const auto artist = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
            assert(artist != exif.end());
            assert(artist->toString() == "TestPhotographer");

            const auto orientation = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
            assert(orientation != exif.end());
            assert(orientation->toInt64() == 1);
        }
    }
#endif
}

int main()
{
    testSupportedImageExtensions();
    testScanImagesRecursesAndDeduplicates();
    testApplyMosaicTouchesOnlyDetectedRegion();
    testOrientationTransforms();
    testEncodeParams();
#ifdef FACEVEIL_HAVE_EXIV2
    testMetadataCopyAndOrientationNormalize();
#endif
    return 0;
}

#include "faceveil/ImageIo.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>

#ifdef FACEVEIL_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace faceveil
{
    bool metadataSupportAvailable()
    {
#ifdef FACEVEIL_HAVE_EXIV2
        return true;
#else
        return false;
#endif
    }

    int readExifOrientation(const std::filesystem::path &source)
    {
#ifdef FACEVEIL_HAVE_EXIV2
        try
        {
            auto image = Exiv2::ImageFactory::open(source.string());
            image->readMetadata();
            const Exiv2::ExifData &exif = image->exifData();
            const auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
            if (it != exif.end())
            {
                const auto value = static_cast<int>(it->toInt64());
                if (value >= 1 && value <= 8)
                {
                    return value;
                }
            }
        }
        catch (const Exiv2::Error &)
        {
        }
#else
        (void) source;
#endif
        return 1;
    }

    void applyOrientation(cv::Mat &image, int orientation)
    {
        switch (orientation)
        {
            case 2:
                cv::flip(image, image, 1);
                break;
            case 3:
                cv::rotate(image, image, cv::ROTATE_180);
                break;
            case 4:
                cv::flip(image, image, 0);
                break;
            case 5:
                cv::transpose(image, image);
                break;
            case 6:
                cv::rotate(image, image, cv::ROTATE_90_CLOCKWISE);
                break;
            case 7:
                cv::transpose(image, image);
                cv::rotate(image, image, cv::ROTATE_180);
                break;
            case 8:
                cv::rotate(image, image, cv::ROTATE_90_COUNTERCLOCKWISE);
                break;
            default:
                break;
        }
    }

    cv::Mat toDetectionBgr(const cv::Mat &image)
    {
        cv::Mat eightBit;
        if (image.depth() == CV_16U)
        {
            image.convertTo(eightBit, CV_8U, 1.0 / 257.0);
        }
        else if (image.depth() != CV_8U)
        {
            double minVal = 0.0;
            double maxVal = 0.0;
            cv::minMaxLoc(image.reshape(1), &minVal, &maxVal);
            const double scale = (maxVal > minVal) ? 255.0 / (maxVal - minVal) : 1.0;
            image.convertTo(eightBit, CV_8U, scale, -minVal * scale);
        }
        else
        {
            eightBit = image;
        }

        cv::Mat bgr;
        if (eightBit.channels() == 1)
        {
            cv::cvtColor(eightBit, bgr, cv::COLOR_GRAY2BGR);
        }
        else if (eightBit.channels() == 4)
        {
            cv::cvtColor(eightBit, bgr, cv::COLOR_BGRA2BGR);
        }
        else
        {
            bgr = eightBit;
        }
        return bgr;
    }

    std::vector<int> encodeParamsForExtension(const std::string &extLower)
    {
        std::string ext = extLower;
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!ext.empty() && ext.front() == '.')
        {
            ext.erase(ext.begin());
        }

        if (ext == "jpg" || ext == "jpeg")
        {
            return {cv::IMWRITE_JPEG_QUALITY, 100,
                    cv::IMWRITE_JPEG_SAMPLING_FACTOR, cv::IMWRITE_JPEG_SAMPLING_FACTOR_444,
                    cv::IMWRITE_JPEG_OPTIMIZE, 1};
        }
        if (ext == "webp")
        {
            return {cv::IMWRITE_WEBP_QUALITY, 101};
        }
        if (ext == "png")
        {
            return {cv::IMWRITE_PNG_COMPRESSION, 6};
        }
        if (ext == "tif" || ext == "tiff")
        {
            return {cv::IMWRITE_TIFF_COMPRESSION, 5};
        }
        return {};
    }

    bool copyMetadata(const std::filesystem::path &source,
                      const std::filesystem::path &destination,
                      bool normalizeOrientation)
    {
#ifdef FACEVEIL_HAVE_EXIV2
        try
        {
            auto src = Exiv2::ImageFactory::open(source.string());
            src->readMetadata();

            auto dst = Exiv2::ImageFactory::open(destination.string());
            dst->readMetadata();

            Exiv2::ExifData exif = src->exifData();
            if (normalizeOrientation && !exif.empty())
            {
                exif["Exif.Image.Orientation"] = static_cast<uint16_t>(1);
            }
            dst->setExifData(exif);
            dst->setIptcData(src->iptcData());
            dst->setXmpData(src->xmpData());

            const Exiv2::DataBuf &icc = src->iccProfile();
            if (icc.size() > 0)
            {
                dst->setIccProfile(Exiv2::DataBuf(icc.c_data(), icc.size()));
            }

            dst->writeMetadata();
            return true;
        }
        catch (const Exiv2::Error &)
        {
            return false;
        }
#else
        (void) source;
        (void) destination;
        (void) normalizeOrientation;
        return false;
#endif
    }
}

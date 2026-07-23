#include "cloakframe/ModelCatalog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace cloakframe
{
    namespace
    {
        QString dataRoot()
        {
            const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            return base.isEmpty() ? QDir::homePath() : base;
        }

        QString legacyModelCacheDir()
        {
            return dataRoot() + "/Redactly/models";
        }
    }

    QString modelCacheDir()
    {
        return dataRoot() + "/CloakFrame/models";
    }

    QString firstExistingModelPath(const QString &fileName)
    {
        const auto appDir = QCoreApplication::applicationDirPath();
        const std::array<QString, 6> candidates = {
            modelCacheDir() + "/" + fileName,
            legacyModelCacheDir() + "/" + fileName,
            appDir + "/models/" + fileName,
            appDir + "/../Resources/models/" + fileName,
            appDir + "/../../../../models/" + fileName,
            QDir::currentPath() + "/models/" + fileName,
        };

        for (const auto &candidate: candidates)
        {
            const QFileInfo info(QDir::cleanPath(candidate));
            if (info.exists() && info.isFile())
            {
                return info.absoluteFilePath();
            }
        }

        return {};
    }

    const std::array<BuiltinModel, 2> &builtinModels()
    {
        static const std::array<BuiltinModel, 2> models = {
            BuiltinModel{
                "Accurate  ·  YOLO5Face-n", "yolov5n_face.onnx",
                "https://github.com/yakhyo/yolov5-face-onnx-inference/releases/download/weights/"
                "yolov5n_face.onnx",
                "eb244a06e36999db732b317c2b30fa113cd6cfc1a397eaf738f2d6f33c01f640",
                11021477, FaceModelKind::Yolo5Face},
            BuiltinModel{
                "Fast  ·  YuNet", "face_detection_yunet_2023mar.onnx",
                "https://github.com/opencv/opencv_zoo/raw/"
                "47534e27c9851bb1128ccc0102f1145e27f23f98/models/"
                "face_detection_yunet/face_detection_yunet_2023mar.onnx",
                "8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4",
                232589, FaceModelKind::YuNet},
        };
        return models;
    }

    const BuiltinModel &plateModel()
    {
        static const BuiltinModel model{
            "License plates  ·  YOLOv9-t",
            "yolo-v9-t-512-license-plates-end2end.onnx",
            "https://github.com/ankandrew/open-image-models/releases/download/assets/"
            "yolo-v9-t-512-license-plates-end2end.onnx",
            "746fdd358ec110418775d7c9d8d07910d48b1a21471f92bf4421f6510d6daade", 7799480};
        return model;
    }

    bool modelDigestMatches(const BuiltinModel &model, const QByteArray &digest)
    {
        return digest.size() == 32 &&
               digest.toHex().compare(model.sha256.toLatin1(), Qt::CaseInsensitive) == 0;
    }
}

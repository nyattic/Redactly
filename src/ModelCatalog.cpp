#include "redactly/ModelCatalog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace redactly
{
    QString modelCacheDir()
    {
        const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const auto root = base.isEmpty() ? QDir::homePath() : base;
        return root + "/Redactly/models";
    }

    QString firstExistingModelPath(const QString &fileName)
    {
        const auto appDir = QCoreApplication::applicationDirPath();
        const std::array<QString, 5> candidates = {
            modelCacheDir() + "/" + fileName,
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
                "Fast  ·  SCRFD 2.5G", "2.5g_bnkps.onnx",
                "https://huggingface.co/RuteNL/SCRFD-face-detection-ONNX/resolve/main/2.5g_bnkps.onnx",
                "3f1ac54e769cb5fd76eda11ac3c088eed78d1f51a935a839d04d49b0e770219e", 3291737},
            BuiltinModel{
                "Accurate  ·  SCRFD 10G", "10g_bnkps.onnx",
                "https://huggingface.co/RuteNL/SCRFD-face-detection-ONNX/resolve/main/10g_bnkps.onnx",
                "5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91", 16923827},
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

    const BuiltinModel *findBuiltinModel(const QString &path)
    {
        const auto name = QFileInfo(path).fileName();
        for (const auto &model: builtinModels())
        {
            if (model.fileName == name)
            {
                return &model;
            }
        }
        return nullptr;
    }
}

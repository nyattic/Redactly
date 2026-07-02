#include "redactly/MainWindow.hpp"

#include "redactly/PlateDetector.hpp"
#include "redactly/ProcessorWorker.hpp"
#include "redactly/ReviewDialog.hpp"
#include "redactly/ScrfdFaceDetector.hpp"
#include "redactly/SettingsDialog.hpp"
#include "redactly/Theme.hpp"
#include "redactly/UpdateChecker.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QCloseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPaintEvent>
#include <QProgressDialog>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>

#include <spdlog/spdlog.h>

#include <array>

namespace redactly
{
    namespace
    {
        constexpr double kDefaultScoreThreshold = 0.5;
        constexpr double kDefaultNmsThreshold = 0.4;
        constexpr int kDefaultBlockSize = 14;
        constexpr double kDefaultPadding = 0.18;
        constexpr qint64 kMaxCustomModelBytes = 512LL * 1024LL * 1024LL;

        QString defaultOutputDirectory()
        {
            const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (!pictures.isEmpty())
            {
                return pictures + "/Redactly";
            }
            return QDir::homePath() + "/Redactly";
        }

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

        struct BuiltinModel
        {
            QString label;
            QString fileName;
            QString url;
            QString sha256;
            qint64 approxBytes;
        };

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

        bool downloadModelWithProgress(QWidget *parent, const BuiltinModel &model, const QString &destPath)
        {
            QNetworkAccessManager manager;
            QNetworkRequest request{QUrl(model.url)};
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);

            QProgressDialog progress(QCoreApplication::translate("redactly::MainWindow", "Downloading model…"),
                                     QCoreApplication::translate("redactly::MainWindow", "Cancel"), 0, 0, parent);
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            progress.setAutoClose(false);
            progress.setAutoReset(false);

            QNetworkReply *reply = manager.get(request);

            const qint64 maxBytes = std::max<qint64>(model.approxBytes * 4, 64LL * 1024 * 1024);
            bool tooLarge = false;

            QObject::connect(reply, &QNetworkReply::downloadProgress, &progress,
                             [&progress, &tooLarge, reply, maxBytes](qint64 received, qint64 total)
                             {
                                 if (received > maxBytes || (total > 0 && total > maxBytes))
                                 {
                                     tooLarge = true;
                                     reply->abort();
                                     return;
                                 }
                                 if (total > 0)
                                 {
                                     progress.setMaximum(100);
                                     progress.setValue(static_cast<int>(received * 100 / total));
                                 }
                             });

            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
            loop.exec();
            progress.close();

            if (tooLarge)
            {
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Download Failed"),
                                     QCoreApplication::translate("redactly::MainWindow",
                                                                 "The download was much larger than expected and was stopped."));
                return false;
            }

            if (reply->error() != QNetworkReply::NoError)
            {
                if (reply->error() != QNetworkReply::OperationCanceledError)
                {
                    QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Download Failed"),
                                         QCoreApplication::translate("redactly::MainWindow", "Could not download the model.\n\n%1")
                                             .arg(reply->errorString()));
                }
                return false;
            }

            const QByteArray data = reply->readAll();
            const auto actual = QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual.compare(model.sha256, Qt::CaseInsensitive) != 0)
            {
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Download Failed"),
                                     QCoreApplication::translate("redactly::MainWindow",
                                                                 "The downloaded model failed its integrity check and was discarded."));
                return false;
            }

            QDir().mkpath(QFileInfo(destPath).absolutePath());
            const QString tempPath = destPath + ".part";
            QFile file(tempPath);
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
            {
                file.remove();
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Download Failed"),
                                     QCoreApplication::translate("redactly::MainWindow", "Could not save the model file."));
                return false;
            }
            file.close();
            QFile::remove(destPath);
            if (!QFile::rename(tempPath, destPath))
            {
                QFile::remove(tempPath);
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Download Failed"),
                                     QCoreApplication::translate("redactly::MainWindow", "Could not save the model file."));
                return false;
            }
            return true;
        }

        bool ensureBuiltinModelAvailable(QWidget *parent, const BuiltinModel &model, const QString &destPath)
        {
            const auto sizeMb = QString::number(model.approxBytes / 1024.0 / 1024.0, 'f', 1);
            const auto answer = QMessageBox::question(
                parent,
                QCoreApplication::translate("redactly::MainWindow", "Download Model"),
                QCoreApplication::translate("redactly::MainWindow",
                                            "The %1 model isn't on this computer yet.\n\n"
                                            "Redactly can download it once (%2 MB) from Hugging Face. "
                                            "The model is provided by InsightFace for non-commercial use. "
                                            "Your images are never uploaded.\n\nDownload now?")
                    .arg(model.fileName, sizeMb),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (answer != QMessageBox::Yes)
            {
                return false;
            }
            return downloadModelWithProgress(parent, model, destPath);
        }

        bool ensurePlateModelAvailable(QWidget *parent, const QString &destPath)
        {
            const auto &model = plateModel();
            const auto sizeMb = QString::number(model.approxBytes / 1024.0 / 1024.0, 'f', 1);
            const auto answer = QMessageBox::question(
                parent,
                QCoreApplication::translate("redactly::MainWindow", "Download Model"),
                QCoreApplication::translate("redactly::MainWindow",
                                            "The license plate detection model isn't on this computer yet.\n\n"
                                            "Redactly can download it once (%1 MB) from the open-image-models "
                                            "project (MIT-licensed). Your images are never uploaded.\n\nDownload now?")
                    .arg(sizeMb),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (answer != QMessageBox::Yes)
            {
                return false;
            }
            return downloadModelWithProgress(parent, model, destPath);
        }

        QLabel *makeSectionTitle(QWidget *parent)
        {
            auto *label = new QLabel(parent);
            label->setProperty("role", "sectionTitle");
            return label;
        }

        QLabel *makeSectionHint(QWidget *parent)
        {
            auto *label = new QLabel(parent);
            label->setProperty("role", "sectionHint");
            label->setWordWrap(true);
            return label;
        }

        QLabel *makeFieldLabel(QWidget *parent)
        {
            auto *label = new QLabel(parent);
            label->setProperty("role", "fieldLabel");
            return label;
        }

        bool customModelFileIsAllowed(QWidget *parent, const QString &path)
        {
            const QFileInfo info(path);
            if (!info.exists() || !info.isFile())
            {
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Invalid Model"),
                                     QCoreApplication::translate("redactly::MainWindow", "Choose an existing ONNX model file."));
                return false;
            }
            if (info.suffix().compare("onnx", Qt::CaseInsensitive) != 0)
            {
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Invalid Model"),
                                     QCoreApplication::translate("redactly::MainWindow", "The selected model must use the .onnx extension."));
                return false;
            }
            if (info.size() > kMaxCustomModelBytes)
            {
                QMessageBox::warning(parent, QCoreApplication::translate("redactly::MainWindow", "Model Too Large"),
                                     QCoreApplication::translate("redactly::MainWindow",
                                                                 "The selected ONNX file is larger than 512 MB. "
                                                                 "Choose a smaller SCRFD model."));
                return false;
            }
            return true;
        }

        bool confirmTrustedCustomModel(QWidget *parent, const QString &path)
        {
            const QFileInfo info(path);
            const auto answer = QMessageBox::question(
                parent,
                QCoreApplication::translate("redactly::MainWindow", "Load Custom Model"),
                QCoreApplication::translate("redactly::MainWindow",
                                            "Only load ONNX models from sources you trust.\n\nModel: %1\nSize: %2 MB\n\nContinue?")
                    .arg(info.fileName())
                    .arg(QString::number(info.size() / 1024.0 / 1024.0, 'f', 1)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            return answer == QMessageBox::Yes;
        }

        QFrame *makeCard(QWidget *parent)
        {
            auto *card = new QFrame(parent);
            card->setObjectName("card");
            card->setFrameShape(QFrame::NoFrame);
            return card;
        }

        class DropListWidget final : public QListWidget
        {
        public:
            using QListWidget::QListWidget;

            void setPlaceholderText(const QString &text)
            {
                placeholder_ = text;
                viewport()->update();
            }

        protected:
            void paintEvent(QPaintEvent *event) override
            {
                QListWidget::paintEvent(event);
                if (count() != 0 || placeholder_.isEmpty())
                {
                    return;
                }
                QPainter painter(viewport());
                painter.setPen(QColor("#9CA3AF"));
                painter.drawText(viewport()->rect().adjusted(24, 0, -24, 0),
                                 Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
            }

        private:
            QString placeholder_;
        };
    }

    MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    {
        setWindowTitle("Redactly");
        setAcceptDrops(true);
        resize(920, 760);
        setMinimumSize(720, 600);

        auto *container = new QWidget(this);
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        auto *scrollArea = new QScrollArea(container);
        scrollArea->setObjectName("rootScroll");
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setFrameShape(QFrame::NoFrame);

        auto *central = new QWidget(scrollArea);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(32, 28, 32, 28);
        root->setSpacing(20);

        auto *header = new QWidget(central);
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(4);

        auto *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(8);
        auto *title = new QLabel("Redactly", header);
        title->setObjectName("titleLabel");

        settingsButton_ = new QToolButton(header);
        settingsButton_->setObjectName("settingsButton");
        settingsButton_->setText(QStringLiteral("⚙"));
        settingsButton_->setFocusPolicy(Qt::NoFocus);
        settingsButton_->setCursor(Qt::PointingHandCursor);
        addRetranslation([this]{ settingsButton_->setToolTip(tr("Settings")); });
        connect(settingsButton_, &QToolButton::clicked, this, &MainWindow::openSettings);

        auto *versionLabel = new QLabel(QString("v%1").arg(QCoreApplication::applicationVersion()), header);
        versionLabel->setObjectName("subtitleLabel");

        updateLabel_ = new QLabel(header);
        updateLabel_->setObjectName("subtitleLabel");
        updateLabel_->setTextFormat(Qt::RichText);
        updateLabel_->setOpenExternalLinks(true);
        updateLabel_->setVisible(false);

        auto *titleGroup = new QWidget(header);
        auto *titleGroupLayout = new QHBoxLayout(titleGroup);
        titleGroupLayout->setContentsMargins(0, 0, 0, 0);
        titleGroupLayout->setSpacing(8);
        titleGroupLayout->addWidget(title, 0, Qt::AlignBottom);
        titleGroupLayout->addWidget(versionLabel, 0, Qt::AlignBottom);
        titleGroupLayout->addWidget(updateLabel_, 0, Qt::AlignBottom);

        titleRow->addWidget(titleGroup, 0, Qt::AlignVCenter);
        titleRow->addStretch(1);
        titleRow->addWidget(settingsButton_, 0, Qt::AlignVCenter);

        auto *subtitle = new QLabel(header);
        subtitle->setObjectName("subtitleLabel");
        addRetranslation([this, subtitle]{ subtitle->setText(tr("Local, private face anonymization for photos")); });
        headerLayout->addLayout(titleRow);
        headerLayout->addWidget(subtitle);
        root->addWidget(header);

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            auto *modelTitle = makeSectionTitle(card);
            cardLayout->addWidget(modelTitle);
            addRetranslation([this, modelTitle]{ modelTitle->setText(tr("Model")); });

            auto *modelHint = makeSectionHint(card);
            cardLayout->addWidget(modelHint);
            addRetranslation([this, modelHint]
                             { modelHint->setText(tr("Choose speed vs. accuracy, or load a custom SCRFD ONNX file.")); });

            detectCombo_ = new QComboBox(card);
            detectCombo_->setMinimumHeight(34);
            detectCombo_->addItem(QString(), "faces");
            detectCombo_->addItem(QString(), "plates");
            detectCombo_->addItem(QString(), "both");
            addRetranslation([this]
                             {
                                 detectCombo_->setItemText(0, tr("Faces"));
                                 detectCombo_->setItemText(1, tr("License plates"));
                                 detectCombo_->setItemText(2, tr("Faces + license plates"));
                             });
            cardLayout->addWidget(detectCombo_);

            modelCombo_ = new QComboBox(card);
            modelCombo_->setMinimumHeight(34);
            cardLayout->addWidget(modelCombo_);
            connect(detectCombo_, &QComboBox::currentIndexChanged, this, [this]
            {
                const bool facesNeeded = detectCombo_->currentData().toString() != "plates";
                modelCombo_->setEnabled(facesNeeded);
                modelPathEdit_->setEnabled(facesNeeded);
            });

            auto *pathRow = new QHBoxLayout();
            pathRow->setSpacing(8);
            modelPathEdit_ = new QLineEdit(card);
            modelPathEdit_->setReadOnly(true);
            addRetranslation([this]{ modelPathEdit_->setPlaceholderText(tr("Bundled SCRFD model path")); });
            downloadButton_ = new QPushButton(card);
            downloadButton_->setObjectName("primaryButton");
            addRetranslation([this]{ downloadButton_->setText(tr("Download")); });
            downloadButton_->setCursor(Qt::PointingHandCursor);
            downloadButton_->setVisible(false);
            auto *modelButton = new QPushButton(card);
            addRetranslation([this, modelButton]{ modelButton->setText(tr("Browse…")); });
            modelButton->setCursor(Qt::PointingHandCursor);
            pathRow->addWidget(modelPathEdit_, 1);
            pathRow->addWidget(downloadButton_);
            pathRow->addWidget(modelButton);
            cardLayout->addLayout(pathRow);

            connect(downloadButton_, &QPushButton::clicked, this, &MainWindow::downloadSelectedModel);
            connect(modelButton, &QPushButton::clicked, this, &MainWindow::chooseModel);
            connect(modelCombo_, &QComboBox::currentIndexChanged,
                    this, &MainWindow::updateModelPathFromSelection);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            auto *inputsTitle = makeSectionTitle(card);
            cardLayout->addWidget(inputsTitle);
            addRetranslation([this, inputsTitle]{ inputsTitle->setText(tr("Inputs")); });

            auto *inputsHint = makeSectionHint(card);
            cardLayout->addWidget(inputsHint);
            addRetranslation([this, inputsHint]
                             { inputsHint->setText(tr("Drag images or folders here, or use the buttons below.")); });

            auto *dropList = new DropListWidget(card);
            inputList_ = dropList;
            inputList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
            inputList_->setMinimumHeight(140);
            inputList_->setAlternatingRowColors(false);
            addRetranslation([dropList]
                             { dropList->setPlaceholderText(tr("Drop images or folders here")); });
            cardLayout->addWidget(inputList_);

            auto *buttonRow = new QHBoxLayout();
            buttonRow->setSpacing(8);
            auto *addFiles = new QPushButton(card);
            addRetranslation([this, addFiles]{ addFiles->setText(tr("Add Images")); });
            auto *addFolder = new QPushButton(card);
            addRetranslation([this, addFolder]{ addFolder->setText(tr("Add Folder")); });
            auto *clearInputs = new QPushButton(card);
            addRetranslation([this, clearInputs]{ clearInputs->setText(tr("Clear")); });
            addFiles->setCursor(Qt::PointingHandCursor);
            addFolder->setCursor(Qt::PointingHandCursor);
            clearInputs->setCursor(Qt::PointingHandCursor);
            recursiveCheck_ = new QCheckBox(card);
            addRetranslation([this]{ recursiveCheck_->setText(tr("Include subfolders")); });
            recursiveCheck_->setChecked(true);
            reviewCheck_ = new QCheckBox(card);
            addRetranslation([this]{ reviewCheck_->setText(tr("Review each image")); });
            reviewCheck_->setChecked(false);
            addRetranslation([this]
                             {
                                 reviewCheck_->setToolTip(tr(
                                     "Before saving each image, open a preview where you can:\n"
                                     "  • Click a detected box to exclude it\n"
                                     "  • Drag an empty area to add a box the model missed"));
                             });

            connect(addFiles, &QPushButton::clicked, this, &MainWindow::chooseFiles);
            connect(addFolder, &QPushButton::clicked, this, &MainWindow::chooseFolder);
            connect(clearInputs, &QPushButton::clicked, inputList_, &QListWidget::clear);

            buttonRow->addWidget(addFiles);
            buttonRow->addWidget(addFolder);
            buttonRow->addWidget(clearInputs);
            buttonRow->addStretch(1);
            buttonRow->addWidget(reviewCheck_);
            buttonRow->addWidget(recursiveCheck_);
            cardLayout->addLayout(buttonRow);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            auto *outputTitle = makeSectionTitle(card);
            cardLayout->addWidget(outputTitle);
            addRetranslation([this, outputTitle]{ outputTitle->setText(tr("Output")); });

            auto *outputHint = makeSectionHint(card);
            cardLayout->addWidget(outputHint);
            addRetranslation([this, outputHint]
                             { outputHint->setText(tr("Anonymized copies are written here, preserving folder structure.")); });

            auto *outputRow = new QHBoxLayout();
            outputRow->setSpacing(8);
            outputDirEdit_ = new QLineEdit(defaultOutputDirectory(), card);
            auto *outputButton = new QPushButton(card);
            addRetranslation([this, outputButton]{ outputButton->setText(tr("Choose…")); });
            outputButton->setCursor(Qt::PointingHandCursor);
            outputRow->addWidget(outputDirEdit_, 1);
            outputRow->addWidget(outputButton);
            cardLayout->addLayout(outputRow);
            connect(outputButton, &QPushButton::clicked, this, &MainWindow::chooseOutputDirectory);

            preserveMetaCheck_ = new QCheckBox(card);
            addRetranslation([this]
                             { preserveMetaCheck_->setText(tr("Preserve original metadata (EXIF, location, color profile)")); });
            preserveMetaCheck_->setChecked(false);
            addRetranslation([this]
                             {
                                 preserveMetaCheck_->setToolTip(tr(
                                     "Off (default): output carries no metadata — GPS, camera, and timestamps are removed.\n"
                                     "On: copies EXIF/IPTC/XMP and the ICC color profile from the original, and preserves "
                                     "format and bit depth at maximum quality. Best for archiving high-resolution photos."));
                             });
            cardLayout->addWidget(preserveMetaCheck_);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 14, 20, 14);
            cardLayout->setSpacing(0);

            auto *headerRow = new QHBoxLayout();
            headerRow->setSpacing(8);
            headerRow->setContentsMargins(0, 0, 0, 0);

            advancedToggle_ = new QToolButton(card);
            advancedToggle_->setObjectName("advancedToggle");
            addRetranslation([this]{ advancedToggle_->setText(tr("Advanced Options")); });
            advancedToggle_->setCheckable(true);
            advancedToggle_->setChecked(false);
            advancedToggle_->setArrowType(Qt::RightArrow);
            advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            advancedToggle_->setCursor(Qt::PointingHandCursor);
            advancedToggle_->setFocusPolicy(Qt::NoFocus);

            auto *resetButton = new QPushButton(card);
            addRetranslation([this, resetButton]{ resetButton->setText(tr("Reset to defaults")); });
            resetButton->setCursor(Qt::PointingHandCursor);

            headerRow->addWidget(advancedToggle_);
            headerRow->addStretch(1);
            headerRow->addWidget(resetButton);
            cardLayout->addLayout(headerRow);

            advancedBody_ = new QWidget(card);
            auto *bodyLayout = new QVBoxLayout(advancedBody_);
            bodyLayout->setContentsMargins(0, 14, 0, 4);
            bodyLayout->setSpacing(12);

            auto *advancedHint = makeSectionHint(advancedBody_);
            bodyLayout->addWidget(advancedHint);
            addRetranslation([this, advancedHint]
                             { advancedHint->setText(tr("Tweak detection and mosaic behavior. Defaults work for most photos.")); });

            methodCombo_ = new QComboBox(advancedBody_);
            methodCombo_->addItem(QString(), static_cast<int>(AnonymizationMethod::Mosaic));
            methodCombo_->addItem(QString(), static_cast<int>(AnonymizationMethod::Blur));
            methodCombo_->addItem(QString(), static_cast<int>(AnonymizationMethod::Fill));
            addRetranslation([this]
                             {
                                 methodCombo_->setItemText(0, tr("Mosaic (pixelate)"));
                                 methodCombo_->setItemText(1, tr("Gaussian blur"));
                                 methodCombo_->setItemText(2, tr("Solid fill (blackout)"));
                             });
            addRetranslation([this]
                             {
                                 methodCombo_->setToolTip(tr(
                                     "How detected faces are obscured.\n"
                                     "Mosaic = pixelation (block size below).\n"
                                     "Gaussian blur = strong smoothing scaled to face size.\n"
                                     "Solid fill = opaque black box, irreversible. Default: Mosaic"));
                             });

            shapeCombo_ = new QComboBox(advancedBody_);
            shapeCombo_->addItem(QString(), static_cast<int>(MaskShape::Rectangle));
            shapeCombo_->addItem(QString(), static_cast<int>(MaskShape::Ellipse));
            addRetranslation([this]
                             {
                                 shapeCombo_->setItemText(0, tr("Rectangle"));
                                 shapeCombo_->setItemText(1, tr("Rounded (ellipse)"));
                             });
            addRetranslation([this]
                             {
                                 shapeCombo_->setToolTip(tr(
                                     "Shape of the obscured region.\n"
                                     "Rectangle = full padded box.\n"
                                     "Rounded = elliptical mask that follows the face and leaves corners untouched. "
                                     "Default: Rectangle"));
                             });

            scoreThresholdSpin_ = new QDoubleSpinBox(advancedBody_);
            scoreThresholdSpin_->setRange(0.05, 0.99);
            scoreThresholdSpin_->setSingleStep(0.05);
            scoreThresholdSpin_->setDecimals(2);
            scoreThresholdSpin_->setValue(kDefaultScoreThreshold);
            addRetranslation([this]
                             {
                                 scoreThresholdSpin_->setToolTip(tr(
                                     "Minimum confidence to accept a face.\n"
                                     "Higher = fewer false positives but may miss small or side-profile faces.\n"
                                     "Lower = catches more faces but may blur non-face regions. Default: 0.50"));
                             });

            nmsThresholdSpin_ = new QDoubleSpinBox(advancedBody_);
            nmsThresholdSpin_->setRange(0.05, 0.95);
            nmsThresholdSpin_->setSingleStep(0.05);
            nmsThresholdSpin_->setDecimals(2);
            nmsThresholdSpin_->setValue(kDefaultNmsThreshold);
            addRetranslation([this]
                             {
                                 nmsThresholdSpin_->setToolTip(tr(
                                     "Non-Maximum Suppression overlap threshold for duplicate boxes.\n"
                                     "Lower = more aggressively removes overlapping detections.\n"
                                     "Higher = allows more overlap. Default: 0.40"));
                             });

            blockSizeSpin_ = new QSpinBox(advancedBody_);
            blockSizeSpin_->setRange(2, 200);
            blockSizeSpin_->setValue(kDefaultBlockSize);
            addRetranslation([this]
                             {
                                 blockSizeSpin_->setToolTip(tr(
                                     "Mosaic block size in pixels.\n"
                                     "Larger = coarser blocks, harder to un-blur.\n"
                                     "Smaller = finer mosaic, higher recovery risk. Default: 14"));
                             });

            paddingSpin_ = new QDoubleSpinBox(advancedBody_);
            paddingSpin_->setRange(0.0, 1.0);
            paddingSpin_->setSingleStep(0.05);
            paddingSpin_->setDecimals(2);
            paddingSpin_->setValue(kDefaultPadding);
            addRetranslation([this]
                             {
                                 paddingSpin_->setToolTip(tr(
                                     "Extra margin around each detected face, as a fraction of its size.\n"
                                     "Covers ears, hairline, and chin that the detector may miss.\n"
                                     "0.00 = exact box, 0.18 = ~18% larger. Default: 0.18"));
                             });

            auto *grid = new QFormLayout();
            grid->setLabelAlignment(Qt::AlignLeft);
            grid->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
            grid->setHorizontalSpacing(18);
            grid->setVerticalSpacing(10);
            auto *methodLabel = makeFieldLabel(advancedBody_);
            addRetranslation([this, methodLabel]{ methodLabel->setText(tr("Anonymization")); });
            grid->addRow(methodLabel, methodCombo_);
            auto *shapeLabel = makeFieldLabel(advancedBody_);
            addRetranslation([this, shapeLabel]{ shapeLabel->setText(tr("Shape")); });
            grid->addRow(shapeLabel, shapeCombo_);
            auto *scoreLabel = makeFieldLabel(advancedBody_);
            addRetranslation([this, scoreLabel]{ scoreLabel->setText(tr("Score threshold")); });
            grid->addRow(scoreLabel, scoreThresholdSpin_);
            auto *nmsLabel = makeFieldLabel(advancedBody_);
            addRetranslation([this, nmsLabel]{ nmsLabel->setText(tr("NMS threshold")); });
            grid->addRow(nmsLabel, nmsThresholdSpin_);
            auto *blockLabel = makeFieldLabel(advancedBody_);
            addRetranslation([this, blockLabel]{ blockLabel->setText(tr("Mosaic block size")); });
            grid->addRow(blockLabel, blockSizeSpin_);
            auto *paddingLabel = makeFieldLabel(advancedBody_);
            addRetranslation([this, paddingLabel]{ paddingLabel->setText(tr("Face padding")); });
            grid->addRow(paddingLabel, paddingSpin_);
            bodyLayout->addLayout(grid);

            advancedBody_->setVisible(false);
            cardLayout->addWidget(advancedBody_);

            connect(advancedToggle_, &QToolButton::toggled, this, &MainWindow::toggleAdvanced);
            connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetAdvancedDefaults);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(10);

            auto *activityTitle = makeSectionTitle(card);
            cardLayout->addWidget(activityTitle);
            addRetranslation([this, activityTitle]{ activityTitle->setText(tr("Activity")); });
            logEdit_ = new QPlainTextEdit(card);
            logEdit_->setReadOnly(true);
            logEdit_->setMinimumHeight(140);

            logEdit_->setMaximumBlockCount(5000);
            cardLayout->addWidget(logEdit_);
            root->addWidget(card);
        }

        scrollArea->setWidget(central);
        containerLayout->addWidget(scrollArea, 1);

        auto *bottomBar = new QWidget(container);
        bottomBar->setObjectName("bottomBar");
        bottomBar->setAutoFillBackground(true);
        auto *bottomLayout = new QHBoxLayout(bottomBar);
        bottomLayout->setContentsMargins(24, 14, 24, 14);
        bottomLayout->setSpacing(14);

        statusLabel_ = new QLabel(bottomBar);
        statusLabel_->setObjectName("statusLabel");
        addRetranslation([this]{ statusLabel_->setText(tr("Ready")); });

        progressBar_ = new QProgressBar(bottomBar);
        progressBar_->setRange(0, 100);
        progressBar_->setValue(0);
        progressBar_->setTextVisible(false);
        progressBar_->setFixedHeight(6);

        stopButton_ = new QPushButton(bottomBar);
        stopButton_->setObjectName("dangerButton");
        addRetranslation([this]{ stopButton_->setText(tr("Stop")); });
        stopButton_->setCursor(Qt::PointingHandCursor);
        stopButton_->setEnabled(false);

        startButton_ = new QPushButton(bottomBar);
        startButton_->setObjectName("primaryButton");
        addRetranslation([this]{ startButton_->setText(tr("Start")); });
        startButton_->setCursor(Qt::PointingHandCursor);
        startButton_->setDefault(true);

        connect(startButton_, &QPushButton::clicked, this, &MainWindow::startProcessing);
        connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopProcessing);

        bottomLayout->addWidget(statusLabel_);
        bottomLayout->addWidget(progressBar_, 1);
        bottomLayout->addWidget(stopButton_);
        bottomLayout->addWidget(startButton_);

        containerLayout->addWidget(bottomBar);

        setCentralWidget(container);
        statusBar()->hide();

        populateBundledModels();
        loadSettings();
        appendLog(tr("Ready. Drop images or folders to begin."));

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme)
        {
            if (themeMode_ == ThemeMode::System)
            {
                applyTheme(*qApp, ThemeMode::System);
            }
        });
#endif

        checkForUpdates();
    }

    MainWindow::~MainWindow()
    {
        if (workerThread_ != nullptr)
        {
            stopProcessing();
            workerThread_->quit();
            workerThread_->wait();
        }
    }

    namespace
    {
        bool hasAcceptableLocalUrls(const QMimeData *mime)
        {
            if (mime == nullptr || !mime->hasUrls())
            {
                return false;
            }
            for (const auto &url: mime->urls())
            {
                if (!url.isLocalFile())
                {
                    continue;
                }
                const QFileInfo info(url.toLocalFile());
                if (info.exists() && (info.isFile() || info.isDir()))
                {
                    return true;
                }
            }
            return false;
        }
    }

    void MainWindow::dragEnterEvent(QDragEnterEvent *event)
    {
        if (hasAcceptableLocalUrls(event->mimeData()))
        {
            event->acceptProposedAction();
        } else
        {
            event->ignore();
        }
    }

    void MainWindow::dropEvent(QDropEvent *event)
    {
        if (!hasAcceptableLocalUrls(event->mimeData()))
        {
            event->ignore();
            return;
        }
        for (const auto &url: event->mimeData()->urls())
        {
            if (!url.isLocalFile())
            {
                continue;
            }
            const QFileInfo info(url.toLocalFile());
            if (info.exists() && (info.isFile() || info.isDir()))
            {
                addInputPath(url.toLocalFile());
            }
        }
        event->acceptProposedAction();
    }

    void MainWindow::chooseModel()
    {
        const auto path = QFileDialog::getOpenFileName(this, tr("Select SCRFD ONNX Model"), QDir::currentPath(),
                                                       tr("ONNX Models (*.onnx)"));
        if (!path.isEmpty())
        {
            if (!customModelFileIsAllowed(this, path) || !confirmTrustedCustomModel(this, path))
            {
                return;
            }
            const QFileInfo info(path);
            modelCombo_->addItem(tr("Custom — %1").arg(info.fileName()), info.absoluteFilePath());
            modelCombo_->setCurrentIndex(modelCombo_->count() - 1);
            modelPathEdit_->setText(path);
        }
    }

    void MainWindow::chooseFiles()
    {
        const auto files = QFileDialog::getOpenFileNames(this,
                                                         tr("Select Images"),
                                                         QStandardPaths::writableLocation(
                                                             QStandardPaths::PicturesLocation),
                                                         tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp)"));
        for (const auto &file: files)
        {
            addInputPath(file);
        }
    }

    void MainWindow::chooseFolder()
    {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("Select Folder"),
                                                              QStandardPaths::writableLocation(
                                                                  QStandardPaths::PicturesLocation));
        if (!folder.isEmpty())
        {
            addInputPath(folder);
        }
    }

    void MainWindow::chooseOutputDirectory()
    {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"), outputDirEdit_->text());
        if (!folder.isEmpty())
        {
            outputDirEdit_->setText(folder);
        }
    }

    void MainWindow::startProcessing()
    {
        const QString detectTarget = detectCombo_ ? detectCombo_->currentData().toString()
                                                   : QStringLiteral("faces");
        const bool detectFaces = detectTarget != "plates";
        const bool detectPlates = detectTarget != "faces";

        const auto modelPath = selectedModelPath();
        const bool isCustom = !modelPath.isEmpty() && findBuiltinModel(modelPath) == nullptr;

        if (detectFaces)
        {
            if (modelPath.isEmpty())
            {
                reportValidationIssue(tr("Choose a SCRFD ONNX model first."), modelCombo_);
                return;
            }

            if (!QFileInfo::exists(modelPath))
            {
                const BuiltinModel *builtin = isCustom ? nullptr : findBuiltinModel(modelPath);
                if (builtin == nullptr)
                {
                    appendLog(tr("Choose a valid SCRFD ONNX model first."));
                    return;
                }
                appendLog(tr("Downloading %1…").arg(builtin->fileName));
                if (!ensureBuiltinModelAvailable(this, *builtin, modelPath))
                {
                    appendLog(tr("Model download was cancelled or failed."));
                    return;
                }
                updateModelPathFromSelection();
                appendLog(tr("Model ready: %1").arg(builtin->fileName));
            }

            if (isCustom && !customModelFileIsAllowed(this, modelPath))
            {
                return;
            }
        }

        QString plateModelPath;
        if (detectPlates)
        {
            plateModelPath = firstExistingModelPath(plateModel().fileName);
            if (plateModelPath.isEmpty())
            {
                plateModelPath = modelCacheDir() + "/" + plateModel().fileName;
                appendLog(tr("Downloading %1…").arg(plateModel().fileName));
                if (!ensurePlateModelAvailable(this, plateModelPath))
                {
                    appendLog(tr("Model download was cancelled or failed."));
                    return;
                }
                appendLog(tr("Model ready: %1").arg(plateModel().fileName));
            }
        }

        if (inputList_->count() == 0)
        {
            reportValidationIssue(tr("Add at least one image or folder."), inputList_);
            return;
        }

        if (outputDirEdit_->text().isEmpty())
        {
            reportValidationIssue(tr("Choose an output folder."), outputDirEdit_);
            return;
        }

        const QString rawOutput = outputDirEdit_->text();
        const QFileInfo outputInfo(rawOutput);
        const QString canonicalOutput = outputInfo.exists()
                                            ? outputInfo.canonicalFilePath()
                                            : QDir::cleanPath(rawOutput);
        for (const auto &input: inputPaths())
        {
            const QFileInfo inputInfo(input);
            const QString canonicalInput = inputInfo.exists()
                                               ? inputInfo.canonicalFilePath()
                                               : QDir::cleanPath(input);
            if (canonicalInput.isEmpty() || canonicalOutput.isEmpty())
            {
                continue;
            }

            const bool isSame = canonicalInput.compare(canonicalOutput, Qt::CaseInsensitive) == 0;
            const QString withSep = canonicalInput.endsWith('/') ? canonicalInput : canonicalInput + '/';
            const bool isUnder = canonicalOutput.startsWith(withSep, Qt::CaseInsensitive);
            if (isSame || isUnder)
            {
                reportValidationIssue(tr("Refusing to run: output folder is inside input '%1'. "
                                         "Pick a different output folder so originals aren't overwritten.")
                                          .arg(input),
                                      outputDirEdit_);
                return;
            }
        }

        setProcessing(true);
        progressBar_->setValue(0);
        statusLabel_->setText(tr("Starting…"));

        auto detectorForRun = (cachedDetectorModelPath_ == modelPath) ? cachedDetector_ : nullptr;
        auto plateForRun = (!plateModelPath.isEmpty() && cachedPlateModelPath_ == plateModelPath)
                               ? cachedPlateDetector_
                               : nullptr;

        workerThread_ = new QThread(this);
        worker_ = new ProcessorWorker(modelPath,
                                      inputPaths(),
                                      outputDirEdit_->text(),
                                      recursiveCheck_->isChecked(),
                                      static_cast<float>(scoreThresholdSpin_->value()),
                                      static_cast<float>(nmsThresholdSpin_->value()),
                                      blockSizeSpin_->value(),
                                      static_cast<float>(paddingSpin_->value()),
                                      static_cast<AnonymizationMethod>(methodCombo_->currentData().toInt()),
                                      static_cast<MaskShape>(shapeCombo_->currentData().toInt()),
                                      preserveMetaCheck_->isChecked(),
                                      reviewCheck_->isChecked(),
                                      this,
                                      std::move(detectorForRun),
                                      detectFaces,
                                      detectPlates,
                                      plateModelPath,
                                      std::move(plateForRun));

        worker_->moveToThread(workerThread_);
        connect(workerThread_, &QThread::started, worker_, &ProcessorWorker::process);
        connect(worker_, &ProcessorWorker::logMessage, this, &MainWindow::appendLog);
        connect(worker_, &ProcessorWorker::progressChanged, this, [this](int completed, int total)
        {
            progressBar_->setRange(0, std::max(total, 1));
            progressBar_->setValue(completed);
        });
        connect(worker_, &ProcessorWorker::stageChanged, this,
                [this](int index, int total, const QString &stage, const QString &fileName)
                {
                    statusLabel_->setText(QString("%1/%2  ·  %3  ·  %4")
                        .arg(index).arg(total).arg(stage, fileName));
                });

        connect(worker_, &ProcessorWorker::finished, this, &MainWindow::onWorkerFinished);
        connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);

        appendLog(tr("Starting…"));
        workerThread_->start();
    }

    void MainWindow::stopProcessing() const
    {
        if (worker_ != nullptr)
        {
            appendLog(tr("Stopping after the current processing step…"));
            statusLabel_->setText(tr("Stopping…"));
            QMetaObject::invokeMethod(worker_, "cancel", Qt::QueuedConnection);
        }
    }

    void MainWindow::onWorkerFinished(bool cancelled)
    {
        appendLog(cancelled ? tr("Cancelled.") : tr("Finished."));
        statusLabel_->setText(cancelled ? tr("Cancelled") : tr("Done"));
        setProcessing(false);

        if (worker_ != nullptr)
        {
            auto detector = worker_->takeDetector();
            if (detector)
            {
                cachedDetector_ = std::move(detector);
                cachedDetectorModelPath_ = selectedModelPath();
            }

            auto plate = worker_->takePlateDetector();
            if (plate)
            {
                cachedPlateDetector_ = std::move(plate);
                cachedPlateModelPath_ = firstExistingModelPath(plateModel().fileName);
            }
        }

        if (workerThread_ != nullptr)
        {
            workerThread_->quit();
        }

        worker_ = nullptr;
        workerThread_ = nullptr;
    }

    void MainWindow::toggleAdvanced(bool expanded) const
    {
        if (advancedBody_ != nullptr)
        {
            advancedBody_->setVisible(expanded);
        }
        if (advancedToggle_ != nullptr)
        {
            advancedToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        }
    }

    void MainWindow::resetAdvancedDefaults() const
    {
        scoreThresholdSpin_->setValue(kDefaultScoreThreshold);
        nmsThresholdSpin_->setValue(kDefaultNmsThreshold);
        blockSizeSpin_->setValue(kDefaultBlockSize);
        paddingSpin_->setValue(kDefaultPadding);
        methodCombo_->setCurrentIndex(0);
        shapeCombo_->setCurrentIndex(0);
    }

    void MainWindow::closeEvent(QCloseEvent *event)
    {
        saveSettings();
        QMainWindow::closeEvent(event);
    }

    void MainWindow::loadSettings()
    {
        QSettings settings;

        settings.beginGroup("window");
        const auto geometry = settings.value("geometry").toByteArray();
        if (!geometry.isEmpty())
        {
            restoreGeometry(geometry);
        }
        settings.endGroup();

        settings.beginGroup("processing");
        const auto savedModelIndex = settings.value("modelIndex", 0).toInt();
        if (modelCombo_ != nullptr && savedModelIndex >= 0 && savedModelIndex < modelCombo_->count())
        {
            modelCombo_->setCurrentIndex(savedModelIndex);
        }
        const auto savedCustomModel = settings.value("customModelPath").toString();
        if (!savedCustomModel.isEmpty() && QFileInfo::exists(savedCustomModel))
        {
            const QFileInfo info(savedCustomModel);
            modelCombo_->addItem(tr("Custom — %1").arg(info.fileName()), info.absoluteFilePath());
            modelCombo_->setCurrentIndex(modelCombo_->count() - 1);
        }

        const auto outputDir = settings.value("outputDir").toString();
        if (!outputDir.isEmpty())
        {
            outputDirEdit_->setText(outputDir);
        }

        recursiveCheck_->setChecked(settings.value("recursive", true).toBool());
        reviewCheck_->setChecked(settings.value("review", false).toBool());
        preserveMetaCheck_->setChecked(settings.value("preserveMetadata", false).toBool());

        scoreThresholdSpin_->setValue(settings.value("scoreThreshold", kDefaultScoreThreshold).toDouble());
        nmsThresholdSpin_->setValue(settings.value("nmsThreshold", kDefaultNmsThreshold).toDouble());
        blockSizeSpin_->setValue(settings.value("blockSize", kDefaultBlockSize).toInt());
        paddingSpin_->setValue(settings.value("padding", kDefaultPadding).toDouble());

        const auto savedMethod = settings.value("method", 0).toInt();
        if (methodCombo_ != nullptr && savedMethod >= 0 && savedMethod < methodCombo_->count())
        {
            methodCombo_->setCurrentIndex(savedMethod);
        }

        const auto savedShape = settings.value("shape", 0).toInt();
        if (shapeCombo_ != nullptr && savedShape >= 0 && savedShape < shapeCombo_->count())
        {
            shapeCombo_->setCurrentIndex(savedShape);
        }

        const auto savedDetect = settings.value("detectTarget", 0).toInt();
        if (detectCombo_ != nullptr && savedDetect >= 0 && savedDetect < detectCombo_->count())
        {
            detectCombo_->setCurrentIndex(savedDetect);
        }

        if (settings.value("advancedExpanded", false).toBool())
        {
            advancedToggle_->setChecked(true);
        }
        settings.endGroup();

        themeMode_ = themeModeFromString(settings.value("theme", "system").toString());
        checkForUpdatesOnStartup_ = settings.value("checkForUpdates", true).toBool();

        const auto savedLanguage = settings.value("language").toString();
        QString language = savedLanguage;
        if (language.isEmpty())
        {
            language = (QLocale::system().language() == QLocale::Korean) ? QStringLiteral("ko") : QStringLiteral("en");
        }
        applyLanguage(language);

        updateModelPathFromSelection();
    }

    void MainWindow::saveSettings() const
    {
        QSettings settings;

        settings.beginGroup("window");
        settings.setValue("geometry", saveGeometry());
        settings.endGroup();

        settings.beginGroup("processing");
        settings.setValue("modelIndex", modelCombo_ ? modelCombo_->currentIndex() : 0);

        const auto currentModel = selectedModelPath();
        if (!currentModel.isEmpty() && findBuiltinModel(currentModel) == nullptr)
        {
            settings.setValue("customModelPath", currentModel);
        } else
        {
            settings.remove("customModelPath");
        }

        settings.setValue("outputDir", outputDirEdit_->text());
        settings.setValue("recursive", recursiveCheck_->isChecked());
        settings.setValue("review", reviewCheck_->isChecked());
        settings.setValue("preserveMetadata", preserveMetaCheck_->isChecked());
        settings.setValue("scoreThreshold", scoreThresholdSpin_->value());
        settings.setValue("nmsThreshold", nmsThresholdSpin_->value());
        settings.setValue("blockSize", blockSizeSpin_->value());
        settings.setValue("padding", paddingSpin_->value());
        settings.setValue("method", methodCombo_ ? methodCombo_->currentIndex() : 0);
        settings.setValue("shape", shapeCombo_ ? shapeCombo_->currentIndex() : 0);
        settings.setValue("detectTarget", detectCombo_ ? detectCombo_->currentIndex() : 0);
        settings.setValue("advancedExpanded", advancedToggle_ ? advancedToggle_->isChecked() : false);
        settings.endGroup();

        settings.setValue("theme", themeModeToString(themeMode_));
        settings.setValue("checkForUpdates", checkForUpdatesOnStartup_);

        settings.setValue("language", language_);
    }

    void MainWindow::checkForUpdates()
    {
        if (!checkForUpdatesOnStartup_)
        {
            return;
        }

        auto *checker = new UpdateChecker(QCoreApplication::applicationVersion(), this);
        connect(checker, &UpdateChecker::updateAvailable, this,
                [this](const QString &latestVersion, const QString &releaseUrl)
                {
                    if (updateLabel_ == nullptr)
                    {
                        return;
                    }
                    const QString text = tr("Update available: %1").arg(latestVersion);
                    updateLabel_->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                        .arg(releaseUrl.toHtmlEscaped(), text.toHtmlEscaped()));
                    updateLabel_->setVisible(true);
                });
        checker->check();
    }

    void MainWindow::openSettings()
    {
        SettingsDialog dialog(themeMode_, language_, checkForUpdatesOnStartup_, this);

        connect(&dialog, &SettingsDialog::themeChanged, this, [this](ThemeMode mode)
        {
            themeMode_ = mode;
            applyTheme(*qApp, mode);
            saveSettings();
        });
        connect(&dialog, &SettingsDialog::languageChanged, this, [this](const QString &language)
        {
            if (language != language_)
            {
                applyLanguage(language);
                saveSettings();
            }
        });
        connect(&dialog, &SettingsDialog::checkForUpdatesChanged, this, [this](bool enabled)
        {
            checkForUpdatesOnStartup_ = enabled;
            saveSettings();
        });

        dialog.exec();
    }

    void MainWindow::addInputPath(const QString &path) const
    {
        if (path.isEmpty())
        {
            return;
        }

        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        const QString key = canonical.isEmpty() ? QDir::cleanPath(path) : canonical;

        for (int i = 0; i < inputList_->count(); ++i)
        {
            const QString existing = inputList_->item(i)->text();
            const QFileInfo existingInfo(existing);
            const QString existingKey = existingInfo.canonicalFilePath().isEmpty()
                                            ? QDir::cleanPath(existing)
                                            : existingInfo.canonicalFilePath();
            if (existingKey.compare(key, Qt::CaseInsensitive) == 0)
            {
                return;
            }
        }
        inputList_->addItem(path);
    }

    void MainWindow::setProcessing(bool processing) const
    {
        if (statusLabel_->property("state").isValid())
        {
            statusLabel_->setProperty("state", QVariant());
            statusLabel_->style()->unpolish(statusLabel_);
            statusLabel_->style()->polish(statusLabel_);
        }

        startButton_->setEnabled(!processing);
        stopButton_->setEnabled(processing);
        settingsButton_->setEnabled(!processing);
        modelCombo_->setEnabled(!processing);
        methodCombo_->setEnabled(!processing);
        shapeCombo_->setEnabled(!processing);
        modelPathEdit_->setEnabled(!processing);
        downloadButton_->setEnabled(!processing);
        outputDirEdit_->setEnabled(!processing);
        inputList_->setEnabled(!processing);
        recursiveCheck_->setEnabled(!processing);
        reviewCheck_->setEnabled(!processing);
        preserveMetaCheck_->setEnabled(!processing);
        scoreThresholdSpin_->setEnabled(!processing);
        nmsThresholdSpin_->setEnabled(!processing);
        blockSizeSpin_->setEnabled(!processing);
        paddingSpin_->setEnabled(!processing);
    }

    QStringList MainWindow::inputPaths() const
    {
        QStringList paths;
        for (int i = 0; i < inputList_->count(); ++i)
        {
            paths.append(inputList_->item(i)->text());
        }
        return paths;
    }

    void MainWindow::populateBundledModels()
    {
        for (const auto &model: builtinModels())
        {
            const auto existing = firstExistingModelPath(model.fileName);
            const auto path = existing.isEmpty() ? modelCacheDir() + "/" + model.fileName : existing;
            modelCombo_->addItem(QString(), path);
        }

        addRetranslation([this]
                         {
                             if (modelCombo_->count() >= 2)
                             {
                                 modelCombo_->setItemText(0, tr("Fast  ·  SCRFD 2.5G"));
                                 modelCombo_->setItemText(1, tr("Accurate  ·  SCRFD 10G"));
                             }
                         });

        modelCombo_->setCurrentIndex(0);
        updateModelPathFromSelection();
    }

    void MainWindow::downloadSelectedModel()
    {
        const auto modelPath = selectedModelPath();
        const BuiltinModel *builtin = findBuiltinModel(modelPath);
        if (builtin == nullptr)
        {
            return;
        }
        if (QFileInfo::exists(modelPath))
        {
            updateModelPathFromSelection();
            return;
        }

        appendLog(tr("Downloading %1…").arg(builtin->fileName));
        if (ensureBuiltinModelAvailable(this, *builtin, modelPath))
        {
            appendLog(tr("Model ready: %1").arg(builtin->fileName));
        }
        else
        {
            appendLog(tr("Model download was cancelled or failed."));
        }
        updateModelPathFromSelection();
    }

    void MainWindow::updateModelPathFromSelection() const
    {
        const auto path = selectedModelPath();
        const bool exists = !path.isEmpty() && QFileInfo::exists(path);
        const bool missingBuiltin = !exists && findBuiltinModel(path) != nullptr;

        if (path.isEmpty() || exists)
        {
            modelPathEdit_->setText(path);
        }
        else if (missingBuiltin)
        {
            modelPathEdit_->setText(tr("Not downloaded yet — click Download"));
        }
        else
        {
            modelPathEdit_->setText(path);
        }

        if (downloadButton_ != nullptr)
        {
            downloadButton_->setVisible(missingBuiltin);
        }
    }

    QString MainWindow::selectedModelPath() const
    {
        if (modelCombo_ == nullptr || modelCombo_->currentIndex() < 0)
        {
            return {};
        }

        return modelCombo_->currentData().toString();
    }

    void MainWindow::addRetranslation(std::function<void()> apply)
    {
        apply();
        retranslators_.push_back(std::move(apply));
    }

    void MainWindow::retranslateUi()
    {
        for (const auto &apply: retranslators_)
        {
            apply();
        }
    }

    void MainWindow::applyLanguage(const QString &language)
    {
        qApp->removeTranslator(&translator_);
        if (language != "en" && translator_.load(":/i18n/redactly_" + language + ".qm"))
        {
            qApp->installTranslator(&translator_);
        }
        language_ = language;
    }

    void MainWindow::changeEvent(QEvent *event)
    {
        if (event->type() == QEvent::LanguageChange)
        {
            retranslateUi();
        }
        QMainWindow::changeEvent(event);
    }

    void MainWindow::appendLog(const QString &message) const
    {
        const auto time = QDateTime::currentDateTime().toString("HH:mm:ss");
        logEdit_->appendPlainText(QString("[%1]  %2").arg(time, message));
        spdlog::info("{}", message.toStdString());
    }

    void MainWindow::reportValidationIssue(const QString &message, QWidget *field) const
    {
        statusLabel_->setProperty("state", "warning");
        statusLabel_->style()->unpolish(statusLabel_);
        statusLabel_->style()->polish(statusLabel_);
        statusLabel_->setText(message);
        appendLog(message);

        if (field != nullptr)
        {
            field->setProperty("invalid", true);
            field->style()->unpolish(field);
            field->style()->polish(field);
            QTimer::singleShot(2200, field, [field]
            {
                field->setProperty("invalid", QVariant());
                field->style()->unpolish(field);
                field->style()->polish(field);
            });
            field->setFocus(Qt::OtherFocusReason);
        }
    }

    ReviewResult MainWindow::requestReview(const QImage &image,
                                           const QString &sourceName,
                                           const QVector<QRectF> &detected,
                                           int currentIndex,
                                           int total)
    {
        ReviewDialog dialog(image, sourceName, detected, currentIndex, total, this);
        dialog.exec();
        return dialog.result();
    }
}

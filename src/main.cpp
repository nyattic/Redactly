#include "faceveil/MainWindow.hpp"
#include "faceveil/ReviewTypes.hpp"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QMetaType>
#include <QPalette>
#include <QRectF>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QVector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <exception>
#include <memory>
#include <vector>

#ifndef FACEVEIL_VERSION
#define FACEVEIL_VERSION "0.0.0"
#endif

namespace
{
    void applyLightPalette(QApplication &app)
    {
        QPalette palette;
        palette.setColor(QPalette::Window, QColor("#F7F8FA"));
        palette.setColor(QPalette::WindowText, QColor("#111827"));
        palette.setColor(QPalette::Base, QColor("#FFFFFF"));
        palette.setColor(QPalette::AlternateBase, QColor("#F3F4F6"));
        palette.setColor(QPalette::Text, QColor("#111827"));
        palette.setColor(QPalette::Button, QColor("#FFFFFF"));
        palette.setColor(QPalette::ButtonText, QColor("#111827"));
        palette.setColor(QPalette::Highlight, QColor("#111827"));
        palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        palette.setColor(QPalette::PlaceholderText, QColor("#9CA3AF"));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#9CA3AF"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#9CA3AF"));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9CA3AF"));
        app.setPalette(palette);
    }

    void setupLogging()
    {
        try
        {
            std::vector<spdlog::sink_ptr> sinks;
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

            const auto dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            if (!dataDir.isEmpty())
            {
                const auto logDir = dataDir + "/FaceVeil/logs";
                if (QDir().mkpath(logDir))
                {
                    const auto logFile = (logDir + "/faceveil.log").toStdString();
                    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        logFile, 1024 * 1024, 3));
                }
            }

            auto logger = std::make_shared<spdlog::logger>("faceveil", sinks.begin(), sinks.end());
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            logger->flush_on(spdlog::level::info);
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::info);
        }
        catch (const std::exception &)
        {
            spdlog::set_level(spdlog::level::off);
        }
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("FaceVeil");
    QCoreApplication::setOrganizationDomain("faceveil.app");
    QCoreApplication::setApplicationName("FaceVeil");
    QCoreApplication::setApplicationVersion(FACEVEIL_VERSION);

    setupLogging();

    QApplication::setStyle(QStyleFactory::create("Fusion"));
    applyLightPalette(app);

    qRegisterMetaType<faceveil::ReviewResult>("faceveil::ReviewResult");
    qRegisterMetaType<QVector<QRectF> >("QVector<QRectF>");

#ifdef Q_OS_MACOS
    QFont defaultFont("SF Pro Text", 13);
#elif defined(Q_OS_WIN)
    QFont defaultFont("Segoe UI", 10);
#else
    QFont defaultFont;
    defaultFont.setPointSize(10);
#endif
    app.setFont(defaultFont);

    faceveil::MainWindow window;
    window.show();
    return QApplication::exec();
}

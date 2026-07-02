#include "redactly/MainWindow.hpp"
#include "redactly/ReviewTypes.hpp"
#include "redactly/Theme.hpp"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QMetaType>
#include <QRectF>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QVector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <exception>
#include <memory>
#include <vector>

#ifndef REDACTLY_VERSION
#define REDACTLY_VERSION "0.0.0"
#endif

namespace
{
    void setupLogging()
    {
        try
        {
            std::vector<spdlog::sink_ptr> sinks;
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

            const auto dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            if (!dataDir.isEmpty())
            {
                const auto logDir = dataDir + "/Redactly/logs";
                if (QDir().mkpath(logDir))
                {
                    const auto logFile = (logDir + "/redactly.log").toStdString();
                    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        logFile, 1024 * 1024, 3));
                }
            }

            auto logger = std::make_shared<spdlog::logger>("redactly", sinks.begin(), sinks.end());
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

    QCoreApplication::setOrganizationName("Redactly");
    QCoreApplication::setOrganizationDomain("redactly.app");
    QCoreApplication::setApplicationName("Redactly");
    QCoreApplication::setApplicationVersion(REDACTLY_VERSION);

    setupLogging();

    QApplication::setStyle(QStyleFactory::create("Fusion"));
    {
        QSettings settings;
        const auto mode = redactly::themeModeFromString(settings.value("theme", "system").toString());
        redactly::applyTheme(app, mode);
    }

    qRegisterMetaType<redactly::ReviewResult>("redactly::ReviewResult");
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

    redactly::MainWindow window;
    window.show();
    return QApplication::exec();
}

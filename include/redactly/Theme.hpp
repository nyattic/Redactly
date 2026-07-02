#pragma once

#include <QString>

class QApplication;

namespace redactly
{
    enum class ThemeMode
    {
        System,
        Light,
        Dark
    };

    ThemeMode themeModeFromString(const QString &value);

    QString themeModeToString(ThemeMode mode);

    bool systemPrefersDark();

    void applyTheme(QApplication &app, ThemeMode mode);
}

#include "redactly/SettingsDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace redactly
{
    SettingsDialog::SettingsDialog(ThemeMode theme, const QString &language, bool checkForUpdates,
                                   QWidget *parent)
        : QDialog(parent)
    {
        setModal(true);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(20, 20, 20, 20);
        root->setSpacing(16);

        auto *form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setHorizontalSpacing(18);
        form->setVerticalSpacing(12);

        themeCombo_ = new QComboBox(this);
        themeCombo_->addItem(QString(), static_cast<int>(ThemeMode::System));
        themeCombo_->addItem(QString(), static_cast<int>(ThemeMode::Light));
        themeCombo_->addItem(QString(), static_cast<int>(ThemeMode::Dark));
        const int themeIndex = themeCombo_->findData(static_cast<int>(theme));
        themeCombo_->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

        languageCombo_ = new QComboBox(this);
        languageCombo_->addItem("English", "en");
        languageCombo_->addItem("한국어", "ko");
        const int languageIndex = languageCombo_->findData(language);
        languageCombo_->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);

        themeLabel_ = new QLabel(this);
        languageLabel_ = new QLabel(this);
        form->addRow(themeLabel_, themeCombo_);
        form->addRow(languageLabel_, languageCombo_);
        root->addLayout(form);

        updateCheck_ = new QCheckBox(this);
        updateCheck_->setChecked(checkForUpdates);
        root->addWidget(updateCheck_);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        closeButton_ = buttons->button(QDialogButtonBox::Close);
        closeButton_->setObjectName("primaryButton");
        closeButton_->setCursor(Qt::PointingHandCursor);
        root->addWidget(buttons);

        connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this]
        {
            emit themeChanged(static_cast<ThemeMode>(themeCombo_->currentData().toInt()));
        });
        connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this]
        {
            emit languageChanged(languageCombo_->currentData().toString());
        });
        connect(updateCheck_, &QCheckBox::toggled, this, [this](bool enabled)
        {
            emit checkForUpdatesChanged(enabled);
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        retranslate();
    }

    void SettingsDialog::retranslate()
    {
        setWindowTitle(tr("Settings"));
        themeLabel_->setText(tr("Theme"));
        languageLabel_->setText(tr("Language"));
        themeCombo_->setItemText(0, tr("System"));
        themeCombo_->setItemText(1, tr("Light"));
        themeCombo_->setItemText(2, tr("Dark"));
        updateCheck_->setText(tr("Check for updates on startup"));
        if (closeButton_ != nullptr)
        {
            closeButton_->setText(tr("Close"));
        }
    }

    void SettingsDialog::changeEvent(QEvent *event)
    {
        if (event->type() == QEvent::LanguageChange)
        {
            retranslate();
        }
        QDialog::changeEvent(event);
    }
}

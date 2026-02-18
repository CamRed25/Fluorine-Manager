#include "settingsdialogtheme.h"
#include "categoriesdialog.h"
#include "colortable.h"
#include "modlist.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <questionboxmemory.h>
#include <utility.h>

#include <QDir>
#include <QSet>

using namespace MOBase;

ThemeSettingsTab::ThemeSettingsTab(Settings& s, SettingsDialog& d) : SettingsTab(s, d)
{
  // style
  addStyles();
  selectStyle();

  // colors
  ui->colorTable->load(s);

  QObject::connect(ui->resetColorsBtn, &QPushButton::clicked, [&] {
    ui->colorTable->resetColors();
  });

  QObject::connect(ui->exploreStyles, &QPushButton::clicked, [&] {
    onExploreStyles();
  });
}

void ThemeSettingsTab::update()
{
  // style
  const QString oldStyle = settings().interface().styleName().value_or("");
  const QString newStyle =
      ui->styleBox->itemData(ui->styleBox->currentIndex()).toString();

  if (oldStyle != newStyle) {
    settings().interface().setStyleName(newStyle);
    emit settings().styleChanged(newStyle);
  }

  // colors
  ui->colorTable->commitColors();
}

void ThemeSettingsTab::addStyles()
{
  ui->styleBox->addItem("None", "");
  for (auto&& key : QStyleFactory::keys()) {
    ui->styleBox->addItem(key, key);
  }

  ui->styleBox->insertSeparator(ui->styleBox->count());

  // Collect stylesheet names from both bundled and user directories.
  // User styles override bundled styles with the same name.
  QSet<QString> seenFiles;
  const QString ssRelPath = ToQString(AppConfig::stylesheetsPath());

  // User stylesheets first (basePath is ~/.local/share/fluorine/ when MO2_BASE_DIR set)
  // Mark them with "(custom)" and record their names so bundled duplicates are skipped.
  const QString userPath = AppConfig::basePath() + "/" + ssRelPath;
  const QString bundledPath = QCoreApplication::applicationDirPath() + "/" + ssRelPath;
  if (userPath != bundledPath && QDir(userPath).exists()) {
    QDirIterator userIter(userPath, QStringList("*.qss"), QDir::Files,
                          QDirIterator::FollowSymlinks);
    while (userIter.hasNext()) {
      userIter.next();
      const QString fileName = userIter.fileName();
      seenFiles.insert(fileName.toLower());
      ui->styleBox->addItem(userIter.fileInfo().completeBaseName() + " (custom)", fileName);
    }
  }

  // Bundled stylesheets (applicationDirPath is /app/lib/fluorine in Flatpak)
  // Skip any that were overridden by user styles.
  QDirIterator bundledIter(bundledPath, QStringList("*.qss"), QDir::Files,
                           QDirIterator::FollowSymlinks);
  while (bundledIter.hasNext()) {
    bundledIter.next();
    const QString fileName = bundledIter.fileName();
    if (!seenFiles.contains(fileName.toLower())) {
      seenFiles.insert(fileName.toLower());
      ui->styleBox->addItem(bundledIter.fileInfo().completeBaseName(), fileName);
    }
  }
}

void ThemeSettingsTab::selectStyle()
{
  const int currentID =
      ui->styleBox->findData(settings().interface().styleName().value_or(""));

  if (currentID != -1) {
    ui->styleBox->setCurrentIndex(currentID);
  }
}

void ThemeSettingsTab::onExploreStyles()
{
  // Open the user stylesheets directory where custom styles can be added.
  // Create it if it doesn't exist.
  QString ssPath = AppConfig::basePath() + "/" +
                   ToQString(AppConfig::stylesheetsPath());
  QDir().mkpath(ssPath);
  shell::Explore(ssPath);
}

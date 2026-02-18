#ifndef FLUORINEPATHS_H
#define FLUORINEPATHS_H

#include <QString>

/// Returns the Fluorine data directory: ~/.var/app/com.fluorine.manager
QString fluorineDataDir();

/// One-time migration from ~/.local/share/fluorine/ back to
/// ~/.var/app/com.fluorine.manager/. Call before initLogging().
void fluorineMigrateDataDir();

#endif  // FLUORINEPATHS_H

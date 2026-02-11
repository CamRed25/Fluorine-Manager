/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SAFEWRITEFILE_H
#define SAFEWRITEFILE_H

#include "dllimport.h"
#include "utility.h"
#include <QFile>
#include <QSaveFile>
#include <QString>

namespace MOBase
{

#ifndef _WIN32
// Thin QFile wrapper that adds a no-op commit() for QSaveFile API compat.
class DirectWriteFile : public QFile
{
public:
  using QFile::QFile;
  bool commit()
  {
    flush();
    return true;
  }
};
#endif

/**
 * @brief a wrapper for QSaveFile that handles errors when opening the file to reduce
 * code duplication.  On Linux, uses plain QFile because QSaveFile's temp-file
 * strategy fails on many common filesystem configurations.
 */
class QDLLEXPORT SafeWriteFile
{
public:
  SafeWriteFile(const QString& fileName);

#ifdef _WIN32
  QSaveFile* operator->();
#else
  DirectWriteFile* operator->();
#endif

private:
#ifdef _WIN32
  QSaveFile m_SaveFile;
#else
  DirectWriteFile m_File;
#endif
};

}  // namespace MOBase

#endif  // SAFEWRITEFILE_H

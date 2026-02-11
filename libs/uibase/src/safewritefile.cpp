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

#include <uibase/safewritefile.h>
#include <uibase/log.h>
#include <QStorageInfo>
#include <QString>

namespace MOBase
{

#ifdef _WIN32

SafeWriteFile::SafeWriteFile(const QString& fileName) : m_SaveFile(fileName)
{
  if (!m_SaveFile.open(QIODeviceBase::WriteOnly)) {
    const auto av =
        static_cast<double>(QStorageInfo(m_SaveFile.fileName()).bytesAvailable());

    log::error(
        "failed to create temporary file for '{}', error {} ('{}'), {:.3f}GB available",
        m_SaveFile.fileName(), m_SaveFile.error(), m_SaveFile.errorString(),
        (av / 1024 / 1024 / 1024));

    throw Exception(
        QObject::tr(
            "Failed to save '%1', could not create a temporary file: %2 (error %3)")
            .arg(m_SaveFile.fileName())
            .arg(m_SaveFile.errorString())
            .arg(m_SaveFile.error()));
  }
}

QSaveFile* SafeWriteFile::operator->()
{
  Q_ASSERT(m_SaveFile.isOpen());
  return &m_SaveFile;
}

#else  // Linux — use plain QFile (QSaveFile is unreliable across filesystems)

SafeWriteFile::SafeWriteFile(const QString& fileName) : m_File(fileName)
{
  if (!m_File.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate)) {
    const auto av =
        static_cast<double>(QStorageInfo(m_File.fileName()).bytesAvailable());

    log::error("failed to open '{}' for writing, error {} ('{}'), {:.3f}GB available",
               m_File.fileName(), m_File.error(), m_File.errorString(),
               (av / 1024 / 1024 / 1024));

    throw Exception(
        QObject::tr("Failed to save '%1': %2 (error %3)")
            .arg(m_File.fileName())
            .arg(m_File.errorString())
            .arg(m_File.error()));
  }
}

DirectWriteFile* SafeWriteFile::operator->()
{
  Q_ASSERT(m_File.isOpen());
  return &m_File;
}

#endif

}  // namespace MOBase

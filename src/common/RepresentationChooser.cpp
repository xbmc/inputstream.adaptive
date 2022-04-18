/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RepresentationChooser.h"

#include "../utils/log.h"
#include "RepresentationChooserDefault.h"
#include "RepresentationChooserManualOSD.h"

#include <vector>

using namespace CHOOSER;

namespace
{
IRepresentationChooser* GetReprChooser(std::string_view type)
{
  if (type == "manual-osd")
    return new CRepresentationChooserManualOSD();
  else if (type == "default")
    return new CRepresentationChooserDefault();
  else
    return nullptr;
}
} // unnamed namespace

IRepresentationChooser* CHOOSER::CreateRepresentationChooser(
    const UTILS::PROPERTIES::KodiProperties& kodiProps)
{
  IRepresentationChooser* reprChooser{nullptr};

  // An add-on can override user settings
  if (!kodiProps.m_streamSelectionType.empty())
  {
    reprChooser = GetReprChooser(kodiProps.m_streamSelectionType);
    if (!reprChooser)
      LOG::Log(LOGERROR, "Stream selection type \"%s\" not exist. Fallback to user settings");
  }

  if (!reprChooser)
    reprChooser = GetReprChooser(kodi::addon::GetSettingString("adaptivestream.type"));

  // Safe check for wrong settings, fallback to default
  if (!reprChooser)
    reprChooser = new CRepresentationChooserDefault();

  reprChooser->Initialize(kodiProps);

  return reprChooser;
}

void IRepresentationChooser::SetScreenResolution(const int width, const int height)
{
  m_screenCurrentWidth = width;
  m_screenCurrentHeight = height;
}

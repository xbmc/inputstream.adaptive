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
#include "RepresentationChooserFixedRes.h"
#include "RepresentationChooserManualOSD.h"

#include <vector>

using namespace CHOOSER;

namespace
{
IRepresentationChooser* GetReprChooser(std::string_view type)
{
  // Chooser's names are used for add-on settings and Kodi properties
  if (type == "default" || type == "adaptive")
    return new CRepresentationChooserDefault();
  else if (type == "fixed-res")
    return new CRepresentationChooserFixedRes();
  else if (type == "manual-osd")
    return new CRepresentationChooserManualOSD();
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

  reprChooser->Initialize(kodiProps.m_chooserProps);

  return reprChooser;
}

void IRepresentationChooser::SetScreenResolution(const int width, const int height)
{
  m_screenCurrentWidth = width;
  m_screenCurrentHeight = height;
}

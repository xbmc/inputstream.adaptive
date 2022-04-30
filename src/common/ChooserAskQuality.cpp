/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ChooserAskQuality.h"

#include "../utils/StringUtils.h"
#include "../utils/Utils.h"
#include "../utils/log.h"
#include "ReprSelector.h"
#include "kodi/tools/StringUtils.h"

#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/gui/dialogs/Select.h>
#endif

#include <vector>

using namespace adaptive;
using namespace CHOOSER;
using namespace kodi::tools;
using namespace UTILS;

namespace
{
std::string CovertFpsToString(float value)
{
  std::string str{StringUtils::Format("%.3f", value)};
  std::size_t found = str.find_last_not_of("0");
  if (found != std::string::npos)
    str.erase(found + 1);

  if (str.back() == '.')
    str.pop_back();

  return str;
}
} // unnamed namespace

CRepresentationChooserAskQuality::CRepresentationChooserAskQuality()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Ask quality");
}

void CRepresentationChooserAskQuality::Initialize(const UTILS::PROPERTIES::ChooserProps& props)
{
}

void CRepresentationChooserAskQuality::PostInit()
{
}

AdaptiveTree::Representation* CRepresentationChooserAskQuality::GetNextRepresentation(
    AdaptiveTree::AdaptationSet* adp, AdaptiveTree::Representation* currentRep)
{
  if (currentRep)
    return currentRep;

  if (adp->type_ != AdaptiveTree::VIDEO)
  {
    CRepresentationSelector selector{m_screenCurrentWidth, m_screenCurrentHeight};
    return selector.HighestBw(adp);
  }

  //! @todo: currently we dont handle in any way a codec priority and selection
  //! that can happens when a manifest have multi-codec videos, therefore
  //! we sent to Kodi the video stream of each codec, but only the
  //! first one (in index order) will be choosen for the playback
  //! with the potential to poorly manage a bandwidth optimisation.
  //! So we ask to the user to select the quality only for the first video
  //! AdaptationSet and we try to select the same quality (resolution)
  //! on all other video AdaptationSet's (codecs)
  if (!m_isDialogShown)
  {
    // We find the best quality for the current resolution, to pre-select this entry
    CRepresentationSelector selector{m_screenCurrentWidth, m_screenCurrentHeight};
    AdaptiveTree::Representation* bestRep{selector.Highest(adp)};

    std::vector<std::string> entries;
    int preselIndex{-1};

    // Add available qualities
    for (size_t i{0}; i < adp->representations_.size(); i++)
    {
      AdaptiveTree::Representation* rep{adp->representations_[i]};
      if (!rep)
        continue;

      std::string entryName{kodi::addon::GetLocalizedString(30232)};
      STRING::ReplaceFirst(entryName, "{codec}", GetVideoCodecDesc(rep->codecs_));

      float fps{static_cast<float>(rep->fpsRate_)};
      if (fps > 0 && rep->fpsScale_ > 0)
        fps /= rep->fpsScale_;

      std::string quality;
      if (fps > 0)
      {
        quality = StringUtils::Format("(%ix%i, %s fps, %u Kbps)", rep->width_, rep->height_,
                                      CovertFpsToString(fps).c_str(), rep->bandwidth_ / 1000);
      }
      else
      {
        quality = StringUtils::Format("(%ix%i, %u Kbps)", rep->width_, rep->height_,
                                      rep->bandwidth_ / 1000);
      }
      STRING::ReplaceFirst(entryName, "{quality}", quality);

      if (rep == bestRep)
        preselIndex = static_cast<int>(i);

      entries.emplace_back(entryName);
    }

    int selIndex{kodi::gui::dialogs::Select::Show(kodi::addon::GetLocalizedString(30231), entries,
                                                  preselIndex, 10000)};

    AdaptiveTree::Representation* selRep{bestRep};

    if (selIndex >= 0) // If was <0 has been cancelled by the user
      selRep = adp->representations_[selIndex];

    m_selectedResWidth = selRep->width_;
    m_selectedResHeight = selRep->height_;
    m_isDialogShown = true;

    LogDetails(nullptr, selRep);
    return selRep;
  }
  else
  {
    // We fall here when:
    // - First start, but we have a multi-codec manifest (workaround)
    //   then we have to try select the same resolution for each other video codec
    //   these streams will be choosable for now via Kodi OSD video settings.
    // - Switched to next period, then we try select the same resolution
    CRepresentationSelector selector{m_selectedResWidth, m_selectedResHeight};
    return selector.Highest(adp);
  }
}

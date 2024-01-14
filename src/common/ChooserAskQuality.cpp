/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ChooserAskQuality.h"

#include "AdaptationSet.h"
#include "CompKodiProps.h"
#include "ReprSelector.h"
#include "Representation.h"
#include "SrvBroker.h"
#include "kodi/tools/StringUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/gui/dialogs/Select.h>
#endif

#include <vector>

using namespace kodi::tools;
using namespace CHOOSER;
using namespace PLAYLIST;
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

void CRepresentationChooserAskQuality::Initialize(const ADP::KODI_PROPS::ChooserProps& props)
{
}

void CRepresentationChooserAskQuality::PostInit()
{
}

PLAYLIST::CRepresentation* CRepresentationChooserAskQuality::GetNextRepresentation(
    PLAYLIST::CAdaptationSet* adp, PLAYLIST::CRepresentation* currentRep)
{
  if (currentRep)
    return currentRep;

  if (adp->GetStreamType() != StreamType::VIDEO)
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
    CRepresentation* bestRep{selector.Highest(adp)};

    std::vector<std::string> entries;
    int preselIndex{-1};
    int selIndex{0};
    const size_t reprSize = adp->GetRepresentations().size();

    if (reprSize > 1)
    {
      // Add available qualities
      for (auto itRep = adp->GetRepresentations().begin(); itRep  !=  adp->GetRepresentations().end(); itRep++)
      {
        CRepresentation* repr = (*itRep).get();

        std::string entryName{kodi::addon::GetLocalizedString(30232)};
        STRING::ReplaceFirst(entryName, "{codec}", CODEC::GetVideoDesc(repr->GetCodecs()));

        float fps{static_cast<float>(repr->GetFrameRate())};
        if (fps > 0 && repr->GetFrameRateScale() > 0)
          fps /= repr->GetFrameRateScale();

        std::string quality = "(";
        if (repr->GetWidth() > 0 && repr->GetHeight() > 0)
          quality += StringUtils::Format("%ix%i, ", repr->GetWidth(), repr->GetHeight());
        if (fps > 0)
          quality += StringUtils::Format("%s fps, ", CovertFpsToString(fps).c_str());

        quality += StringUtils::Format("%u Kbps)", repr->GetBandwidth() / 1000);
        STRING::ReplaceFirst(entryName, "{quality}", quality);

        if (repr == bestRep)
          preselIndex = static_cast<int>(std::distance(adp->GetRepresentations().begin(), itRep));

        entries.emplace_back(entryName);        
      }
      
      selIndex = kodi::gui::dialogs::Select::Show(kodi::addon::GetLocalizedString(30231), entries,
                                                  preselIndex, 10000);
    }

    CRepresentation* selRep{bestRep};

    if (selIndex >= 0) // If was <0 has been cancelled by the user
      selRep = adp->GetRepresentations()[selIndex].get();

    m_selectedResWidth = selRep->GetWidth();
    m_selectedResHeight = selRep->GetHeight();
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

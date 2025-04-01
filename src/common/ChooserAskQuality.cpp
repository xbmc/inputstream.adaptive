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
#include "Period.h"
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

PLAYLIST::CAdaptationSet* CHOOSER::CRepresentationChooserAskQuality::GetPreferredVideoAdpSet(
    PLAYLIST::CPeriod* period, PLAYLIST::CAdaptationSet* adpSetPreferred)
{
  // If the dialog box has already been displayed, then the period has changed
  if (m_isDialogShown)
  {
    // Try find the adaptation set with same codec or fallback to the preferred
    PLAYLIST::CAdaptationSet* selAdpSet = adpSetPreferred;
    for (auto& adpSet : period->GetAdaptationSets())
    {
      if (adpSet->GetStreamType() != StreamType::VIDEO || adpSet->GetRepresentations().size() == 0)
        continue;

      if (CODEC::GetVideoDesc(adpSet->GetCodecs()) != m_selectedVideoCodecDesc)
        continue;
    
      selAdpSet = adpSet.get();
      break;
    }

    return selAdpSet;
  }
  else
  {
    CRepresentationSelector selector{m_screenCurrentWidth, m_screenCurrentHeight};
    std::vector<std::string> entries;
    std::vector<std::pair<CAdaptationSet*, CRepresentation*>> entriesOjb;
    int preselIndex{-1}; // Preselected list item
    int selIndex{0};

    for (auto& adpSet : period->GetAdaptationSets())
    {
      if (adpSet->GetStreamType() != StreamType::VIDEO || adpSet->GetRepresentations().size() == 0)
        continue;

      CRepresentation* bestRep{nullptr};

      // preferred adaptation set, in order to have a preferred codec for multi-codec manifests
      if (adpSetPreferred == adpSet.get())
      {
        bestRep = selector.Highest(adpSetPreferred);
      }

      for (auto& repr : adpSet->GetRepresentations())
      {
        if (!repr->isPlayable)
          continue;

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

        entries.emplace_back(entryName);
        entriesOjb.emplace_back(adpSet.get(), repr.get());

        if (repr.get() == bestRep)
          preselIndex = static_cast<int>(entries.size()) - 1;
      }
    }

    if (entries.size() > 1)
    {
      selIndex = kodi::gui::dialogs::Select::Show(kodi::addon::GetLocalizedString(30231), entries,
                                                  preselIndex, 10000);
    }

    if (!entries.empty())
    {
      CAdaptationSet* selAdpSet{entriesOjb[preselIndex].first};
      CRepresentation* selRep{entriesOjb[preselIndex].second};

      if (selIndex >= 0) // If selIndex is <0 has been cancelled by the user
      {
        selAdpSet = entriesOjb[selIndex].first;
        selRep = entriesOjb[selIndex].second;
      }

      m_selectedResWidth = selRep->GetWidth();
      m_selectedResHeight = selRep->GetHeight();
      // Convert codec to description, as the codec string may be slightly different
      // when using ISO BMFF format, and the comparison may fail
      m_selectedVideoCodecDesc = CODEC::GetVideoDesc(selAdpSet->GetCodecs());

      m_isDialogShown = true;

      LogDetails(nullptr, selRep);
      return selAdpSet;
    }
  }
  return adpSetPreferred;
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

  CRepresentationSelector selector{m_selectedResWidth, m_selectedResHeight};
  return selector.Highest(adp);
}

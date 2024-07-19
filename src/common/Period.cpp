/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Period.h"

#include "AdaptationSet.h"
#include "Representation.h"
#include "utils/log.h"

using namespace PLAYLIST;

PLAYLIST::CPeriod::CPeriod() : CCommonSegAttribs()
{
  m_psshSets.emplace_back(PSSHSet());
}

PLAYLIST::CPeriod::~CPeriod()
{
}

void PLAYLIST::CPeriod::CopyHLSData(const CPeriod* other)
{
  m_adaptationSets.reserve(other->m_adaptationSets.size());
  for (const auto& otherAdp : other->m_adaptationSets)
  {
    std::unique_ptr<CAdaptationSet> adp = CAdaptationSet::MakeUniquePtr(this);
    adp->CopyHLSData(otherAdp.get());
    m_adaptationSets.push_back(std::move(adp));
  }

  m_baseUrl = other->m_baseUrl;
  m_id = other->m_id;
  m_timescale = other->m_timescale;
  m_includedStreamType = other->m_includedStreamType;
}

uint16_t PLAYLIST::CPeriod::InsertPSSHSet(const PSSHSet& psshSet)
{
  auto itPssh = m_psshSets.end();

  if (psshSet.m_licenseUrl.empty())
  {
    // Find the psshSet by skipping the first one of the list (for unencrypted streams)
    // note that PSSHSet struct has a custom comparator for std::find
    itPssh = std::find(m_psshSets.begin() + 1, m_psshSets.end(), psshSet);
  }

  if (itPssh != m_psshSets.end() && itPssh->m_usageCount == 0)
  {
    // If the existing psshSet is not used, replace it with the new one
    *itPssh = psshSet;
  }
  else
  {
    // Add a new PsshSet
    itPssh = m_psshSets.insert(m_psshSets.end(), psshSet);
  }

  itPssh->m_usageCount++;

  return static_cast<uint16_t>(itPssh - m_psshSets.begin());
}

void PLAYLIST::CPeriod::RemovePSSHSet(uint16_t pssh_set)
{
  for (std::unique_ptr<PLAYLIST::CAdaptationSet>& adpSet : m_adaptationSets)
  {
    auto& reps = adpSet->GetRepresentations();

    for (auto itRepr = reps.begin(); itRepr != reps.end();)
    {
      if ((*itRepr)->m_psshSetPos == pssh_set)
        itRepr = reps.erase(itRepr);
      else
        itRepr++;
    }
  }
}

void PLAYLIST::CPeriod::DecreasePSSHSetUsageCount(uint16_t pssh_set)
{
  if (pssh_set >= m_psshSets.size())
  {
    LOG::LogF(LOGERROR, "Cannot decrease PSSH usage, PSSHSet position %u exceeds the container size", pssh_set);
    return;
  }

  PSSHSet& psshSet = m_psshSets[pssh_set];
  if (psshSet.m_usageCount > 0)
    psshSet.m_usageCount--;
}

void PLAYLIST::CPeriod::AddAdaptationSet(std::unique_ptr<CAdaptationSet>& adaptationSet)
{
  m_adaptationSets.push_back(std::move(adaptationSet));
}

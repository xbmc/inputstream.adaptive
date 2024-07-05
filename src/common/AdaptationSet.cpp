/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptationSet.h"

#include "Representation.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"

#include <algorithm> // any_of
#include <iterator> // back_inserter

using namespace PLAYLIST;
using namespace UTILS;

void PLAYLIST::CAdaptationSet::AddCodecs(std::string_view codecs)
{
  std::set<std::string> list = STRING::SplitToSet(codecs.data(), ',');
  m_codecs.insert(list.begin(), list.end());
}

void PLAYLIST::CAdaptationSet::AddCodecs(const std::set<std::string>& codecs)
{
  m_codecs.insert(codecs.begin(), codecs.end());
}

bool PLAYLIST::CAdaptationSet::ContainsCodec(std::string_view codec)
{
  if (std::any_of(m_codecs.begin(), m_codecs.end(),
                  [codec](const std::string_view name) { return STRING::Contains(name, codec); }))
  {
    return true;
  }
  return false;
}

void PLAYLIST::CAdaptationSet::AddSwitchingIds(std::string_view switchingIds)
{
  std::vector<std::string> list = STRING::SplitToVec(switchingIds.data(), ',');
  m_switchingIds.insert(m_switchingIds.end(), list.begin(), list.end());
}

void PLAYLIST::CAdaptationSet::AddRepresentation(
    std::unique_ptr<CRepresentation>& representation)
{
  m_representations.push_back(std::move(representation));
}

std::vector<CRepresentation*> PLAYLIST::CAdaptationSet::GetRepresentationsPtr()
{
  std::vector<CRepresentation*> ptrReprs;
  std::transform(m_representations.begin(), m_representations.end(), std::back_inserter(ptrReprs),
                 [](const std::unique_ptr<CRepresentation>& r) { return r.get(); });
  return ptrReprs;
}

void PLAYLIST::CAdaptationSet::CopyHLSData(const CAdaptationSet* other)
{
  m_representations.reserve(other->m_representations.size());
  for (const auto& otherRep : other->m_representations)
  {
    std::unique_ptr<CRepresentation> rep = CRepresentation::MakeUniquePtr(this);
    rep->CopyHLSData(otherRep.get());
    m_representations.push_back(std::move(rep));
  }

  m_baseUrl = other->m_baseUrl;
  m_streamType = other->m_streamType;
  m_isImpaired = other->m_isImpaired;
  m_isOriginal = other->m_isOriginal;
  m_isDefault = other->m_isDefault;
  m_isForced = other->m_isForced;
  m_language = other->m_language;
  m_mimeType = other->m_mimeType;
  m_baseUrl = other->m_baseUrl;
  m_id = other->m_id;
  m_group = other->m_group;
  m_codecs = other->m_codecs;
  m_name = other->m_name;
}

bool PLAYLIST::CAdaptationSet::IsMergeable(const CAdaptationSet* other) const
{
  if (m_streamType != other->m_streamType)
    return false;

  if (m_streamType == StreamType::AUDIO)
  {
    if (m_id == other->m_id && m_startPts == other->m_startPts &&
        m_startNumber == other->m_startNumber && m_duration == other->m_duration &&
        m_group == other->m_group && m_language == other->m_language && m_name == other->m_name &&
        m_baseUrl == other->m_baseUrl && m_isDefault == other->m_isDefault &&
        m_isOriginal == other->m_isOriginal && m_isForced == other->m_isForced &&
        m_isImpaired == other->m_isImpaired && m_mimeType == other->m_mimeType &&
        m_audioChannels == other->m_audioChannels && m_codecs == other->m_codecs)
    {
      return true;
    }
  }

  return false;
}

bool PLAYLIST::CAdaptationSet::CompareSwitchingId(const CAdaptationSet* other) const
{
  if (m_streamType != other->m_streamType || m_switchingIds.empty())
    return false;

  if (m_streamType == StreamType::VIDEO)
  {
    if (m_group == other->m_group &&
        std::find(m_switchingIds.cbegin(), m_switchingIds.cend(), other->m_id) !=
            m_switchingIds.cend() &&
        std::find(other->m_switchingIds.cbegin(), other->m_switchingIds.cend(), m_id) !=
            other->m_switchingIds.cend())
    {
      //! @todo: we have no way to determine supported codecs by hardware in use
      //! and can broken playback, we allow same codec only
      for (std::string codec : m_codecs)
      {
        codec = codec.substr(0, codec.find('.')); // Get fourcc only
        if (CODEC::IsVideo(codec) && CODEC::Contains(other->m_codecs, codec))
        {
          return true;
        }
      }
    }
  }
  else if (m_streamType == StreamType::AUDIO)
  {
    if (m_language == other->m_language && m_group == other->m_group &&
        std::find(m_switchingIds.cbegin(), m_switchingIds.cend(), other->m_id) !=
            m_switchingIds.cend() &&
        std::find(other->m_switchingIds.cbegin(), other->m_switchingIds.cend(), m_id) !=
            other->m_switchingIds.cend())
    {
      return true;
    }
  }

  return false;
}

bool PLAYLIST::CAdaptationSet::Compare(const std::unique_ptr<CAdaptationSet>& left,
                                       const std::unique_ptr<CAdaptationSet>& right)
{
  if (left->m_streamType != right->m_streamType)
    return left->m_streamType < right->m_streamType;

  if (left->m_isDefault != right->m_isDefault)
    return left->m_isDefault;

  if (left->m_streamType == StreamType::AUDIO)
  {
    if (left->m_name != right->m_name)
      return left->m_name < right->m_name;

    if (left->m_isImpaired != right->m_isImpaired)
      return !left->m_isImpaired;

    if (left->m_isOriginal != right->m_isOriginal)
      return left->m_isOriginal;

    if (!left->m_representations.empty() && !right->m_representations.empty())
    {
      const PLAYLIST::CRepresentation* leftRepr = left->m_representations[0].get();
      const PLAYLIST::CRepresentation* rightRepr = right->m_representations[0].get();

      if (leftRepr->GetCodecs() != rightRepr->GetCodecs())
        return leftRepr->GetCodecs() < rightRepr->GetCodecs();

      if (leftRepr->GetAudioChannels() != rightRepr->GetAudioChannels())
        return leftRepr->GetAudioChannels() < rightRepr->GetAudioChannels();
    }
  }
  else if (left->m_streamType == StreamType::SUBTITLE)
  {
    if (left->m_isImpaired != right->m_isImpaired)
      return !left->m_isImpaired;

    if (left->m_isForced != right->m_isForced)
      return left->m_isForced;
  }

  return false;
}

PLAYLIST::CAdaptationSet* PLAYLIST::CAdaptationSet::FindByCodec(
    std::vector<std::unique_ptr<CAdaptationSet>>& adpSets, std::string codec)
{
  auto itAdpSet = std::find_if(adpSets.cbegin(), adpSets.cend(),
                               [&codec](const std::unique_ptr<CAdaptationSet>& item)
                               { return CODEC::Contains(item->GetCodecs(), codec); });
  if (itAdpSet != adpSets.cend())
    return (*itAdpSet).get();

  return nullptr;
}

CAdaptationSet* PLAYLIST::CAdaptationSet::FindMergeable(
    std::vector<std::unique_ptr<CAdaptationSet>>& adpSets, CAdaptationSet* adpSet)
{
  auto itAdpSet = std::find_if(adpSets.cbegin(), adpSets.cend(),
                               [&adpSet](const std::unique_ptr<CAdaptationSet>& item)
                               { return item->IsMergeable(adpSet); });
  if (itAdpSet != adpSets.cend())
    return (*itAdpSet).get();

  return nullptr;
}

PLAYLIST::CAdaptationSet* PLAYLIST::CAdaptationSet::FindByStreamType(
    std::vector<std::unique_ptr<CAdaptationSet>>& adpSets, StreamType type)
{
  auto itAdpSet = std::find_if(adpSets.cbegin(), adpSets.cend(),
                               [&type](const std::unique_ptr<CAdaptationSet>& item)
                               { return item->GetStreamType() == type; });
  if (itAdpSet != adpSets.cend())
    return (*itAdpSet).get();

  return nullptr;
}

PLAYLIST::CAdaptationSet* PLAYLIST::CAdaptationSet::FindByFirstAVStream(
  std::vector<std::unique_ptr<CAdaptationSet>>& adpSets)
{
  CAdaptationSet* adp = CAdaptationSet::FindByStreamType(adpSets, StreamType::VIDEO);
  if (!adp)
    adp = CAdaptationSet::FindByStreamType(adpSets, StreamType::AUDIO);

  return adp;
}

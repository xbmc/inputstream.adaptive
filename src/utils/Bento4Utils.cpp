/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Bento4Utils.h"

#include "utils/log.h"

#include <algorithm>
#include <cstring>

#include <bento4/Ap4PsshAtom.h>
#include <bento4/Ap4SbgpAtom.h>

using namespace UTILS::BENTO4;

namespace
{
constexpr AP4_UI32 GROUPING_TYPE_SEIG = AP4_ATOM_TYPE('s', 'e', 'i', 'g');
// SBGP indexes above this value refer to the SGPD of the fragment, the lower ones
// to the SGPD of the init segment (ISO/IEC 14496-12, 8.9.4)
constexpr AP4_UI32 SBGP_FRAGMENT_INDEX_BASE = 0x10000;

bool HasSeigKid(const CencSeigGroupEntry& entry)
{
  return entry.isProtected == 1 && !entry.keySets.empty() &&
         entry.keySets.front().kid.size() == 16 &&
         std::any_of(entry.keySets.front().kid.cbegin(), entry.keySets.front().kid.cend(),
                     [](std::uint8_t b) { return b != 0; });
}

std::vector<std::uint8_t> FindPsshData(AP4_ContainerAtom* container, const std::uint8_t* systemId)
{
  AP4_Atom* childAtom{nullptr};
  unsigned int atomIndex{0};
  while ((childAtom = container->GetChild(AP4_ATOM_TYPE_PSSH, atomIndex++)) != nullptr)
  {
    AP4_PsshAtom* pssh = AP4_DYNAMIC_CAST(AP4_PsshAtom, childAtom);
    if (!pssh)
      continue;

    if (std::memcmp(pssh->GetSystemId(), systemId, 16) != 0)
      continue;

    const AP4_DataBuffer& data = pssh->GetData();
    return std::vector<std::uint8_t>(data.GetData(), data.GetData() + data.GetDataSize());
  }
  return {};
}
} // unnamed namespace

std::vector<std::uint8_t> UTILS::BENTO4::FindMoofPsshData(AP4_ContainerAtom* moof,
                                                          const std::uint8_t* systemId)
{
  if (!moof || !systemId)
    return {};

  std::vector<std::uint8_t> pssh = FindPsshData(moof, systemId);
  if (!pssh.empty())
    return pssh;

  // Some packagers place the PSSH inside the TRAF
  AP4_Atom* trafAtom{nullptr};
  unsigned int trafIndex{0};
  while ((trafAtom = moof->GetChild(AP4_ATOM_TYPE_TRAF, trafIndex++)) != nullptr)
  {
    AP4_ContainerAtom* traf = AP4_DYNAMIC_CAST(AP4_ContainerAtom, trafAtom);
    if (!traf)
      continue;

    pssh = FindPsshData(traf, systemId);
    if (!pssh.empty())
      return pssh;
  }

  return {};
}

std::vector<std::uint8_t> UTILS::BENTO4::GetTrafSeigKid(AP4_ContainerAtom* traf, bool& isPartial)
{
  isPartial = false;
  if (!traf)
    return {};

  std::vector<CencSeigGroupEntry> entries;
  AP4_Atom* childAtom{nullptr};
  unsigned int atomIndex{0};
  while ((childAtom = traf->GetChild(AP4_ATOM_TYPE_SGPD, atomIndex++)) != nullptr)
  {
    // A malformed box is created as a generic atom and the cast fails.
    // FMP4SgpdAtom only adds accessors, it is not known to the Bento4 RTTI
    AP4_SgpdAtom* sgpd = AP4_DYNAMIC_CAST(AP4_SgpdAtom, childAtom);
    if (!sgpd)
      continue;

    auto sgpdAccessor = static_cast<FMP4SgpdAtom*>(sgpd);
    if (sgpdAccessor->GetGroupingType() != GROUPING_TYPE_SEIG)
      continue;

    entries = sgpdAccessor->GetSeigEntries();
    break; // Assume that only a single SGPD can contain SEIG
  }
  if (entries.empty())
    return {};

  AP4_SbgpAtom* sbgp{nullptr};
  atomIndex = 0;
  while ((childAtom = traf->GetChild(AP4_ATOM_TYPE_SBGP, atomIndex++)) != nullptr)
  {
    sbgp = AP4_DYNAMIC_CAST(AP4_SbgpAtom, childAtom);
    if (sbgp && sbgp->GetGroupingType() == GROUPING_TYPE_SEIG)
      break;
    sbgp = nullptr;
  }

  const CencSeigGroupEntry* selected{nullptr};
  if (sbgp)
  {
    // The distinct group indexes used by the samples, 0 means no group (track defaults)
    std::vector<AP4_UI32> groupIndexes;
    AP4_Array<AP4_SbgpAtom::Entry>& sbgpEntries = sbgp->GetEntries();
    for (unsigned int i = 0; i < sbgpEntries.ItemCount(); ++i)
    {
      const AP4_UI32 groupIndex = sbgpEntries[i].group_description_index;
      if (sbgpEntries[i].sample_count > 0 &&
          std::find(groupIndexes.begin(), groupIndexes.end(), groupIndex) == groupIndexes.end())
      {
        groupIndexes.emplace_back(groupIndex);
      }
    }
    isPartial = groupIndexes.size() > 1;

    for (const AP4_UI32 groupIndex : groupIndexes)
    {
      if (groupIndex <= SBGP_FRAGMENT_INDEX_BASE)
        continue; // No group, or a group of the init segment

      const size_t entryIndex = groupIndex - SBGP_FRAGMENT_INDEX_BASE - 1;
      if (entryIndex < entries.size() && HasSeigKid(entries[entryIndex]))
      {
        selected = &entries[entryIndex];
        break;
      }
    }
  }
  else
  {
    // Without SBGP no sample is mapped to the group. Some packagers leave it out
    // when the fragment has a single group, so its protected entry is used
    for (const CencSeigGroupEntry& entry : entries)
    {
      if (!HasSeigKid(entry))
        continue;
      if (selected)
      {
        isPartial = true;
        break;
      }
      selected = &entry;
    }
  }

  if (!selected)
    return {};

  // Multiple keys in the entry, the subsamples may use the other ones
  if (selected->keySets.size() > 1)
    isPartial = true;

  return selected->keySets.front().kid;
}

std::vector<CencSeigGroupEntry> UTILS::BENTO4::FMP4SgpdAtom::GetSeigEntries()
{
  if (m_GroupingType != AP4_ATOM_TYPE('s', 'e', 'i', 'g'))
    return {};

  if (m_Version > 1)
  {
    LOG::LogF(LOGERROR, "Unsupported TRAF/SGPD box version %u", m_Version);
    return {};
  }

  std::vector<CencSeigGroupEntry> entries;

  for (AP4_List<AP4_DataBuffer>::Item* item = m_Entries.FirstItem(); item; item = item->GetNext())
  {
    AP4_DataBuffer* data = item->GetData();
    AP4_MemoryByteStream stream(data->GetData(), data->GetDataSize());
    BENTO4::CencSeigGroupEntry entry;

    // First byte: multi_key_flag (1 bit) + reserved (7 bit)
    AP4_UI08 firstByte;
    AP4_Result res = stream.ReadUI08(firstByte);
    if (AP4_FAILED(res))
    {
      LOG::LogF(LOGERROR, "Failed to parse multi_key_flag from SEIG group desc entry");
      entries.emplace_back(entry); // Keeps the SGPD entry indexes referenced by SBGP
      continue;
    }
    entry.multiKeyFlag = (firstByte & 0x80) != 0; // Bit 7 (MSB)
    // Bits 0-6 are reserved, per spec they are ignored rather than rejected (some muxers set them)

    // Second byte: crypt_byte_block (4 bit MSB) + skip_byte_block (4 bit LSB)
    AP4_UI08 secondByte;
    res = stream.ReadUI08(secondByte);
    if (AP4_FAILED(res))
    {
      LOG::LogF(LOGERROR,
                "Failed to parse crypt_byte_block/skip_byte_block from SEIG group desc entry");
      entries.emplace_back(entry); // Keeps the SGPD entry indexes referenced by SBGP
      continue;
    }
    entry.cryptByteBlock = (secondByte >> 4) & 0x0F;
    entry.skipByteBlock = secondByte & 0x0F;

    // Third byte: isProtected (8 bit)
    res = stream.ReadUI08(entry.isProtected);
    if (AP4_FAILED(res))
    {
      LOG::LogF(LOGERROR, "Failed to parse isProtected from SEIG group desc entry");
      entries.emplace_back(entry); // Keeps the SGPD entry indexes referenced by SBGP
      continue;
    }

    // Key count
    uint16_t keyCount = 1;
    if (entry.multiKeyFlag)
    {
      AP4_UI16 value; // 16 bit
      res = stream.ReadUI16(value);
      if (AP4_FAILED(res))
      {
        LOG::LogF(LOGERROR, "Failed to parse key count from SEIG group desc entry");
        entries.emplace_back(entry); // Keeps the SGPD entry indexes referenced by SBGP
        continue;
      }
      keyCount = value;
    }

    // Read key sets. On a short read the rest of the entry is lost, so stop reading it.
    // The entry is kept anyway, to preserve the SGPD entry indexes referenced by SBGP
    for (uint16_t k = 0; k < keyCount; ++k)
    {
      BENTO4::CencSeigKeySet kset;

      res = stream.ReadUI08(kset.perSampleIvSize);
      if (AP4_FAILED(res))
      {
        LOG::LogF(LOGERROR, "Failed to parse Per_Sample_IV_Size from SEIG group desc entry");
        break;
      }

      kset.kid.resize(16, 0);
      res = stream.Read(kset.kid.data(), 16);
      if (AP4_FAILED(res))
      {
        LOG::LogF(LOGERROR, "Failed to parse KID from SEIG group desc entry");
        break;
      }

      // constant_IV is present only for protected entries with no per-sample IV (ISO/IEC 23001-7)
      if (kset.perSampleIvSize == 0 && entry.isProtected == 1)
      {
        res = stream.ReadUI08(kset.constantIvSize);
        if (AP4_FAILED(res))
        {
          LOG::LogF(LOGERROR, "Failed to parse constant_IV_size from SEIG group desc entry");
          break;
        }

        if (kset.constantIvSize > 0)
        {
          kset.constantIv.resize(kset.constantIvSize);
          res = stream.Read(kset.constantIv.data(), kset.constantIvSize);
          if (AP4_FAILED(res))
          {
            LOG::LogF(LOGERROR, "Failed to parse constant_IV from SEIG group desc entry");
            break;
          }
        }
      }

      entry.keySets.emplace_back(kset);
    }

    entries.emplace_back(entry);
  }

  return entries;
}

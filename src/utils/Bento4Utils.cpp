/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Bento4Utils.h"

#include "utils/log.h"

#include <cstring>

#include <bento4/Ap4PsshAtom.h>

using namespace UTILS::BENTO4;

namespace
{
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

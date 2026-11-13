/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <cstdint>
#include <vector>

#include <bento4/Ap4.h>
#include <bento4/Ap4SgpdAtom.h>

namespace UTILS
{
namespace BENTO4
{

// \brief Accessor for accessing AP4_UnknownUuidAtom data
class ATTR_DLL_LOCAL FMP4UnknownUuidAtom : public AP4_UnknownUuidAtom
{
public:
  const AP4_DataBuffer& GetData() { return m_Data; }
};

struct CencSeigKeySet
{
  std::uint8_t perSampleIvSize = 0;
  std::vector<std::uint8_t> kid;
  std::uint8_t constantIvSize = 0; // Only when per_sample_iv_size==0
  std::vector<std::uint8_t> constantIv;
};

struct CencSeigGroupEntry // CencSampleEncryptionInformationGroupEntry
{
  bool multiKeyFlag = false;
  std::uint8_t cryptByteBlock = 0; // Encryption pattern (4 bit)
  std::uint8_t skipByteBlock = 0; // Encryption pattern (4 bit)
  std::uint8_t isProtected = 0; // 0=unprotected, 1=protected, other reserved
  std::vector<CencSeigKeySet> keySets;
};

// \brief Accessor for accessing AP4_SgpdAtom data
class ATTR_DLL_LOCAL FMP4SgpdAtom : public AP4_SgpdAtom
{
public:
  AP4_UI32 GetGroupingType() const { return m_GroupingType; }

  std::vector<CencSeigGroupEntry> GetSeigEntries();
};

// \brief Find the PSSH box of a DRM system in a media segment's MOOF. The box normally
//        is a direct child of the MOOF, but some packagers place it inside a TRAF,
//        so both are searched. PSSH boxes of other DRM systems are skipped.
// \param moof The MOOF atom
// \param systemId The 16-byte DRM system ID to look for
// \return The Data field of the PSSH box (no box header), or empty if not found
std::vector<std::uint8_t> FindMoofPsshData(AP4_ContainerAtom* moof, const std::uint8_t* systemId);

} // namespace BENTO4
} // namespace UTILS

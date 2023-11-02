/*
 *  Copyright (C) 2022 Team Kodi
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

// forwards
namespace PLAYLIST
{
class CAdaptationSet;
class CRepresentation;
}

namespace CHOOSER
{

class ATTR_DLL_LOCAL CRepresentationSelector
{
public:
  CRepresentationSelector(const int& resWidth, const int& resHeight);
  ~CRepresentationSelector() {}

  /*!
   * \brief Select the lowest representation (as index order)
   * \param adaptSet The adaption set
   * \return The lowest representation, otherwise nullptr if no available
   */
  PLAYLIST::CRepresentation* Lowest(PLAYLIST::CAdaptationSet* adaptSet) const;

  /*!
   * \brief Select the highest representation quality closer to the screen resolution
   * \param adaptSet The adaption set
   * \return The highest representation, otherwise nullptr if no available
   */
  PLAYLIST::CRepresentation* Highest(PLAYLIST::CAdaptationSet* adaptSet) const;

  /*!
   * \brief Select the representation with the higher bandwidth
   * \param adaptSet The adaption set
   * \return The representation with higher bandwidth, otherwise nullptr if no available
   */
  PLAYLIST::CRepresentation* HighestBw(PLAYLIST::CAdaptationSet* adaptSet) const;


  PLAYLIST::CRepresentation* Higher(PLAYLIST::CAdaptationSet* adaptSet,
                                    PLAYLIST::CRepresentation* currRep) const;

private:
  int m_screenWidth{0};
  int m_screenHeight{0};
};

} // namespace adaptive

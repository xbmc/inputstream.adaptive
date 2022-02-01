/*
 *  Copyright (C) 2005-2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef ES_SUBTITLE_H
#define ES_SUBTITLE_H

#include "elementaryStream.h"

namespace TSDemux
{
  class ES_Subtitle : public ElementaryStream
  {
  public:
    ES_Subtitle(uint16_t pid);
    virtual ~ES_Subtitle();

    virtual void Parse(STREAM_PKT* pkt);
  };
}

#endif /* ES_SUBTITLE_H */

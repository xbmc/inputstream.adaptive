/*
 *  Copyright (C) 2005-2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef ES_TELETEXT_H
#define ES_TELETEXT_H

#include "elementaryStream.h"

namespace TSDemux
{
  class ES_Teletext : public ElementaryStream
  {
  public:
    ES_Teletext(uint16_t pid);
    virtual ~ES_Teletext();

    virtual void Parse(STREAM_PKT* pkt);
  };
}

#endif /* ES_TELETEXT_H */

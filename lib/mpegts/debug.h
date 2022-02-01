/*
 *  Copyright (C) 2015 Jean-Luc Barriere
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef TS_DEBUG_H
#define TS_DEBUG_H

#define DEMUX_DBG_NONE  -1
#define DEMUX_DBG_ERROR  0
#define DEMUX_DBG_WARN   1
#define DEMUX_DBG_INFO   2
#define DEMUX_DBG_DEBUG  3
#define DEMUX_DBG_PARSE  4
#define DEMUX_DBG_ALL    6

namespace TSDemux
{
  void DBGLevel(int l);
  void DBGAll(void);
  void DBGNone(void);
  void DBG(int level, const char* fmt, ...);
  void SetDBGMsgCallback(void (*msgcb)(int level, char*));
}

#endif /* TS_DEBUG_H */

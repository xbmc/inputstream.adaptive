/*
 *      Copyright (C) 2015 Jean-Luc Barriere
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
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

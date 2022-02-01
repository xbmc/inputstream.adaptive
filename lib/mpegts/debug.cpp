/*
 *  Copyright (C) 2015 Jean-Luc Barriere
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "debug.h"

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctype.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

typedef struct
{
  const char* name;
  int cur_level;
  void (*msg_callback)(int level, char* msg);
} debug_ctx_t;

static debug_ctx_t debug_ctx = {"TSDemux", DEMUX_DBG_NONE, NULL};

/**
 * Set the debug level to be used for the subsystem
 * \param ctx the subsystem debug context to use
 * \param level the debug level for the subsystem
 * \return an integer subsystem id used for future interaction
 */
static inline void __dbg_setlevel(debug_ctx_t* ctx, int level)
{
  if (ctx != NULL)
  {
    ctx->cur_level = level;
  }
}

/**
 * Generate a debug message at a given debug level
 * \param ctx the subsystem debug context to use
 * \param level the debug level of the debug message
 * \param fmt a printf style format string for the message
 * \param ... arguments to the format
 */
static inline void __dbg(debug_ctx_t* ctx, int level, const char* fmt, va_list ap)
{
  if (ctx != NULL && level <= ctx->cur_level)
  {
    char msg[4096];
    int len = snprintf(msg, sizeof (msg), "(%s)", ctx->name);
    vsnprintf(msg + len, sizeof (msg) - len, fmt, ap);
    if (ctx->msg_callback)
    {
      ctx->msg_callback(level, msg);
    }
    else
    {
      fwrite(msg, strlen(msg), 1, stderr);
    }
  }
}

void TSDemux::DBGLevel(int l)
{
  __dbg_setlevel(&debug_ctx, l);
}

void TSDemux::DBGAll()
{
  __dbg_setlevel(&debug_ctx, DEMUX_DBG_ALL);
}

void TSDemux::DBGNone()
{
  __dbg_setlevel(&debug_ctx, DEMUX_DBG_NONE);
}

void TSDemux::DBG(int level, const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  __dbg(&debug_ctx, level, fmt, ap);
  va_end(ap);
}

void TSDemux::SetDBGMsgCallback(void (*msgcb)(int level, char*))
{
  debug_ctx.msg_callback = msgcb;
}

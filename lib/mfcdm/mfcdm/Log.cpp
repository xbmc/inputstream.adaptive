#include "Log.h"

#include <cstdarg>
#include <cstdio>

typedef struct
{
  const char* name;
  int cur_level;
  void (*msg_callback)(int level, char* msg);
} debug_ctx_t;

static debug_ctx_t debug_ctx = {"MF", MFCDM::MFLOG_NONE, NULL};


static inline void __dbg(debug_ctx_t* ctx, int level, const char* fmt, va_list ap)
{
  if (ctx != NULL && level <= ctx->cur_level)
  {
    char msg[4096];
    int len = snprintf(msg, sizeof(msg), "[%s] ", ctx->name);
    vsnprintf(msg + len, sizeof(msg) - len, fmt, ap);
    if (ctx->msg_callback)
    {
      ctx->msg_callback(level, msg);
    }
  }
}

void MFCDM::LogAll()
{
  debug_ctx.cur_level = MFLOG_ALL;
}

void MFCDM::Log(LogLevel level, const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  __dbg(&debug_ctx, level, fmt, ap);
  va_end(ap);
}

void MFCDM::SetMFMsgCallback(void (*msgcb)(int level, char*))
{
  debug_ctx.msg_callback = msgcb;
}

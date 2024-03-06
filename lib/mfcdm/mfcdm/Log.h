namespace MFCDM
{
enum LogLevel
{
  MFLOG_NONE = -1,
  MFLOG_ERROR,
  MFLOG_WARN,
  MFLOG_INFO,
  MFLOG_DEBUG,
  MFLOG_ALL = 100
};

void LogAll();
void Log(LogLevel level, const char* fmt, ...);
void SetMFMsgCallback(void (*msgcb)(int level, char*));

} // namespace MFCDM

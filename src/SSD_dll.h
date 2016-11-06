#pragma once

//Functionality wich is supported by the Addon
class SSD_HOST
{
public:
  enum CURLOPTIONS
  {
    OPTION_PROTOCOL,
    OPTION_HEADER
  };
  static const uint32_t version = 5;

  virtual const char *GetLibraryPath() const = 0;
  virtual const char *GetProfilePath() const = 0;
  virtual void* CURLCreate(const char* strURL) = 0;
  virtual bool CURLAddOption(void* file, CURLOPTIONS opt, const char* name, const char* value) = 0;
  virtual bool CURLOpen(void* file) = 0;
  virtual size_t ReadFile(void* file, void* lpBuf, size_t uiBufSize) = 0;
  virtual void CloseFile(void* file) = 0;
  virtual bool CreateDirectory(const char *dir) = 0;

  enum LOGLEVEL
  {
    LL_DEBUG,
    LL_INFO,
    LL_ERROR
  };

  virtual void Log(LOGLEVEL level, const char *msg) = 0;
};


//Functionality wich is supported by the Decrypter
class AP4_CencSingleSampleDecrypter;
class AP4_DataBuffer;

class SSD_DECRYPTER
{
public:
  // Return supported URN if type matches to capabikitues, otherwise null
  virtual const char *Supported(const char* licenseType, const char *licenseKey) = 0;
  virtual AP4_CencSingleSampleDecrypter *CreateSingleSampleDecrypter(AP4_DataBuffer &streamCodec) = 0;
};

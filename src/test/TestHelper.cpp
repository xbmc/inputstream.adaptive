#include "TestHelper.h"

std::string testHelper::testFile;
std::string testHelper::effectiveUrl;
std::string testHelper::lastDownloadUrl;

void Log(const LogLevel loglevel, const char* format, ...){}

std::string GetEnv(const std::string& var)
{
  const char* val = std::getenv(var.c_str());
  if (val == nullptr)
    return "";
  else
    return val;
}

void SetFileName(std::string& file, std::string name)
{
  file = GetEnv("DATADIR") + "/" + name;
}

bool adaptive::AdaptiveTree::download(const char* url,
                                      const std::map<std::string, std::string>& manifestHeaders,
                                      void* opaque,
                                      bool scanEffectiveURL)
{
  FILE* f = fopen(testHelper::testFile.c_str(), "rb");
  if (!f)
    return false;

  if (scanEffectiveURL && !testHelper::effectiveUrl.empty())
    SetEffectiveURL(testHelper::effectiveUrl);
 
  // read the file
  static const unsigned int CHUNKSIZE = 16384;
  char buf[CHUNKSIZE];
  size_t nbRead;

  while ((nbRead = fread(buf, 1, CHUNKSIZE, f)) > 0 && ~nbRead && write_data(buf, nbRead, opaque))
    ;

  fclose(f);

  SortTree();
  return nbRead == 0;
}

bool TestAdaptiveStream::download(const char* url,
                                  const std::map<std::string, std::string>& mediaHeaders)
{
  size_t nbRead = ~0UL;
  std::stringstream ss("Sixteen bytes!!!");

  char buf[16];
  size_t nbReadOverall = 0;
  while ((nbRead = ss.readsome(buf, 16)) > 0 && ~nbRead && write_data(buf, nbRead))
    nbReadOverall += nbRead;

  if (!nbReadOverall)
  {
    return false;
  }      
  testHelper::lastDownloadUrl = url;
  return nbRead == 0;
}

void AESDecrypter::decrypt(const AP4_UI08* aes_key,
                           const AP4_UI08* aes_iv,
                           const AP4_UI08* src,
                           AP4_UI08* dst,
                           size_t dataSize){}

std::string AESDecrypter::convertIV(const std::string& input)
{
  std::string result;
  return result;
}

void AESDecrypter::ivFromSequence(uint8_t* buffer, uint64_t sid){}

bool AESDecrypter::RenewLicense(const std::string& pluginUrl){return false;}

DASHTestTree::DASHTestTree(){}

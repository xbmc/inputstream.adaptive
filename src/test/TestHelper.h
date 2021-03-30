#include "../Iaes_decrypter.h"
#include "../log.h"
#include "../parser/DASHTree.h"
#include "../parser/HLSTree.h"


extern std::string testFile;
extern std::string effectiveUrl;
std::string GetEnv(const std::string& var);
void SetFileName(std::string& file, const std::string name);
void Log(const LogLevel loglevel, const char* format, ...);

class AESDecrypter : public IAESDecrypter
{
public:
  AESDecrypter(const std::string& licenseKey) : m_licenseKey(licenseKey){};
  virtual ~AESDecrypter() = default;

  void decrypt(const AP4_UI08* aes_key,
               const AP4_UI08* aes_iv,
               const AP4_UI08* src,
               AP4_UI08* dst,
               size_t dataSize);
  std::string convertIV(const std::string& input);
  void ivFromSequence(uint8_t* buffer, uint64_t sid);
  const std::string& getLicenseKey() const { return m_licenseKey; };
  bool RenewLicense(const std::string& pluginUrl);

private:
  std::string m_licenseKey;
};

class DASHTestTree : public adaptive::DASHTree
{
public:
  uint64_t mock_time = 10000000L;
  DASHTestTree();
  uint64_t GetNowTime() override { return mock_time; }
};

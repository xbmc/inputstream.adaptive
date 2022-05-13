#pragma once

#include <bento4/Ap4Types.h>

#include <string>

class IAESDecrypter
{
public:
  virtual ~IAESDecrypter() {};

  virtual void decrypt(const AP4_UI08* aes_key,
                       const AP4_UI08* aes_iv,
                       const AP4_UI08* src,
                       AP4_UI08* dst,
                       size_t dataSize) = 0;
  virtual std::string convertIV(const std::string& input) = 0;
  virtual void ivFromSequence(uint8_t* buffer, uint64_t sid) = 0;
  virtual const std::string& getLicenseKey() const = 0;
  virtual bool RenewLicense(const std::string& pluginUrl) = 0;

private:
  std::string m_licenseKey;
};
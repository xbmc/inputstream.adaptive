/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../IDecrypter.h"

#include <bento4/Ap4.h>

class CWVCdmAdapter;
class CWVCencSingleSampleDecrypter;

using namespace DRM;
using namespace kodi::tools;


/*********************************************************************************************/

class ATTR_DLL_LOCAL CWVDecrypter : public IDecrypter
{
public:
  CWVDecrypter() : m_WVCdmAdapter(nullptr), m_decodingDecrypter(nullptr){};
  virtual ~CWVDecrypter() override;

  virtual bool Initialize() override;

  virtual std::string SelectKeySytem(std::string_view keySystem) override;
  virtual bool OpenDRMSystem(const char* licenseURL,
                             const std::vector<uint8_t>& serverCertificate,
                             const uint8_t config) override;
  virtual Adaptive_CencSingleSampleDecrypter* CreateSingleSampleDecrypter(
      std::vector<uint8_t>& pssh,
      std::string_view optionalKeyParameter,
      std::string_view defaultKeyId,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override;
  virtual void DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter) override;
  virtual void GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                               std::string_view keyId,
                               uint32_t media,
                               IDecrypter::DecrypterCapabilites& caps) override;
  virtual bool HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                             std::string_view keyId) override;
  virtual bool IsInitialised() override { return m_WVCdmAdapter != nullptr; }
  virtual std::string GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter) override;
  virtual bool OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                const VIDEOCODEC_INITDATA* initData) override;
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override;
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override;
  virtual void ResetVideo() override;
  virtual void SetLibraryPath(const char* libraryPath) override;
  virtual void SetProfilePath(const std::string& profilePath) override;
  virtual void SetDebugSaveLicense(bool isDebugSaveLicense) override
  {
    m_isDebugSaveLicense = isDebugSaveLicense;
  }
  virtual bool GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture);
  virtual void ReleaseBuffer(void* instance, void* buffer);
  virtual const char* GetLibraryPath() const override { return m_strLibraryPath.c_str(); }
  virtual const char* GetProfilePath() const override { return m_strProfilePath.c_str(); }
  virtual const bool IsDebugSaveLicense() const override { return m_isDebugSaveLicense; }

private:
  CWVCdmAdapter* m_WVCdmAdapter;
  CWVCencSingleSampleDecrypter* m_decodingDecrypter;
  std::string m_strProfilePath;
  std::string m_strLibraryPath;
  bool m_isDebugSaveLicense;
  void* m_hdlLibLoader{nullptr}; // Aarch64 loader library handle
};

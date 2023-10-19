/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../IDecrypter.h"
#include "mfcdm/MediaFoundationCdm.h"

using namespace DRM;
using namespace kodi::tools;

class MediaFoundationCdm;
class CMFCencSingleSampleDecrypter;

/*********************************************************************************************/

class ATTR_DLL_LOCAL CMFDecrypter : public IDecrypter
{
public:
  CMFDecrypter();
  ~CMFDecrypter() override;

  bool Initialize() override;

  std::string SelectKeySytem(std::string_view keySystem) override;

  bool OpenDRMSystem(std::string_view licenseURL,
                     const std::vector<uint8_t>& serverCertificate,
                     const uint8_t config) override;

  Adaptive_CencSingleSampleDecrypter* CreateSingleSampleDecrypter(
      std::vector<uint8_t>& pssh,
      std::string_view optionalKeyParameter,
      std::string_view defaultKeyId,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override;

  void DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter) override;

  void GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                       std::string_view keyId,
                       uint32_t media,
                       IDecrypter::DecrypterCapabilites& caps) override;

  bool HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                     std::string_view keyId) override;

  std::string GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter) override;

  virtual bool OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                const VIDEOCODEC_INITDATA* initData) override;

  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override;
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override;
  virtual void ResetVideo() override;

  void SetLibraryPath(const char* libraryPath) override {};
  void SetProfilePath(const std::string& profilePath) override;
  bool IsInitialised() override
  {
    if (!m_cdm)
      return false;
    return m_cdm->IsInitialized();
  }

  void SetDebugSaveLicense(bool isDebugSaveLicense) override
  {
    m_isDebugSaveLicense = isDebugSaveLicense;
  }

  const bool IsDebugSaveLicense() const override { return m_isDebugSaveLicense; }
  const char* GetLibraryPath() const override { return m_strLibraryPath.c_str(); }
  const char* GetProfilePath() const override { return m_strProfilePath.c_str(); }

  MediaFoundationCdm* GetCdm() const { return m_cdm; }

private:
  MediaFoundationCdm* m_cdm;
  CMFCencSingleSampleDecrypter* m_decodingDecrypter;

  std::string m_strProfilePath;
  std::string m_strLibraryPath;

  bool m_isDebugSaveLicense;
};

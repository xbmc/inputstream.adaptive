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

  virtual std::vector<std::string_view> SelectKeySystems(std::string_view keySystem) override;
  virtual bool OpenDRMSystem(std::string_view licenseURL,
                             const std::vector<uint8_t>& serverCertificate,
                             const uint8_t config) override;
  virtual Adaptive_CencSingleSampleDecrypter* CreateSingleSampleDecrypter(
      std::vector<uint8_t>& initData,
      std::string_view optionalKeyParameter,
      const std::vector<uint8_t>& defaultKeyId,
      std::string_view licenseUrl,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override;
  virtual void DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter) override;
  virtual void GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                               const std::vector<uint8_t>& keyId,
                               uint32_t media,
                               DecrypterCapabilites& caps) override;
  virtual bool HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                             const std::vector<uint8_t>& keyId) override;
  virtual bool IsInitialised() override { return m_WVCdmAdapter != nullptr; }
  virtual std::string GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter) override;
  virtual bool OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                const VIDEOCODEC_INITDATA* initData) override;
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override;
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override;
  virtual void ResetVideo() override;
  virtual void SetLibraryPath(std::string_view libraryPath) override;
  virtual bool GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture);
  virtual void ReleaseBuffer(void* instance, void* buffer);
  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

private:
  CWVCdmAdapter* m_WVCdmAdapter;
  CWVCencSingleSampleDecrypter* m_decodingDecrypter;
  std::string m_libraryPath;
  void* m_hdlLibLoader{nullptr}; // Aarch64 loader library handle
};

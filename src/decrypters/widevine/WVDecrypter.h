/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "decrypters/DrmEngineDefines.h"
#include "decrypters/IDecrypter.h"

class CWVCdmAdapter;
class CWVCencSingleSampleDecrypter;

class ATTR_DLL_LOCAL CWVDecrypter : public DRM::IDecrypter
{
public:
  CWVDecrypter() = default;
  virtual ~CWVDecrypter() override;

  virtual const std::string GetName() const override { return "Widevine-CDM"; }

  virtual bool IsKeySystemSupported(std::string_view keySystem) override;

  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      const DRM::Config& config,
      const std::vector<uint8_t>& defaultKeyId,
      CryptoMode cryptoMode) override;

  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyId,
                               DRM::Capabilities& caps,
                               DRM::DRMMediaType mediaType) override;
  virtual std::optional<bool> HasLicenseKey(
      std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
      const std::vector<uint8_t>& keyId) override;

  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) override;
  virtual bool OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const VIDEOCODEC_INITDATA* initData) override;
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override;
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override;
  virtual void ResetVideo() override;
  virtual void DisposeDecoder() override;
  virtual void SetLibraryPath(std::string_view libraryPath) override;
  virtual bool GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture);
  virtual void ReleaseBuffer(void* instance, void* buffer);
  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

#ifdef TARGET_WEBOS
  virtual bool IsSecureDecoderAudioSupported() override { return true; }
#else
  //! @todo: Secure path for audio is not implemented
  virtual bool IsSecureDecoderAudioSupported() override { return false; }
#endif

private:
  std::shared_ptr<CWVCdmAdapter> m_WVCdmAdapter;
  std::shared_ptr<CWVCencSingleSampleDecrypter> m_decodingDecrypter;
  std::string m_libraryPath;
};

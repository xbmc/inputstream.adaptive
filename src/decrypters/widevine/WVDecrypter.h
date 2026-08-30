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

  virtual bool Initialize() override;

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
  virtual bool OpenAudioDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const AUDIOCODEC_INITDATA* initData) override;
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override;
  virtual AUDIOCODEC_RETVAL DecryptAndDecodeAudio(kodi::addon::CInstanceAudioCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override;

  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override;
  virtual AUDIOCODEC_RETVAL AudioFrameDataToFrame(kodi::addon::CInstanceAudioCodec* codecInstance,
                                                  AUDIOCODEC_FRAME* frame) override;
  virtual void ResetVideo() override;
  virtual void ResetAudio() override;
  virtual void DisposeDecoder() override;
  virtual void SetLibraryPath(std::string_view libraryPath) override;
  virtual bool GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture);
  virtual void ReleaseBuffer(void* instance, void* buffer);
  virtual bool GetBufferAudio(void* instance, AUDIOCODEC_FRAME& buffer);
  virtual void ReleaseBufferAudio(void* instance, void* buffer);

  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

private:
  std::shared_ptr<CWVCdmAdapter> m_WVCdmAdapter;
  std::shared_ptr<CWVCencSingleSampleDecrypter> m_decodingDecrypter;
  std::shared_ptr<CWVCencSingleSampleDecrypter> m_decodingDecrypterAudio;
  std::string m_libraryPath;
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
  void* m_hdlLibLoader{nullptr}; // Aarch64 loader library handle
#endif
};

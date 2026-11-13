/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cdm/media/cdm/api/content_decryption_module.h"
#include "common/AdaptiveCencSampleDecrypter.h"
#include "decrypters/HelperWv.h"
#include "decrypters/IDecrypter.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace media
{
class CdmAdapter;
class CdmVideoFrame;
}

using namespace DRM;

class ATTR_DLL_LOCAL CWVCencSingleSampleDecrypter : public Adaptive_CencSingleSampleDecrypter,
                                                    public IWVObserver
{
public:
  CWVCencSingleSampleDecrypter(IWVCdmAdapter<media::CdmAdapter>* cdmAdapter,
                               const std::vector<uint8_t>& defaultKeyId,
                               CryptoMode cryptoMode);
  virtual ~CWVCencSingleSampleDecrypter();

  virtual SResult CreateSession(const std::vector<uint8_t>& pssh,
                                std::string_view licenseUrl,
                                bool skipSessionMessage) override;

  void GetCapabilities(const std::vector<uint8_t>& keyId,
                       Capabilities& caps,
                       DRMMediaType mediaType);
  virtual std::string GetSessionId() override;
  void CloseSessionId();
  AP4_DataBuffer GetChallengeData();

  void SetSession(const std::string sessionId, const uint8_t* data, const size_t dataSize);

  bool HasKeyId(const std::vector<uint8_t>& keyid);

  virtual AP4_Result SetFragmentInfo(AP4_UI32 poolId,
                                     const std::vector<uint8_t>& keyId,
                                     const AP4_UI08 nalLengthSize,
                                     const std::vector<uint8_t>& annexbSpsPps,
                                     DRM::Capabilities caps,
                                     CryptoInfo cryptoInfo) override;
  virtual AP4_UI32 AddPool() override;
  virtual void RemovePool(AP4_UI32 poolId) override;

  virtual AP4_Result DecryptSampleData(
      AP4_UI32 poolId,
      AP4_DataBuffer& dataIn,
      AP4_DataBuffer& dataOut,

      // always 16 bytes
      const AP4_UI08* iv,

      // pass 0 for full decryption
      unsigned int subsampleCount,

      // array of <subsample_count> integers. NULL if subsample_count is 0
      const AP4_UI16* bytesOfCleartextData,

      // array of <subsample_count> integers. NULL if subsample_count is 0
      const AP4_UI32* bytesOfEncryptedData,

      DRM::DRMMediaType streamType) override;

  bool OpenVideoDecoder(const VIDEOCODEC_INITDATA* initData);
  VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                          const DEMUX_PACKET* sample);
  VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                            VIDEOCODEC_PICTURE* picture);
  void ResetVideo();
  void SetDefaultKeyId(const std::vector<uint8_t>& keyId) override;
  void AddKeyId(const std::vector<uint8_t>& keyId) override;

  const uint8_t* GetRenewalSystemId() const override;
  void RenewSessionForKey(const std::vector<uint8_t>& keyId,
                          const std::vector<uint8_t>& psshData,
                          bool isSeigKid) override;
  bool WaitForKey(const std::vector<uint8_t>& keyId, std::chrono::milliseconds timeout) override;

  // IWVObserver interface
  void OnNotify(const CdmMessage& message) override;
  void OnKeyStatusChangeNotify(const std::string& sessionId,
                               const std::vector<DRM::KeyInfo>& keysInfo) override;

private:
  void CheckLicenseRenewal();
  bool SendSessionMessage();
  bool HasChallenge();

  // In-band key rotation, a license request with the PSSH of a media segment
  struct RenewalRequest
  {
    std::vector<uint8_t> keyId;
    std::vector<uint8_t> psshData;
    int attempts{0};
    std::chrono::steady_clock::time_point notBefore;
  };
  enum class PsshState
  {
    QUEUED, // Waiting for or in a license request
    DONE,
    GIVEN_UP,
  };
  struct RequestedPssh
  {
    std::vector<uint8_t> psshData;
    PsshState state{PsshState::QUEUED};
    std::chrono::steady_clock::time_point time; // When it has been given up
    // KIDs of the fragments waiting for this license, while it is queued
    std::vector<std::vector<uint8_t>> pendingKids;
  };
  struct PreviousSession
  {
    std::string sessionId;
    std::vector<std::vector<uint8_t>> kids;
  };
  enum class SessionKind
  {
    CURRENT, // m_strSession, or any session when m_strSession is empty (first session)
    PREVIOUS, // The session replaced by the last in-band renewal
    OTHER,
  };

  SessionKind GetSessionKindLocked(const std::string& sessionId) const;
  bool IsKeyRotation(const std::vector<uint8_t>& keyId, bool hasCryptoPeriodIndex, bool canStart);
  bool HasUsableKey(const std::vector<uint8_t>& keyId);
  // The license has granted the key, it can be also restricted or usable in the future
  bool HasGrantedKey(const std::vector<uint8_t>& keyId);
  bool IsRenewalKeyReady(const std::vector<uint8_t>& keyId);
  bool IsRenewalPending(const std::vector<uint8_t>& keyId);
  bool IsKidPendingLocked(const std::vector<uint8_t>& keyId) const;
  void SetPsshStateLocked(const std::vector<uint8_t>& psshData, PsshState state);
  void AddPendingKidLocked(const std::vector<uint8_t>& psshData, const std::vector<uint8_t>& keyId);
  bool StartRenewalThreadLocked();
  void RenewalThreadLoop();
  bool DoRenewSession(const RenewalRequest& request);
  bool WaitRenewalKey(const std::vector<uint8_t>& keyId);
  std::string WaitSessionFuture(std::future<std::string>& sessionFut);
  void CloseCdmSession(const std::string& sessionId);
  // isClosedSession: the keys of a closed session are remembered, see TakeRemovedKid
  void RemoveSessionKeys(const std::vector<std::vector<uint8_t>>& sessionKids,
                         const std::vector<std::vector<uint8_t>>& keepKids,
                         bool isClosedSession);
  // True, once, when the key was removed with its closed session
  bool TakeRemovedKid(const std::vector<uint8_t>& keyId);

  IWVCdmAdapter<media::CdmAdapter>* m_cdmAdapter;
  std::string m_strSession;
  std::vector<uint8_t> m_pssh;
  AP4_DataBuffer m_challenge;
  std::vector<uint8_t> m_defaultKeyId;
  std::vector<DRM::KeyInfo> m_keys;
  // KIDs reported by the CDM for m_strSession
  std::vector<std::vector<uint8_t>> m_currentSessionKids;
  std::vector<std::vector<uint8_t>> m_removedKids; // Keys removed with their closed session
  // Guards m_keys, m_currentSessionKids and m_removedKids, written on the CDM callbacks.
  // It can be taken while holding m_renewalLock, never the opposite
  std::mutex m_keysLock;

  uint16_t m_hdcpVersion{DRM::HDCP_V_MAX};
  int m_hdcpLimit{0};
  int m_resolutionLimit{0};

  AP4_DataBuffer m_decryptIn;
  AP4_DataBuffer m_decryptOut;

  struct FINFO
  {
    std::vector<uint8_t> m_key;
    AP4_UI08 m_nalLengthSize;
    DRM::Capabilities capabilities;
    std::vector<uint8_t> m_annexbSpsPps;
    CryptoInfo m_cryptoInfo;
  };
  std::vector<FINFO> m_fragmentPool;
  void LogDecryptError(const cdm::Status status, const std::vector<uint8_t>& keyId);
  void SetCdmSubsamples(std::vector<cdm::SubsampleEntry>& subsamples, bool isCbc);
  void RepackSubsampleData(AP4_DataBuffer& dataIn,
                           AP4_DataBuffer& dataOut,
                           size_t& startPos,
                           size_t& cipherPos,
                           const size_t subsamplePos,
                           const AP4_UI16* bytesOfCleartextData,
                           const AP4_UI32* bytesOfEncryptedData);
  void UnpackSubsampleData(AP4_DataBuffer& dataIn,
                           size_t& startPos,
                           const size_t subsamplePos,
                           const AP4_UI16* bytesOfCleartextData,
                           const AP4_UI32* bytesOfEncryptedData);
  void SetInput(cdm::InputBuffer_2& cdmInputBuffer,
                const AP4_DataBuffer& inputData,
                const unsigned int subsampleCount,
                const uint8_t* iv,
                const FINFO& fragInfo,
                const std::vector<cdm::SubsampleEntry>& subsamples);
  AP4_Result ConvertToAnnexBandInject(AP4_DataBuffer& dataIn,
                                      AP4_DataBuffer& dataOut,
                                      unsigned int subsampleCount,
                                      FINFO& fragInfo,
                                      const AP4_UI08* iv,
                                      const AP4_UI16* bytesOfCleartextData,
                                      const AP4_UI32* bytesOfEncryptedData,
                                      bool convertAnnexB,
                                      std::vector<cdm::SubsampleEntry>& rebuiltSubs);

  bool m_isDrained;

  std::list<media::CdmVideoFrame> m_videoFrames;
  // guards m_strSession, m_challenge, m_pssh, m_defaultKeyId and the members below
  std::mutex m_renewalLock;
  // An in-band renewal opens a new session, its message is kept here, not in m_challenge
  bool m_isRenewalSessionPending{false};
  std::vector<std::pair<std::string, std::vector<uint8_t>>> m_renewalMessages;
  PreviousSession m_previousSession;
  bool m_isKeyRotation{false};
  // Serializes the license requests (SendSessionMessage), the decrypter is shared
  // by the audio and video streams
  std::mutex m_sessionActionLock;
  CryptoMode m_EncryptionMode;

  std::optional<cdm::VideoDecoderConfig_3> m_currentVideoDecConfig;
  bool m_isTestingCapabilities{false};

  // In-band renewal worker. m_renewalQueueLock guards the members below, it can be held
  // while taking m_keysLock, never the opposite
  std::mutex m_renewalQueueLock;
  std::condition_variable m_renewalQueueCv; // New request or stop
  std::condition_variable m_keyCv; // Key status changed, request ended or stop
  std::deque<RenewalRequest> m_renewalQueue;
  std::deque<RequestedPssh> m_requestedPssh;
  std::vector<std::vector<uint8_t>> m_noPsshWarnedKids;
  std::atomic<bool> m_renewalStop{false};
  std::thread m_renewalThread;
};

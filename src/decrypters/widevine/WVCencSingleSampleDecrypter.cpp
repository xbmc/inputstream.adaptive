/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVCencSingleSampleDecrypter.h"

#include "CompSettings.h"
#include "SrvBroker.h"
#include "CdmDecryptedBlock.h"
#include "CdmFixedBuffer.h"
#include "CdmTypeConversion.h"
#include "WVCdmAdapter.h"
#include "cdm/media/cdm/cdm_adapter.h"
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/FileUtils.h"
#include "utils/GUIUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <algorithm>
#include <mutex>
#include <system_error>
#include <thread>

using namespace UTILS;

namespace
{
#ifdef TARGET_WEBOS
constexpr size_t DECRYPTION_SVP_MARGIN = 64;
#endif

// In-band key rotation (license renewal with the PSSH of the media segments)
// Attempts of a license request, then the PSSH is given up
constexpr int RENEWAL_MAX_ATTEMPTS = 3;
// Pause before retrying a failed request, multiplied by the attempt number
constexpr std::chrono::seconds RENEWAL_RETRY_DELAY{1};
// A PSSH given up, or received without the key, can be requested again after this time
constexpr std::chrono::seconds RENEWAL_RETRY_AFTER{30};
// Max wait for the CDM to open the new session
constexpr std::chrono::seconds RENEWAL_SESSION_WAIT{10};
constexpr std::chrono::milliseconds RENEWAL_SESSION_WAIT_STEP{100};
// Max wait for the keys after the license update
constexpr std::chrono::seconds RENEWAL_KEY_WAIT{2};
// Number of requested PSSH kept to not request them again
constexpr size_t RENEWAL_PSSH_HISTORY = 8;
// Max session messages kept while the renewal session is being opened
constexpr size_t RENEWAL_MAX_MESSAGES = 4;
// Max KIDs kept in each list (warned for the missing PSSH, waiting for a license, removed keys)
constexpr size_t RENEWAL_MAX_KIDS = 16;

// The CDM adapter keeps the pending session promises by their ID, the decrypters of the
// streams share the adapter and their in-band renewals can open sessions at the same time,
// so the IDs must be unique among all the decrypters
std::atomic<uint32_t> g_promiseId{1};

uint32_t NextPromiseId()
{
  return g_promiseId++;
}

bool IsKeyGranted(DRM::KeyStatus status)
{
  // A restricted key (e.g. HDCP) is still a key the license has granted,
  // requesting the license again does not change it. Also a key of a future crypto period
  return status == DRM::KeyStatus::USABLE || status == DRM::KeyStatus::OUTPUT_NOT_ALLOWED ||
         status == DRM::KeyStatus::USABLE_IN_FUTURE;
}

bool ContainsKid(const std::vector<std::vector<uint8_t>>& kids, const std::vector<uint8_t>& kid)
{
  return std::find(kids.cbegin(), kids.cend(), kid) != kids.cend();
}

cdm::StreamType ToCdmStreamType(DRM::DRMMediaType stream_type)
{
  switch (stream_type)
  {
    case DRM::DRMMediaType::AUDIO:
      return cdm::StreamType::kStreamTypeAudio;
    case DRM::DRMMediaType::VIDEO:
      return cdm::StreamType::kStreamTypeVideo;
    default:
      return cdm::StreamType::kStreamTypeVideo;
  }
}
} // unnamed namespace

void CWVCencSingleSampleDecrypter::SetSession(const std::string sessionId,
                                              const uint8_t* data,
                                              const size_t dataSize)
{
  std::lock_guard<std::mutex> lock(m_renewalLock);

  m_strSession = sessionId;
  m_challenge.SetData(data, static_cast<AP4_Size>(dataSize));
  LOG::LogF(LOGDEBUG, "Opened widevine session ID: %s", m_strSession.c_str());
}

CWVCencSingleSampleDecrypter::CWVCencSingleSampleDecrypter(
    IWVCdmAdapter<media::CdmAdapter>* cdmAdapter,
    const std::vector<uint8_t>& defaultKeyId,
    CryptoMode cryptoMode)
  : m_cdmAdapter(cdmAdapter),
    m_defaultKeyId(defaultKeyId),
    m_isDrained(true),
    m_EncryptionMode(cryptoMode)
{
  SetParentIsOwner(false);

  m_cdmAdapter->AttachObserver(this);
}

CWVCencSingleSampleDecrypter::~CWVCencSingleSampleDecrypter()
{
  // This decrypter can be used/shared with more streams "sessions"
  // since it is used wrapped in a shared_ptr the destructor will be called only
  // when the last stream "session" will be deleted, and so it can close the CDM session.

  // Stop the in-band renewal worker first, it must not use the session state while it goes
  // away. A license HTTP request in progress is not interrupted, the join waits for its end
  {
    std::lock_guard<std::mutex> lock(m_renewalQueueLock);
    m_renewalStop = true;
  }
  m_renewalQueueCv.notify_all();
  m_keyCv.notify_all();
  if (m_renewalThread.joinable())
    m_renewalThread.join();

  CloseSessionId();

  std::string previousSession;
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    previousSession = std::move(m_previousSession.sessionId);
    m_previousSession = {};
  }
  if (!previousSession.empty())
    CloseCdmSession(previousSession);

  m_cdmAdapter->DetachObserver(this);
}

SResult CWVCencSingleSampleDecrypter::CreateSession(const std::vector<uint8_t>& pssh,
                                                    std::string_view licenseUrl,
                                                    bool skipSessionMessage)
{
  if (m_cdmAdapter->GetConfig().license.serverUri.empty())
  {
    LOG::LogF(LOGERROR, "The DRM license server url has not been configured");
    return SResult::Error(GUI::GetLocalizedString(30306));
  }

  if (pssh.size() < 4 || pssh.size() > 4096)
  {
    LOG::LogF(LOGERROR, "Cannot request license, PSSH init data has unexpected size (%zu)",
              pssh.size());
    return SResult::Error(GUI::GetLocalizedString(30303));
  }

  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    m_pssh = pssh;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(),
                                                     "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.init");
    UTILS::FILESYS::SaveFile(debugFilePath, {pssh.cbegin(), pssh.cend()}, true);
  }

  const uint32_t pid = NextPromiseId();

  // prepare the promise before calling into CDM
  std::future<std::string> sessionFut = m_cdmAdapter->GetCDM()->PrepareSessionFuture(pid);

  m_cdmAdapter->GetCDM()->CreateSessionAndGenerateRequest(pid, cdm::SessionType::kTemporary,
                                                          cdm::InitDataType::kCenc, pssh.data(),
                                                          static_cast<uint32_t>(pssh.size()));

  // block until CDM resolves or rejects the promise
  std::string sessionId;
  try
  {
    sessionId = sessionFut.get(); // throws if CDM rejected
  }
  catch (const std::exception& e)
  {
    LOG::LogF(LOGERROR, "Session creation failed: %s", e.what());
    return SResult::Error(GUI::GetLocalizedString(30303));
  }

  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    m_strSession = sessionId;
  }

  if (sessionId.empty())
  {
    LOG::LogF(LOGERROR, "No session ID available");
    return SResult::Error(GUI::GetLocalizedString(30303));
  }

  // The in-band renewal does not request again the PSSH of this session
  DRM::PSSH psshParser;
  if (psshParser.Parse(pssh) && !psshParser.GetInitData().empty())
  {
    std::lock_guard<std::mutex> lock(m_renewalQueueLock);
    SetPsshStateLocked(psshParser.GetInitData(), PsshState::DONE);
  }

  if (skipSessionMessage)
    return SResult::Ok();

  std::lock_guard<std::mutex> action(m_sessionActionLock);
  //! @todo: this loop is not so clear
  while (HasChallenge() && SendSessionMessage())
    ;

  return SResult::Ok();
}

void CWVCencSingleSampleDecrypter::GetCapabilities(const std::vector<uint8_t>& keyId,
                                                   Capabilities& caps,
                                                   DRMMediaType mediaType)
{
  caps = {0, m_hdcpVersion, m_hdcpLimit};

  if (GetSessionId().empty())
  {
    LOG::LogF(LOGDEBUG, "Session empty");
    caps.flags = Capabilities::INVALID_STATUS;
    return;
  }

  caps.flags = Capabilities::SUPPORTS_DECODING;

  std::vector<uint8_t> firstKid;
  {
    std::lock_guard<std::mutex> lock(m_keysLock);
    if (m_keys.empty())
    {
      LOG::LogF(LOGDEBUG, "Keys empty");
      caps.flags = Capabilities::INVALID_STATUS;
      return;
    }
    firstKid = m_keys.front().kid;
  }

  if (!caps.hdcpLimit)
    caps.hdcpLimit = m_resolutionLimit;

#ifdef TARGET_WEBOS
  LOG::LogF(LOGDEBUG, "Overriding settings to: SECURE_PATH | ANNEXB_REQUIRED | SECURE_DECODER");
  caps = {DRM::Capabilities::SECURE_PATH | DRM::Capabilities::ANNEXB_REQUIRED,
          DRM::Capabilities::SECURE_DECODER};
  caps.hdcpVersion = DRM::HDCP_V_MAX;
  return;
#endif

  if (caps.HasFlag(Capabilities::SUPPORTS_DECODING))
  {
    AP4_UI32 poolId(AddPool());
    m_fragmentPool[poolId].m_key = keyId.empty() ? firstKid : keyId;
    m_fragmentPool[poolId].m_cryptoInfo.m_mode = m_EncryptionMode;

    AP4_DataBuffer in;
    AP4_DataBuffer out;
    AP4_UI32 encryptedBytes[2] = {1, 1};
    AP4_UI16 clearBytes[2] = {5, 5};
    AP4_Byte testData[12] = {0, 0, 0, 1, 9, 255, 0, 0, 0, 1, 10, 255};
    const AP4_UI08 iv[] = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0};
    in.SetBuffer(testData, 12);
    in.SetDataSize(12);

    try
    {
      encryptedBytes[0] = 12;
      clearBytes[0] = 0;

      // Ignore callbacks to OnKeyStatusChangeNotify when testing capabilities,
      // when the decryption fails CDM trigger a key status change with keys as unusable but this is expected
      m_isTestingCapabilities = true;

      if (DecryptSampleData(poolId, in, out, iv, 1, clearBytes, encryptedBytes, mediaType) !=
          AP4_SUCCESS)
      {
        LOG::LogF(LOGDEBUG, "Single decrypt failed, secure path only");
        caps.flags |= (Capabilities::SECURE_PATH | Capabilities::ANNEXB_REQUIRED);
      }
      else
      {
        LOG::LogF(LOGDEBUG, "Single decrypt possible");
        caps.flags |= Capabilities::SINGLE_DECRYPT;
        caps.hdcpVersion = DRM::HDCP_V_MAX;
        caps.hdcpLimit = m_resolutionLimit;
      }

      m_isTestingCapabilities = false;
    }
    catch (const std::exception& e)
    {
      LOG::LogF(LOGDEBUG, "Decrypt error, assuming secure path: %s", e.what());
      caps.flags |= (Capabilities::SECURE_PATH | Capabilities::ANNEXB_REQUIRED);
    }
    RemovePool(poolId);
  }
  else
  {
    LOG::LogF(LOGDEBUG, "Decoding not supported");
    caps.flags = Capabilities::INVALID_STATUS;
  }
}

std::string CWVCencSingleSampleDecrypter::GetSessionId()
{
  std::lock_guard<std::mutex> lock(m_renewalLock);
  return m_strSession;
}

CWVCencSingleSampleDecrypter::SessionKind CWVCencSingleSampleDecrypter::GetSessionKindLocked(
    const std::string& sessionId) const
{
  // An empty m_strSession matches, so a decrypter opening its first session receives its messages
  if (m_strSession.empty() || m_strSession == sessionId)
    return SessionKind::CURRENT;
  if (!m_previousSession.sessionId.empty() && m_previousSession.sessionId == sessionId)
    return SessionKind::PREVIOUS;
  return SessionKind::OTHER;
}

void CWVCencSingleSampleDecrypter::CloseSessionId()
{
  // m_renewalLock is not held while calling the CDM, the CDM callbacks take it
  const std::string sessionId = GetSessionId();
  if (!sessionId.empty())
  {
    LOG::LogF(LOGDEBUG, "Closing widevine session ID: %s", sessionId.c_str());
    m_cdmAdapter->GetCDM()->CloseSession(NextPromiseId(), sessionId.data(),
                                         static_cast<uint32_t>(sessionId.size()));

    LOG::LogF(LOGDEBUG, "Widevine session ID %s closed", sessionId.c_str());
    std::lock_guard<std::mutex> lock(m_renewalLock);
    if (m_strSession == sessionId)
      m_strSession.clear();
  }
}

AP4_DataBuffer CWVCencSingleSampleDecrypter::GetChallengeData()
{
  std::lock_guard<std::mutex> lock(m_renewalLock);
  return m_challenge;
}

void CWVCencSingleSampleDecrypter::CheckLicenseRenewal()
{
  // Called before every decrypt, by the audio and the video decoding threads.
  // Only try the lock: while another thread sends the license request, skip this round,
  // blocking would stall the decoding for the whole license HTTP round-trip
  std::unique_lock<std::mutex> action(m_sessionActionLock, std::try_to_lock);
  if (!action.owns_lock() || !HasChallenge())
    return;
  SendSessionMessage();
}

bool CWVCencSingleSampleDecrypter::HasChallenge()
{
  std::lock_guard<std::mutex> lock(m_renewalLock);
  return m_challenge.GetDataSize() > 0;
}

bool CWVCencSingleSampleDecrypter::SendSessionMessage()
{
  // The session state is also written by the CDM callbacks, work on a snapshot
  std::vector<uint8_t> challenge;
  std::string sessionId;
  std::vector<uint8_t> defaultKeyId;
  std::vector<uint8_t> pssh;
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    challenge.assign(m_challenge.GetData(), m_challenge.GetData() + m_challenge.GetDataSize());
    sessionId = m_strSession;
    defaultKeyId = m_defaultKeyId;
    pssh = m_pssh;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_cdmAdapter->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge");
    std::string data{challenge.cbegin(), challenge.cend()};
    UTILS::FILESYS::SaveFile(debugFilePath, data, true);
  }

  const DRM::Config drmCfg = m_cdmAdapter->GetConfig();
  const DRM::Config::License& licConfig = drmCfg.license;
  std::string reqData;

  if (!licConfig.isHttpGetRequest) // Make HTTP POST request
  {
    if (licConfig.reqData.empty()) // By default add raw challenge
    {
      reqData.assign(challenge.cbegin(), challenge.cend());
    }
    else
    {
      if (BASE64::IsValidBase64(licConfig.reqData))
        reqData = BASE64::DecodeToStr(licConfig.reqData);
      else //! @todo: this fallback as plain text must be removed when the deprecated DRM properties are removed, and so replace it to return error
        reqData = licConfig.reqData;

      // Some services have a customized license server that require data to be wrapped with their formats (e.g. JSON).
      // Here we provide a built-in way to customize the license data to be sent, this avoid force add-ons to integrate
      // an HTTP server proxy to manage the license data request/response, and so use Kodi properties to set wrappers.
      if (m_cdmAdapter->GetKeySystem() == DRM::KS_WIDEVINE &&
          !DRM::WvWrapLicense(reqData, challenge, sessionId, defaultKeyId, pssh, licConfig.wrapper,
                              drmCfg.isNewConfig))
      {
        return false;
      }
    }
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_cdmAdapter->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.request");
    UTILS::FILESYS::SaveFile(debugFilePath, reqData, true);
  }

  std::string url = licConfig.serverUri;
  DRM::TranslateLicenseUrlPh(url, challenge, drmCfg.isNewConfig);

  CURL::CUrl cUrl{url, reqData};
  cUrl.AddHeaders(licConfig.reqHeaders);

  const int statusCode = cUrl.Open();

  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::Log(LOGERROR, "License server returned failure (HTTP error %i)", statusCode);
    return false;
  }

  std::string respData;
  if (cUrl.Read(respData) == CURL::ReadStatus::ERROR)
  {
    LOG::LogF(LOGERROR, "Cannot read license server response");
    return false;
  }

  const std::string resLimit = cUrl.GetResponseHeader("X-Limit-Video"); // Custom header
  const std::string respContentType = cUrl.GetResponseHeader("Content-Type");

  if (m_cdmAdapter->GetKeySystem() == DRM::KS_WIDEVINE)
  {
    // Force limit playable streams resolutions
    m_resolutionLimit = DRM::ParseXLimitVideoHeader(resLimit);
  }

  // The first request could be the license certificate request
  // this request is done by sending a challenge of 2 bytes, 0x08 0x04 (CAQ=)
  //! @todo: compare data not only by size but also by bytes
  const bool isCertRequest = challenge.size() == 2 && respContentType == "application/octet-stream";
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    m_challenge.SetDataSize(0);
  }

  // Unwrap license response
  if (!licConfig.unwrapper.empty() && m_cdmAdapter->GetKeySystem() == DRM::KS_WIDEVINE)
  {
    std::string unwrappedData;
    // Some services have a customized license server that require data to be wrapped with their formats (e.g. JSON).
    // Here we provide a built-in way to unwrap the license data received, this avoid force add-ons to integrate
    // a HTTP server proxy to manage the license data request/response, and so use Kodi properties to set wrappers.
    if (!DRM::WvUnwrapLicense(licConfig.unwrapper, licConfig.unwrapperParams, respContentType,
                              respData, unwrappedData, m_hdcpLimit, m_hdcpVersion))
    {
      return false;
    }
    respData = unwrappedData;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(),
                                                     "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED");
    if (isCertRequest)
      debugFilePath += ".cert.response";
    else
      debugFilePath += ".response";

    FILESYS::SaveFile(debugFilePath, respData, true);
  }

  m_cdmAdapter->GetCDM()->UpdateSession(
      NextPromiseId(), sessionId.data(), static_cast<uint32_t>(sessionId.size()),
      reinterpret_cast<const uint8_t*>(respData.c_str()), static_cast<uint32_t>(respData.size()));

  bool haveKeys;
  {
    std::lock_guard<std::mutex> lock(m_keysLock);
    haveKeys = !m_keys.empty();
  }
  if (!haveKeys)
  {
    LOG::LogF(LOGERROR, "License update not successful (no keys)");
    CloseSessionId();
    return false;
  }

  LOG::Log(LOGDEBUG, "License update successful");
  return true;
}

void CWVCencSingleSampleDecrypter::OnNotify(const CdmMessage& message)
{
  if (message.type != CdmMessageType::SESSION_MESSAGE)
    return;

  std::lock_guard<std::mutex> lock(m_renewalLock);

  // The session opened by an in-band renewal: keep its message apart, the current session
  // stays in use until the license of the new one is received
  if (m_isRenewalSessionPending && message.sessionId != m_strSession &&
      message.sessionId != m_previousSession.sessionId)
  {
    if (m_renewalMessages.size() < RENEWAL_MAX_MESSAGES)
      m_renewalMessages.emplace_back(message.sessionId, message.data);
    return;
  }

  if (!m_strSession.empty() && m_strSession != message.sessionId)
    return;

  m_strSession = message.sessionId;
  m_challenge.SetData(message.data.data(), static_cast<AP4_Size>(message.data.size()));
  LOG::LogF(LOGDEBUG, "Opened widevine session ID: %s", m_strSession.c_str());
}

void CWVCencSingleSampleDecrypter::OnKeyStatusChangeNotify(const std::string& sessionId, const std::vector<DRM::KeyInfo>& keysInfo)
{
  if (m_isTestingCapabilities)
    return;

  // Updates the local status of session keys
  //! @todo: this code does not check keys that are no longer listed in the session, should to be removed

  // Runs on the CDM callback thread, the readers are on the demux and decoding threads.
  // An in-band renewal switches the session and its KIDs holding both locks, in this order
  {
    std::lock_guard<std::mutex> sessionLock(m_renewalLock);
    const SessionKind sessionKind = GetSessionKindLocked(sessionId);
    if (sessionKind == SessionKind::OTHER)
      return;

    std::lock_guard<std::mutex> lock(m_keysLock);
    for (const DRM::KeyInfo& keyInfo : keysInfo)
    {
      // A key also in the current session keeps the status of the current session
      if (sessionKind == SessionKind::PREVIOUS && ContainsKid(m_currentSessionKids, keyInfo.kid))
        continue;

      LOG::Log(LOGDEBUG, "OnKeyStatusChangeNotify: KID %s, status %s",
               STRING::ToHexadecimal(keyInfo.kid).c_str(), KeyStatusToStr(keyInfo.status));

      auto keyFound = std::find(m_keys.begin(), m_keys.end(), keyInfo);
      if (keyFound != m_keys.end())
        *keyFound = keyInfo; // update properties
      else
        m_keys.emplace_back(keyInfo);

      if (sessionKind == SessionKind::CURRENT && !ContainsKid(m_currentSessionKids, keyInfo.kid))
        m_currentSessionKids.emplace_back(keyInfo.kid);
    }
  }

  // Wake up the threads waiting for a key (WaitForKey, DoRenewSession). The empty lock
  // ensures that a waiter is either already waiting or checks the new status
  {
    std::lock_guard<std::mutex> lock(m_renewalQueueLock);
  }
  m_keyCv.notify_all();
}

bool CWVCencSingleSampleDecrypter::HasKeyId(const std::vector<uint8_t>& keyid)
{
  if (!keyid.empty())
  {
    std::lock_guard<std::mutex> lock(m_keysLock);
    for (const DRM::KeyInfo& key : m_keys)
    {
      if (key.kid == keyid &&
          (key.status == DRM::KeyStatus::USABLE || key.status == DRM::KeyStatus::USABLE_IN_FUTURE ||
           key.status == DRM::KeyStatus::PENDING))
      {
        return true;
      }
    }
  }
  return false;
}

AP4_Result CWVCencSingleSampleDecrypter::SetFragmentInfo(AP4_UI32 poolId,
                                                         const std::vector<uint8_t>& keyId,
                                                         const AP4_UI08 nalLengthSize,
                                                         const std::vector<uint8_t>& annexbSpsPps,
                                                         DRM::Capabilities caps,
                                                         CryptoInfo cryptoInfo)
{
  if (poolId >= m_fragmentPool.size())
    return AP4_ERROR_OUT_OF_RANGE;

  m_fragmentPool[poolId].m_key = keyId;
  m_fragmentPool[poolId].m_nalLengthSize = nalLengthSize;
  m_fragmentPool[poolId].m_annexbSpsPps = annexbSpsPps;
  m_fragmentPool[poolId].capabilities = caps;
  m_fragmentPool[poolId].m_cryptoInfo = cryptoInfo;

  return AP4_SUCCESS;
}

AP4_UI32 CWVCencSingleSampleDecrypter::AddPool()
{
  for (size_t i(0); i < m_fragmentPool.size(); ++i)
    if (m_fragmentPool[i].m_nalLengthSize == 99)
    {
      m_fragmentPool[i].m_nalLengthSize = 0;
      return static_cast<AP4_UI32>(i);
    }
  m_fragmentPool.push_back(FINFO());
  m_fragmentPool.back().m_nalLengthSize = 0;
  return static_cast<AP4_UI32>(m_fragmentPool.size() - 1);
}


void CWVCencSingleSampleDecrypter::RemovePool(AP4_UI32 poolId)
{
  m_fragmentPool[poolId].m_nalLengthSize = 99;
  m_fragmentPool[poolId].m_key.clear();
}

void CWVCencSingleSampleDecrypter::LogDecryptError(const cdm::Status status,
                                                   const std::vector<uint8_t>& keyId)
{
  LOG::LogF(LOGDEBUG, "Decrypt failed with error code: %d and KID: %s", status,
            STRING::ToHexadecimal(keyId).c_str());
}

void CWVCencSingleSampleDecrypter::SetCdmSubsamples(std::vector<cdm::SubsampleEntry>& subsamples,
                                                    bool isCbc)
{
  if (isCbc)
  {
    subsamples.resize(1);
    subsamples[0] = {0, m_decryptIn.GetDataSize()};
  }
  else
  {
    subsamples.push_back({0, m_decryptIn.GetDataSize()});
  }
}

void CWVCencSingleSampleDecrypter::RepackSubsampleData(AP4_DataBuffer& dataIn,
                                                       AP4_DataBuffer& dataOut,
                                                       size_t& pos,
                                                       size_t& cipherPos,
                                                       const size_t subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  dataOut.AppendData(dataIn.GetData() + pos, bytesOfCleartextData[subsamplePos]);
  pos += bytesOfCleartextData[subsamplePos];
  dataOut.AppendData(m_decryptOut.GetData() + cipherPos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
  cipherPos += bytesOfEncryptedData[subsamplePos];
}

void CWVCencSingleSampleDecrypter::UnpackSubsampleData(AP4_DataBuffer& dataIn,
                                                       size_t& pos,
                                                       const size_t subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  pos += bytesOfCleartextData[subsamplePos];
  m_decryptIn.AppendData(dataIn.GetData() + pos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
}

void CWVCencSingleSampleDecrypter::SetInput(cdm::InputBuffer_2& cdmInputBuffer,
                                            const AP4_DataBuffer& inputData,
                                            const unsigned int subsampleCount,
                                            const uint8_t* iv,
                                            const FINFO& fragInfo,
                                            const std::vector<cdm::SubsampleEntry>& subsamples)
{
  cdmInputBuffer.data = inputData.GetData();
  cdmInputBuffer.data_size = inputData.GetDataSize();
  cdmInputBuffer.num_subsamples = subsampleCount;
  cdmInputBuffer.iv = iv;
  cdmInputBuffer.iv_size = 16; //Always 16, see AP4_CencSingleSampleDecrypter declaration.
  cdmInputBuffer.key_id = fragInfo.m_key.data();
  cdmInputBuffer.key_id_size = static_cast<uint32_t>(fragInfo.m_key.size());
  cdmInputBuffer.subsamples = subsamples.data();
  cdmInputBuffer.encryption_scheme = media::ToCdmEncryptionScheme(fragInfo.m_cryptoInfo.m_mode);
  cdmInputBuffer.timestamp = 0;
  cdmInputBuffer.pattern = {fragInfo.m_cryptoInfo.m_cryptBlocks,
                            fragInfo.m_cryptoInfo.m_skipBlocks};
}

/*----------------------------------------------------------------------
|   CWVCencSingleSampleDecrypter::DecryptSampleData
+---------------------------------------------------------------------*/
AP4_Result CWVCencSingleSampleDecrypter::DecryptSampleData(AP4_UI32 poolId,
                                                           AP4_DataBuffer& dataIn,
                                                           AP4_DataBuffer& dataOut,
                                                           const AP4_UI08* iv,
                                                           unsigned int subsampleCount,
                                                           const AP4_UI16* bytesOfCleartextData,
                                                           const AP4_UI32* bytesOfEncryptedData,
                                                           DRM::DRMMediaType streamType)
{
  if (!m_cdmAdapter->GetCDM())
  {
    dataOut.SetData(dataIn.GetData(), dataIn.GetDataSize());
    return AP4_SUCCESS;
  }

  FINFO& fragInfo(m_fragmentPool[poolId]);

  if (fragInfo.capabilities.HasFlag(Capabilities::SECURE_PATH)) //we can not decrypt only
  {
    if (fragInfo.m_nalLengthSize > 4)
    {
      LOG::LogF(LOGERROR, "Nalu length size > 4 not supported");
      return AP4_ERROR_NOT_SUPPORTED;
    }

    AP4_DataBuffer payload;
    std::vector<cdm::SubsampleEntry> rebuiltSubs;
    const bool convertAnnexB = fragInfo.capabilities.HasFlag(Capabilities::ANNEXB_REQUIRED);

    // Convert only when safe to look at clear_bytes[0]
    if (fragInfo.m_nalLengthSize && (!iv || (subsampleCount > 0 && bytesOfCleartextData[0] > 0)))
    {
      AP4_Result res = ConvertToAnnexBandInject(dataIn, payload, subsampleCount, fragInfo, iv,
                                                bytesOfCleartextData, bytesOfEncryptedData,
                                                convertAnnexB, rebuiltSubs);
      if (res != AP4_SUCCESS)
      {
        LOG::LogF(LOGERROR, "ConvertToAnnexBandInject failed: %d", res);
        return res;
      }
    }
    else
    {
      payload.AppendData(dataIn.GetData(), dataIn.GetDataSize());
      if (subsampleCount)
      {
        rebuiltSubs.reserve(subsampleCount);
        for (unsigned i = 0; i < subsampleCount; ++i)
        {
          rebuiltSubs.push_back({bytesOfCleartextData[i], bytesOfEncryptedData[i]});
        }
      }
    }

    // If IV present but no subs were provided, treat as fully encrypted payload
    if (iv && rebuiltSubs.empty())
    {
      rebuiltSubs.push_back({0u, static_cast<uint32_t>(payload.GetDataSize())});
    }

    dataOut.SetDataSize(0);

    if (iv)
    {
      const AP4_UI32 subsOut = static_cast<AP4_UI32>(rebuiltSubs.size());

      std::vector<AP4_UI16> rebuiltBytesOfCleartextData(subsOut);
      std::vector<AP4_UI32> rebuiltBytesOfEncryptedData(subsOut);
      for (AP4_UI32 i = 0; i < subsOut; ++i) {
        rebuiltBytesOfCleartextData[i] = rebuiltSubs[i].clear_bytes;
        rebuiltBytesOfEncryptedData[i] = rebuiltSubs[i].cipher_bytes;
      }

      // CENC-like prefix: [count:32][clear[]:16*N][cipher[]:32*N][IV:16][key]
      dataOut.SetData(reinterpret_cast<const AP4_Byte*>(&subsOut), sizeof(AP4_UI32));
      dataOut.AppendData(reinterpret_cast<const AP4_Byte*>(rebuiltBytesOfCleartextData.data()),
                         subsOut * sizeof(AP4_UI16));
      dataOut.AppendData(reinterpret_cast<const AP4_Byte*>(rebuiltBytesOfEncryptedData.data()),
                         subsOut * sizeof(AP4_UI32));
      dataOut.AppendData(reinterpret_cast<const AP4_Byte*>(iv), 16);
      dataOut.AppendData(fragInfo.m_key.data(), static_cast<AP4_Size>(fragInfo.m_key.size()));
    }
    else
    {
      dataOut.SetDataSize(0);
    }

#ifdef TARGET_WEBOS
    // Prepare CDM input using rebuiltSubs
    cdm::InputBuffer_2 cdmIn;
    SetInput(cdmIn, payload, static_cast<uint32_t>(rebuiltSubs.size()), iv, fragInfo, rebuiltSubs);

    m_decryptOut.Reserve(payload.GetDataSize() + dataOut.GetDataSize() + DECRYPTION_SVP_MARGIN);
    m_decryptOut.SetDataSize(0);

    CdmBuffer buf = &m_decryptOut;
    CdmDecryptedBlock cdmOut;
    cdmOut.SetDecryptedBuffer(&buf);

    CheckLicenseRenewal();

    cdm::Status ret = m_cdmAdapter->GetCDM()->Decrypt(cdmIn, &cdmOut, ToCdmStreamType(streamType));
    if (ret != cdm::kSuccess)
    {
      LOG::LogF(LOGERROR, "[DecryptSampleData] CDM->Decrypt failed, status=%d",
                static_cast<int>(ret));
      return AP4_ERROR_INVALID_FORMAT;
    }

    // Prefix (if any) + SVP-wrapped payload
    dataOut.AppendData(m_decryptOut.GetData(), m_decryptOut.GetDataSize());
#else
    // Prefix (if IV) + raw payload
    dataOut.AppendData(payload.GetData(), payload.GetDataSize());
#endif

    return AP4_SUCCESS;
  }

  if (fragInfo.m_key.empty())
  {
    LOG::LogF(LOGDEBUG, "No Key");
    return AP4_ERROR_INVALID_PARAMETERS;
  }

  dataOut.SetDataSize(0);

  uint16_t clearBytes = 0;
  uint32_t encryptedBytes = dataIn.GetDataSize();

  // check input parameters
  if (iv == NULL)
    return AP4_ERROR_INVALID_PARAMETERS;
  if (subsampleCount)
  {
    if (bytesOfCleartextData == NULL || bytesOfEncryptedData == NULL)
    {
      LOG::LogF(LOGDEBUG, "Invalid input params");
      return AP4_ERROR_INVALID_PARAMETERS;
    }
  }
  else
  {
    subsampleCount = 1;
    bytesOfCleartextData = &clearBytes;
    bytesOfEncryptedData = &encryptedBytes;
  }
  cdm::Status ret{cdm::Status::kSuccess};
  std::vector<cdm::SubsampleEntry> subsamples;
  subsamples.reserve(subsampleCount);

  bool useCbcDecrypt = fragInfo.m_cryptoInfo.m_mode == CryptoMode::AES_CBC;

  // We can only decrypt with subsamples set to 1
  // This must be handled differently for CENC and CBCS
  // CENC:
  // CDM should get 1 block of encrypted data per sample, encrypted data
  // from all subsamples should be formed into a contiguous block.
  // Even if there is only 1 subsample, we should remove cleartext data
  // from it before passing to CDM.
  // CBCS:
  // Due to the nature of this cipher subsamples must be decrypted separately

  const size_t iterations = useCbcDecrypt ? subsampleCount : 1;
  size_t absPos = 0;

  for (size_t i = 0; i < iterations; ++i)
  {
    m_decryptIn.Reserve(dataIn.GetDataSize());
    m_decryptIn.SetDataSize(0);
    size_t decryptInPos = absPos;
    if (useCbcDecrypt)
    {
      UnpackSubsampleData(dataIn, decryptInPos, i, bytesOfCleartextData, bytesOfEncryptedData);
    }
    else
    {
      for (size_t subsamplePos = 0; subsamplePos < subsampleCount; ++subsamplePos)
      {
        UnpackSubsampleData(dataIn, absPos, subsamplePos, bytesOfCleartextData,
                            bytesOfEncryptedData);
      }
    }

    if (m_decryptIn.GetDataSize() > 0) // remember to include when calling setcdmsubsamples
    {
      SetCdmSubsamples(subsamples, useCbcDecrypt);
    }

    else // we have nothing to decrypt in this iteration
    {
      if (useCbcDecrypt)
      {
        dataOut.AppendData(dataIn.GetData() + absPos, bytesOfCleartextData[i]);
        absPos += bytesOfCleartextData[i];
        continue;
      }
      else // we can exit here for CENC and just return the input buffer
      {
        dataOut.AppendData(dataIn.GetData(), dataIn.GetDataSize());
        return AP4_SUCCESS;
      }
    }

    cdm::InputBuffer_2 cdmIn;
    SetInput(cdmIn, m_decryptIn, 1, iv, fragInfo, subsamples);
    m_decryptOut.SetDataSize(m_decryptIn.GetDataSize());
    CdmBuffer buf = &m_decryptOut;
    CdmDecryptedBlock cdmOut;
    cdmOut.SetDecryptedBuffer(&buf);

    CheckLicenseRenewal();
    ret = m_cdmAdapter->GetCDM()->Decrypt(cdmIn, &cdmOut, ToCdmStreamType(streamType));

    if (ret == cdm::Status::kSuccess)
    {
      size_t cipherPos = 0;
      if (useCbcDecrypt)
      {
        RepackSubsampleData(dataIn, dataOut, absPos, cipherPos, i, bytesOfCleartextData,
                            bytesOfEncryptedData);
      }
      else
      {
        size_t absPos = 0;
        for (unsigned int i{0}; i < subsampleCount; ++i)
        {
          RepackSubsampleData(dataIn, dataOut, absPos, cipherPos, i, bytesOfCleartextData,
                              bytesOfEncryptedData);
        }
      }
    }
    else
    {
      LogDecryptError(ret, fragInfo.m_key);
    }
  }
  return (ret == cdm::Status::kSuccess) ? AP4_SUCCESS : AP4_ERROR_INVALID_PARAMETERS;
}

AP4_Result CWVCencSingleSampleDecrypter::ConvertToAnnexBandInject(
    AP4_DataBuffer& dataIn,
    AP4_DataBuffer& dataOut,
    unsigned int subsampleCount,
    CWVCencSingleSampleDecrypter::FINFO& fragInfo,
    const AP4_UI08* iv,
    const AP4_UI16* bytesOfCleartextData,
    const AP4_UI32* bytesOfEncryptedData,
    bool convertAnnexB,
    std::vector<cdm::SubsampleEntry>& rebuiltSubs)
{
  //check NAL / subsample
  const AP4_Byte *packetIn(dataIn.GetData()), *packetInEnd(dataIn.GetData() + dataIn.GetDataSize());
  // Byte position of "bytesOfCleartextData" where to set the size of the data,
  // by default starts after the subsample count data (so the size of subsampleCount data type)
  size_t clrDataBytePos = sizeof(subsampleCount);
  // size_t nalUnitCount = 0; // is there a use for this?
  size_t nalUnitSum = 0;

  // Prepare subs from originals
  rebuiltSubs.clear();
  if (iv && subsampleCount)
  {
    rebuiltSubs.reserve(subsampleCount);
    for (unsigned i = 0; i < subsampleCount; ++i)
    {
      cdm::SubsampleEntry e;
      e.clear_bytes = bytesOfCleartextData[i];
      e.cipher_bytes = bytesOfEncryptedData[i];
      rebuiltSubs.push_back(e);
    }
  }

  size_t curSub = 0;
  bool injectedStartupNALs = false;

  while (packetIn < packetInEnd)
  {
    // Read length-prefixed NAL size
    uint32_t nalSize = 0;
    for (size_t i = 0; i < fragInfo.m_nalLengthSize; ++i)
    {
      nalSize = (nalSize << 8) + *packetIn++;
    }

    // Inject SPS/PPS once before first non-AUD
    if (!injectedStartupNALs && !fragInfo.m_annexbSpsPps.empty() &&
        ((*packetIn & 0x1F) != 9 /*AVC_NAL_AUD*/))
    {
      dataOut.AppendData(fragInfo.m_annexbSpsPps.data(),
                         static_cast<AP4_Size>(fragInfo.m_annexbSpsPps.size()));

      if (iv && curSub < rebuiltSubs.size()) {
        // attribute injected bytes to clear region of current subsample
        rebuiltSubs[curSub].clear_bytes += static_cast<uint32_t>(fragInfo.m_annexbSpsPps.size());
      }

      fragInfo.m_annexbSpsPps.clear();
      injectedStartupNALs = true;
    }

    // Write Annex-B start code and payload
    static const AP4_Byte annexbStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    if (convertAnnexB && !(nalSize >= 4 && packetIn[0] == 0x00 && packetIn[1] == 0x00 &&
                           packetIn[2] == 0x00 && packetIn[3] == 0x01))
    {
      dataOut.AppendData(annexbStartCode, 4);
    }

    dataOut.AppendData(packetIn, nalSize);
    packetIn += nalSize;

    if (iv)
    {
      // Start-code delta: 4 - nalLengthSize (0,2,3) goes to clear_bytes of current subsample
      if (curSub < rebuiltSubs.size())
      {
        rebuiltSubs[curSub].clear_bytes += static_cast<uint32_t>(4 - fragInfo.m_nalLengthSize);
      }
    }

    if (!iv) {
      nalUnitSum = 0;
    }
    else if (nalSize + fragInfo.m_nalLengthSize + nalUnitSum >=
             *bytesOfCleartextData + *bytesOfEncryptedData)
    {
      // We may cross one or more subsample boundaries; advance pointers and curSub in lockstep
      AP4_UI32 summedBytes = 0;
      size_t advanced = 0;

      do
      {
        summedBytes += *bytesOfCleartextData + *bytesOfEncryptedData;
        ++bytesOfCleartextData;
        ++bytesOfEncryptedData;
        ++advanced;
        --subsampleCount;
      } while (subsampleCount && nalSize + fragInfo.m_nalLengthSize + nalUnitSum > summedBytes);

      if (nalSize + fragInfo.m_nalLengthSize + nalUnitSum > summedBytes) {
        LOG::LogF(LOGERROR, "NAL Unit exceeds subsample definition (nls: %u) %u -> %u ",
                  static_cast<unsigned>(fragInfo.m_nalLengthSize),
                  static_cast<unsigned>(nalSize + fragInfo.m_nalLengthSize + nalUnitSum),
                  summedBytes);
        return AP4_ERROR_NOT_SUPPORTED;
      }

      curSub += advanced;
      nalUnitSum = 0;
    }
    else
    {
      nalUnitSum += nalSize + fragInfo.m_nalLengthSize;
    }
  }

  if (packetIn != packetInEnd || subsampleCount) {
    LOG::Log(LOGERROR, "NAL Unit definition incomplete (nls: %u) %u -> %u ",
             static_cast<unsigned>(fragInfo.m_nalLengthSize),
             static_cast<unsigned>(packetInEnd - packetIn), subsampleCount);
    return AP4_ERROR_NOT_SUPPORTED;
  }

  return AP4_SUCCESS;
}


bool CWVCencSingleSampleDecrypter::OpenVideoDecoder(const VIDEOCODEC_INITDATA* initData)
{
  cdm::VideoDecoderConfig_3 vconfig = media::ToCdmVideoDecoderConfig(initData, m_EncryptionMode);

  // InputStream interface call OpenVideoDecoder also during playback when stream quality
  // change, so we reinitialize the decoder only when the codec change
  if (m_currentVideoDecConfig.has_value())
  {
    cdm::VideoDecoderConfig_3& currVidConfig = *m_currentVideoDecConfig;
    if (currVidConfig.codec == vconfig.codec && currVidConfig.profile == vconfig.profile)
      return true;

    m_cdmAdapter->GetCDM()->DeinitializeDecoder(cdm::StreamType::kStreamTypeVideo);
  }

  m_currentVideoDecConfig = vconfig;

  cdm::Status ret = m_cdmAdapter->GetCDM()->InitializeVideoDecoder(vconfig);
  m_videoFrames.clear();
  m_isDrained = true;

  LOG::LogF(LOGDEBUG, "Initialization returned status: %s", media::CdmStatusToString(ret).c_str());
  return ret == cdm::Status::kSuccess;
}

VIDEOCODEC_RETVAL CWVCencSingleSampleDecrypter::DecryptAndDecodeVideo(
    kodi::addon::CInstanceVideoCodec* codecInstance, const DEMUX_PACKET* sample)
{
  // if we have an picture waiting, or not yet get the dest buffer, do nothing
  if (m_videoFrames.size() == 4)
    return VC_ERROR;

  if (sample->cryptoInfo && sample->cryptoInfo->numSubSamples > 0 &&
      (!sample->cryptoInfo->clearBytes || !sample->cryptoInfo->cipherBytes))
  {
    LOG::LogF(LOGERROR, "Missing crypting clear/cipher bytes info");
    return VC_ERROR;
  }

  cdm::InputBuffer_2 inputBuffer{};
  std::vector<cdm::SubsampleEntry> subsamples;

  media::ToCdmInputBuffer(sample, &subsamples, &inputBuffer);

  if (sample->iSize > 0)
    m_isDrained = false;

  //LICENSERENEWAL:
  CheckLicenseRenewal();

  media::CdmVideoFrame videoFrame;

  // DecryptAndDecodeFrame calls CdmAdapter::Allocate which calls Host->GetBuffer
  // that cast hostInstance to CInstanceVideoCodec to get the frame buffer
  // so we have temporary set the host instance
  m_cdmAdapter->SetCodecInstance(codecInstance);
  cdm::Status status = m_cdmAdapter->GetCDM()->DecryptAndDecodeFrame(inputBuffer, &videoFrame);
  m_cdmAdapter->ResetCodecInstance();

  if (status == cdm::Status::kSuccess)
  {
    std::list<media::CdmVideoFrame>::iterator f(m_videoFrames.begin());
    while (f != m_videoFrames.end() && f->Timestamp() < videoFrame.Timestamp())
    {
      ++f;
    }
    m_videoFrames.insert(f, videoFrame);
    return VC_NONE;
  }
  else if (status == cdm::Status::kNeedMoreData && inputBuffer.data)
  {
    return VC_NONE;
  }
  else if (status == cdm::Status::kNoKey)
  {
    // In-band key rotation: the license of the key is being requested. This only avoids
    // the error log, the packet is dropped anyway
    if (IsRenewalPending({inputBuffer.key_id, inputBuffer.key_id + inputBuffer.key_id_size}))
      return VC_NONE;

    LOG::LogF(LOGERROR, "Returned CDM status \"kNoKey\" for KID: %s",
              STRING::ToHexadecimal(inputBuffer.key_id, inputBuffer.key_id_size).c_str());
    return VC_EOF;
  }

  LOG::LogF(LOGDEBUG, "Returned CDM status: %i", status);
  return VC_ERROR;
}

VIDEOCODEC_RETVAL CWVCencSingleSampleDecrypter::VideoFrameDataToPicture(
    kodi::addon::CInstanceVideoCodec* codecInstance, VIDEOCODEC_PICTURE* picture)
{
  if (m_videoFrames.size() == 4 ||
      (m_videoFrames.size() > 0 && (picture->flags & VIDEOCODEC_PICTURE_FLAG_DRAIN)))
  {
    media::CdmVideoFrame& videoFrame(m_videoFrames.front());

    picture->width = videoFrame.Size().width;
    picture->height = videoFrame.Size().height;
    picture->pts = videoFrame.Timestamp();
    picture->decodedData = videoFrame.FrameBuffer()->Data();
    picture->decodedDataSize = videoFrame.FrameBuffer()->Size();
    picture->videoBufferHandle = static_cast<CdmFixedBuffer*>(videoFrame.FrameBuffer())->Buffer();

    for (size_t i = 0; i < cdm::kMaxPlanes; ++i)
    {
      picture->planeOffsets[i] = videoFrame.PlaneOffset(static_cast<cdm::VideoPlane>(i));
      picture->stride[i] = videoFrame.Stride(static_cast<cdm::VideoPlane>(i));
    }
    picture->videoFormat = media::ToSSDVideoFormat(videoFrame.Format());
    videoFrame.SetFrameBuffer(nullptr); //marker for "No Picture"

    delete (CdmFixedBuffer*)(videoFrame.FrameBuffer());
    m_videoFrames.pop_front();

    return VC_PICTURE;
  }
  else if ((picture->flags & VIDEOCODEC_PICTURE_FLAG_DRAIN))
  {
    static DEMUX_PACKET drainSample{};
    if (m_isDrained || DecryptAndDecodeVideo(codecInstance, &drainSample) == VC_ERROR)
    {
      m_isDrained = true;
      return VC_EOF;
    }
    else
      return VC_NONE;
  }

  return VC_BUFFER;
}

void CWVCencSingleSampleDecrypter::ResetVideo()
{
  m_cdmAdapter->GetCDM()->ResetDecoder(cdm::kStreamTypeVideo);
  m_isDrained = true;
}

void CWVCencSingleSampleDecrypter::SetDefaultKeyId(const std::vector<uint8_t>& keyId)
{
  std::lock_guard<std::mutex> lock(m_renewalLock);
  m_defaultKeyId = keyId;
}

void CWVCencSingleSampleDecrypter::AddKeyId(const std::vector<uint8_t>& keyId)
{
  DRM::KeyInfo key;
  key.kid = keyId;
  key.status = DRM::KeyStatus::USABLE;

  std::lock_guard<std::mutex> lock(m_keysLock);
  if (std::find(m_keys.begin(), m_keys.end(), key) == m_keys.end())
  {
    m_keys.push_back(key);
  }
}

bool CWVCencSingleSampleDecrypter::HasUsableKey(const std::vector<uint8_t>& keyId)
{
  // Unlike HasKeyId, a pending or future key is not usable yet
  if (keyId.empty())
    return false;

  std::lock_guard<std::mutex> lock(m_keysLock);
  return std::any_of(m_keys.cbegin(), m_keys.cend(), [&keyId](const DRM::KeyInfo& key)
                     { return key.kid == keyId && key.status == DRM::KeyStatus::USABLE; });
}

bool CWVCencSingleSampleDecrypter::HasGrantedKey(const std::vector<uint8_t>& keyId)
{
  std::lock_guard<std::mutex> lock(m_keysLock);
  return std::any_of(m_keys.cbegin(), m_keys.cend(), [&keyId](const DRM::KeyInfo& key)
                     { return key.kid == keyId && IsKeyGranted(key.status); });
}

bool CWVCencSingleSampleDecrypter::IsRenewalKeyReady(const std::vector<uint8_t>& keyId)
{
  // The license of the new session has granted a key, and the requested key is granted.
  // On early renewal (new PSSH, the key still usable) the requested key can come from
  // the previous session, the new license brings the key of the next crypto period
  std::lock_guard<std::mutex> lock(m_keysLock);
  bool hasNewSessionKey{false};
  bool isKeyGranted{false};
  for (const DRM::KeyInfo& key : m_keys)
  {
    if (!IsKeyGranted(key.status))
      continue;
    if (ContainsKid(m_currentSessionKids, key.kid))
      hasNewSessionKey = true;
    if (key.kid == keyId)
      isKeyGranted = true;
  }
  return hasNewSessionKey && isKeyGranted;
}

bool CWVCencSingleSampleDecrypter::IsRenewalPending(const std::vector<uint8_t>& keyId)
{
  std::lock_guard<std::mutex> lock(m_renewalQueueLock);
  return IsKidPendingLocked(keyId);
}

bool CWVCencSingleSampleDecrypter::IsKidPendingLocked(const std::vector<uint8_t>& keyId) const
{
  return std::any_of(m_requestedPssh.cbegin(), m_requestedPssh.cend(),
                     [&keyId](const RequestedPssh& item)
                     {
                       return item.state == PsshState::QUEUED &&
                              ContainsKid(item.pendingKids, keyId);
                     });
}

void CWVCencSingleSampleDecrypter::AddPendingKidLocked(const std::vector<uint8_t>& psshData,
                                                       const std::vector<uint8_t>& keyId)
{
  // One PSSH can carry the keys of several KIDs (e.g. the next crypto period, or the tracks
  // that share the session), the readers of all of them wait for the same license
  auto it =
      std::find_if(m_requestedPssh.begin(), m_requestedPssh.end(),
                   [&psshData](const RequestedPssh& item) { return item.psshData == psshData; });
  if (it != m_requestedPssh.end() && it->state == PsshState::QUEUED &&
      !ContainsKid(it->pendingKids, keyId) && it->pendingKids.size() < RENEWAL_MAX_KIDS)
  {
    it->pendingKids.emplace_back(keyId);
  }
}

void CWVCencSingleSampleDecrypter::SetPsshStateLocked(const std::vector<uint8_t>& psshData,
                                                      PsshState state)
{
  auto it =
      std::find_if(m_requestedPssh.begin(), m_requestedPssh.end(),
                   [&psshData](const RequestedPssh& item) { return item.psshData == psshData; });
  if (it == m_requestedPssh.end())
  {
    m_requestedPssh.push_back({psshData, state, {}});
    it = std::prev(m_requestedPssh.end());
  }
  it->state = state;
  it->time = std::chrono::steady_clock::now();
  if (state != PsshState::QUEUED)
    it->pendingKids.clear();

  // Drop the oldest entries that are not queued
  while (m_requestedPssh.size() > RENEWAL_PSSH_HISTORY)
  {
    auto oldest =
        std::find_if(m_requestedPssh.begin(), m_requestedPssh.end(),
                     [](const RequestedPssh& item) { return item.state != PsshState::QUEUED; });
    if (oldest == m_requestedPssh.end())
      break;
    m_requestedPssh.erase(oldest);
  }
}

const uint8_t* CWVCencSingleSampleDecrypter::GetRenewalSystemId() const
{
  return DRM::ID_WIDEVINE;
}

bool CWVCencSingleSampleDecrypter::IsKeyRotation(const std::vector<uint8_t>& keyId,
                                                 bool hasCryptoPeriodIndex,
                                                 bool canStart)
{
  // Streams without key rotation can also have SEIG boxes and PSSH boxes in the media
  // segments, their license must not be requested again. Once seen, the rotation is
  // kept, a seek can go back to a crypto period that uses the default KID
  std::lock_guard<std::mutex> lock(m_renewalLock);
  if (!m_isKeyRotation && canStart)
  {
    m_isKeyRotation =
        hasCryptoPeriodIndex || DRM::IsAllZeroKey(m_defaultKeyId) || keyId != m_defaultKeyId;
  }
  return m_isKeyRotation;
}

void CWVCencSingleSampleDecrypter::RenewSessionForKey(const std::vector<uint8_t>& keyId,
                                                      const std::vector<uint8_t>& psshData,
                                                      bool isSeigKid)
{
  // Called on the demux thread for each fragment, the license request is queued
  // for the worker thread
  if (keyId.empty())
    return;

  std::vector<std::vector<uint8_t>> psshKids;
  bool hasCryptoPeriodIndex{false};
  DRM::ParseWidevinePssh(psshData, psshKids, &hasCryptoPeriodIndex);
  // The default KID can differ from the one of the shared decrypter also without key rotation
  // (e.g. audio and video with their own keys in one session), only a SEIG KID can start it
  if (!IsKeyRotation(keyId, hasCryptoPeriodIndex, isSeigKid))
    return;

  if (psshData.empty())
  {
    if (HasUsableKey(keyId))
      return;

    std::lock_guard<std::mutex> lock(m_renewalQueueLock);
    if (!ContainsKid(m_noPsshWarnedKids, keyId) &&
        m_noPsshWarnedKids.size() < RENEWAL_MAX_KIDS)
    {
      m_noPsshWarnedKids.emplace_back(keyId);
      LOG::LogF(LOGWARNING,
                "No Widevine PSSH in the media segment, the license of KID %s cannot be renewed",
                STRING::ToHexadecimal(keyId).c_str());
    }
    return;
  }

  // Requested also when the key of the fragment is usable, the license can bring the key
  // of the next crypto period before it starts. A PSSH whose keys are all granted
  // (repeated in each segment) does not need a request
  const bool isKeyMissing = psshKids.empty() ? !HasUsableKey(keyId)
                                             : std::any_of(psshKids.cbegin(), psshKids.cend(),
                                                           [this](const std::vector<uint8_t>& kid)
                                                           { return !HasGrantedKey(kid); });
  if (!isKeyMissing)
    return;

  std::lock_guard<std::mutex> lock(m_renewalQueueLock);
  if (m_renewalStop)
    return;

  auto it =
      std::find_if(m_requestedPssh.begin(), m_requestedPssh.end(),
                   [&psshData](const RequestedPssh& item) { return item.psshData == psshData; });
  if (it != m_requestedPssh.end())
  {
    // A PSSH already requested is requested again only when the key of the fragment
    // is not usable. A license given up, or received without this key, is retried after
    // a while. A license whose key was removed with its session, closed by a later renewal,
    // is requested again at once, the playback went back to its crypto period (seek backwards)
    if (it->state == PsshState::QUEUED)
    {
      AddPendingKidLocked(psshData, keyId);
      return;
    }
    if (HasUsableKey(keyId))
      return;
    if (!(it->state == PsshState::DONE && TakeRemovedKid(keyId)) &&
        std::chrono::steady_clock::now() - it->time < RENEWAL_RETRY_AFTER)
    {
      return;
    }
    LOG::LogF(LOGDEBUG, "In-band renewal: requesting again the license for KID %s",
              STRING::ToHexadecimal(keyId).c_str());
  }

  SetPsshStateLocked(psshData, PsshState::QUEUED);
  AddPendingKidLocked(psshData, keyId);
  m_renewalQueue.push_back({keyId, psshData, 0, std::chrono::steady_clock::now()});
  if (!StartRenewalThreadLocked())
  {
    // Nobody would process the request, the readers must not wait for it
    m_renewalQueue.pop_back();
    SetPsshStateLocked(psshData, PsshState::GIVEN_UP);
    m_keyCv.notify_all();
    return;
  }
  m_renewalQueueCv.notify_one();
}

bool CWVCencSingleSampleDecrypter::WaitForKey(const std::vector<uint8_t>& keyId,
                                              std::chrono::milliseconds timeout)
{
  std::unique_lock<std::mutex> lock(m_renewalQueueLock);
  // Wait only while a license request for the KID is queued or running
  m_keyCv.wait_for(lock, timeout, [this, &keyId]
                   { return m_renewalStop || !IsKidPendingLocked(keyId) || HasUsableKey(keyId); });
  return HasUsableKey(keyId);
}

bool CWVCencSingleSampleDecrypter::StartRenewalThreadLocked()
{
  // The worker runs until the destructor joins it
  if (m_renewalThread.joinable())
    return true;
  if (m_renewalStop)
    return false;

  try
  {
    m_renewalThread = std::thread(&CWVCencSingleSampleDecrypter::RenewalThreadLoop, this);
  }
  catch (const std::system_error& e)
  {
    // The thread stays not joinable, a later request tries again
    LOG::LogF(LOGERROR, "In-band renewal: cannot start the worker thread: %s", e.what());
    return false;
  }
  return true;
}

void CWVCencSingleSampleDecrypter::RenewalThreadLoop()
{
  while (true)
  {
    RenewalRequest request;
    {
      std::unique_lock<std::mutex> lock(m_renewalQueueLock);
      while (true)
      {
        if (m_renewalStop)
          return;

        // The first request that can be made, a retry waits for its time without
        // delaying the requests of the other KIDs
        auto next = std::min_element(m_renewalQueue.begin(), m_renewalQueue.end(),
                                     [](const RenewalRequest& a, const RenewalRequest& b)
                                     { return a.notBefore < b.notBefore; });
        if (next == m_renewalQueue.end())
        {
          m_renewalQueueCv.wait(lock);
        }
        else if (next->notBefore <= std::chrono::steady_clock::now())
        {
          request = std::move(*next);
          m_renewalQueue.erase(next);
          break;
        }
        else
        {
          const auto notBefore = next->notBefore;
          m_renewalQueueCv.wait_until(lock, notBefore);
        }
      }
    }

    ++request.attempts;
    const bool isSuccess = DoRenewSession(request);

    {
      std::lock_guard<std::mutex> lock(m_renewalQueueLock);
      if (m_renewalStop)
        return;

      if (!isSuccess && request.attempts < RENEWAL_MAX_ATTEMPTS)
      {
        // The KID stays pending, the reader keeps waiting for its key
        request.notBefore =
            std::chrono::steady_clock::now() + RENEWAL_RETRY_DELAY * request.attempts;
        m_renewalQueue.push_back(std::move(request));
        continue;
      }

      if (!isSuccess)
      {
        LOG::LogF(LOGWARNING,
                  "In-band renewal: license request for KID %s given up after %d attempts",
                  STRING::ToHexadecimal(request.keyId).c_str(), request.attempts);
      }
      // Ends the wait of all the KIDs of the PSSH
      SetPsshStateLocked(request.psshData, isSuccess ? PsshState::DONE : PsshState::GIVEN_UP);
    }
    m_keyCv.notify_all();
  }
}

std::string CWVCencSingleSampleDecrypter::WaitSessionFuture(std::future<std::string>& sessionFut)
{
  // The Widevine CDM resolves the promise inside the call that opens the session,
  // waiting is only a safeguard against a CDM that does not
  const auto deadline = std::chrono::steady_clock::now() + RENEWAL_SESSION_WAIT;
  while (sessionFut.wait_for(RENEWAL_SESSION_WAIT_STEP) != std::future_status::ready)
  {
    if (m_renewalStop || std::chrono::steady_clock::now() >= deadline)
    {
      LOG::LogF(LOGERROR, "In-band renewal: the CDM has not opened the session");
      return {};
    }
  }

  try
  {
    return sessionFut.get(); // throws if CDM rejected
  }
  catch (const std::exception& e)
  {
    LOG::LogF(LOGERROR, "In-band renewal: session creation failed: %s", e.what());
  }
  return {};
}

bool CWVCencSingleSampleDecrypter::WaitRenewalKey(const std::vector<uint8_t>& keyId)
{
  // The CDM reports the keys inside the license update call, waiting is only a safeguard
  std::unique_lock<std::mutex> lock(m_renewalQueueLock);
  m_keyCv.wait_for(lock, RENEWAL_KEY_WAIT,
                   [this, &keyId] { return m_renewalStop || IsRenewalKeyReady(keyId); });
  return !m_renewalStop && IsRenewalKeyReady(keyId);
}

void CWVCencSingleSampleDecrypter::CloseCdmSession(const std::string& sessionId)
{
  LOG::LogF(LOGDEBUG, "Closing widevine session ID: %s", sessionId.c_str());
  m_cdmAdapter->GetCDM()->CloseSession(NextPromiseId(), sessionId.data(),
                                       static_cast<uint32_t>(sessionId.size()));
}

void CWVCencSingleSampleDecrypter::RemoveSessionKeys(
    const std::vector<std::vector<uint8_t>>& sessionKids,
    const std::vector<std::vector<uint8_t>>& keepKids,
    bool isClosedSession)
{
  std::lock_guard<std::mutex> lock(m_keysLock);
  m_keys.erase(std::remove_if(m_keys.begin(), m_keys.end(),
                              [&](const DRM::KeyInfo& key)
                              {
                                const bool isRemoved = ContainsKid(sessionKids, key.kid) &&
                                                       !ContainsKid(keepKids, key.kid) &&
                                                       !ContainsKid(m_currentSessionKids, key.kid);
                                if (isRemoved && isClosedSession &&
                                    !ContainsKid(m_removedKids, key.kid))
                                  m_removedKids.emplace_back(key.kid);
                                return isRemoved;
                              }),
               m_keys.end());
  while (m_removedKids.size() > RENEWAL_MAX_KIDS)
    m_removedKids.erase(m_removedKids.begin());
}

bool CWVCencSingleSampleDecrypter::TakeRemovedKid(const std::vector<uint8_t>& keyId)
{
  std::lock_guard<std::mutex> lock(m_keysLock);
  auto it = std::find(m_removedKids.begin(), m_removedKids.end(), keyId);
  if (it == m_removedKids.end())
    return false;
  m_removedKids.erase(it);
  return true;
}

bool CWVCencSingleSampleDecrypter::DoRenewSession(const RenewalRequest& request)
{
  // Runs on the worker thread. The lock serializes the license requests with the
  // automatic license renewal (CheckLicenseRenewal), that skips its turn meanwhile
  std::lock_guard<std::mutex> action(m_sessionActionLock);

  const std::string kidHex = STRING::ToHexadecimal(request.keyId);
  const std::vector<uint8_t> initData = DRM::PSSH::Make(DRM::ID_WIDEVINE, {}, request.psshData);
  if (initData.size() < 4 || initData.size() > 4096)
  {
    LOG::LogF(LOGERROR, "In-band renewal: unexpected PSSH size (%zu) for KID %s", initData.size(),
              kidHex.c_str());
    return false;
  }

  LOG::LogF(LOGDEBUG, "In-band renewal: requesting the license for KID %s (attempt %d)",
            kidHex.c_str(), request.attempts);

  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    m_isRenewalSessionPending = true;
    m_renewalMessages.clear();
  }

  const uint32_t pid = NextPromiseId();
  std::future<std::string> sessionFut = m_cdmAdapter->GetCDM()->PrepareSessionFuture(pid);
  m_cdmAdapter->GetCDM()->CreateSessionAndGenerateRequest(pid, cdm::SessionType::kTemporary,
                                                          cdm::InitDataType::kCenc, initData.data(),
                                                          static_cast<uint32_t>(initData.size()));

  const std::string newSession = WaitSessionFuture(sessionFut);

  std::vector<uint8_t> newChallenge;
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    m_isRenewalSessionPending = false;
    for (const auto& [sessionId, data] : m_renewalMessages)
    {
      if (sessionId == newSession)
        newChallenge = data;
    }
    m_renewalMessages.clear();
  }

  if (newSession.empty())
    return false;

  if (newChallenge.empty() || m_renewalStop)
  {
    if (newChallenge.empty())
      LOG::LogF(LOGERROR, "In-band renewal: no license request from the new session");
    CloseCdmSession(newSession);
    return false;
  }

  // Switch to the new session, the license request and the response are on it
  std::string savedSession;
  std::vector<uint8_t> savedPssh;
  std::vector<uint8_t> savedDefaultKeyId;
  std::vector<std::vector<uint8_t>> savedKids;
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    savedSession = m_strSession;
    savedPssh = m_pssh;
    savedDefaultKeyId = m_defaultKeyId;
    m_strSession = newSession;
    m_challenge.SetData(newChallenge.data(), static_cast<AP4_Size>(newChallenge.size()));
    m_pssh = initData;
    m_defaultKeyId = request.keyId; // For the {KID} placeholder of the license wrapper

    std::lock_guard<std::mutex> keysLock(m_keysLock);
    savedKids.swap(m_currentSessionKids);
  }

  // Same loop as CreateSession, the first request can be the service certificate request
  while (!m_renewalStop && HasChallenge() && SendSessionMessage())
    ;

  if (!WaitRenewalKey(request.keyId))
  {
    // Go back to the previous session, its keys still decrypt the buffered samples and the
    // automatic license renewal stays on it. The new session and its keys are dropped
    if (!m_renewalStop)
      LOG::LogF(LOGERROR, "In-band renewal: no usable key for KID %s", kidHex.c_str());

    std::vector<std::vector<uint8_t>> newKids;
    std::vector<std::vector<uint8_t>> previousKids;
    {
      std::lock_guard<std::mutex> lock(m_renewalLock);
      m_strSession = savedSession;
      m_challenge.SetDataSize(0);
      m_pssh = savedPssh;
      m_defaultKeyId = savedDefaultKeyId;
      previousKids = m_previousSession.kids;

      std::lock_guard<std::mutex> keysLock(m_keysLock);
      newKids.swap(m_currentSessionKids);
      m_currentSessionKids.swap(savedKids);
    }
    // A failed renewal, its keys are not remembered as removed
    CloseCdmSession(newSession);
    RemoveSessionKeys(newKids, previousKids, false);
    return false;
  }

  // Keep only the previous session, its keys can still decrypt the buffered samples.
  // An older one is closed and its keys removed. When the requested key is granted only
  // by the older session (early renewal after a seek), that one is kept instead
  PreviousSession closing{savedSession, savedKids};
  PreviousSession kept;
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    const bool isKeyOnlyInOlder = ContainsKid(m_previousSession.kids, request.keyId) &&
                                  !ContainsKid(savedKids, request.keyId);
    if (!isKeyOnlyInOlder)
      std::swap(closing, m_previousSession);
    kept = m_previousSession;
  }
  if (!closing.sessionId.empty() && closing.sessionId != kept.sessionId)
  {
    CloseCdmSession(closing.sessionId);
    RemoveSessionKeys(closing.kids, kept.kids, true);
  }

  LOG::LogF(LOGDEBUG, "In-band renewal: license received for KID %s, session ID: %s",
            kidHex.c_str(), newSession.c_str());
  return true;
}

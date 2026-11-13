/*
 *  Copyright (C) 2015 The Chromium Authors. All rights reserved.
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See LICENSES/README.md for more information.
 */

#include "cdm_adapter.h"

#include "../../debug.h"
#include "../base/limits.h"

#include <chrono>
#include <cstring>
#include <thread>

#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define DCHECK(condition) assert(condition)

#ifdef __APPLE__
#include <sys/time.h>
//clock_gettime is not implemented on OSX
int clock_gettime(int clk_id, struct timespec* t) {
  struct timeval now;
  int rv = gettimeofday(&now, NULL);
  if (rv) return rv;
  t->tv_sec  = now.tv_sec;
  t->tv_nsec = now.tv_usec * 1000;
  return 0;
}
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif
#endif

using namespace CDM_DBG;

namespace media {

uint64_t gtc()
{
#ifdef OS_WIN
  return GetTickCount64();
#else
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  return  tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
#endif
}

namespace {
#ifdef _WIN32
const char PATH_SEPARATOR = '\\';
#else
const char PATH_SEPARATOR = '/';
#endif

constexpr std::chrono::seconds INIT_FUTURE_TIMEOUT_SEC{3};

void* GetCdmHost(int host_interface_version, void* user_data)
{
  if (!user_data)
    return nullptr;

  CdmAdapter *adapter = static_cast<CdmAdapter*>(user_data);

  switch (host_interface_version)
  {
    case cdm::Host_10::kVersion:
      return static_cast<cdm::Host_10*>(adapter);
    case cdm::Host_11::kVersion:
      return static_cast<cdm::Host_11*>(adapter);
    case cdm::Host_12::kVersion:
      return static_cast<cdm::Host_12*>(adapter);
    default:
      return nullptr;
  }
}

bool ExistsDir(const char* path)
{
  struct stat info;
  if (stat(path, &info) != 0)
    return false;
  else if (info.st_mode & S_IFDIR)
    return true;
  return false;
}

// \brief Create a full directory path
bool CreateDirs(const char* path)
{
  const char* p;
  bool ret = true;
  // Skip Windows drive letter.
#ifdef _WIN32
  p = std::strchr(path, ':');
  if (p != NULL)
    p++;
  else
  {
#endif
    p = path;
#ifdef _WIN32
  }
#endif
  // Ignore the first separator
  if (*p == PATH_SEPARATOR)
    p++;

  // Iterate each folder in the path
  while ((p = std::strchr(p, PATH_SEPARATOR)) != NULL)
  {
    // Skip empty elements. Could be a Windows UNC path or just multiple separators
    if (p != path && *(p - 1) == PATH_SEPARATOR)
    {
      p++;
      continue;
    }
    std::string currPath(path, p - path);
    p++;
#ifdef _WIN32
    std::wstring currPathW(currPath.begin(), currPath.end());
    if (CreateDirectory(currPathW.c_str(), NULL) == FALSE)
    {
      if (GetLastError() != ERROR_ALREADY_EXISTS)
      {
        ret = false;
        break;
      }
    }
#else
    if (mkdir(currPath.c_str(), 0774) != 0)
    {
      if (errno != EEXIST)
      {
        ret = false;
        break;
      }
    }
#endif
  }
  return ret;
}

}  // namespace

cdm::VideoDecoderConfig_2 ToVideoDecoderConfig_2(
  const cdm::VideoDecoderConfig_3& config) {
  return{ config.codec,
    config.profile,
    config.format,
    config.coded_size,
    config.extra_data,
    config.extra_data_size,
    config.encryption_scheme };
}


/*******************************         CdmAdapter        ****************************************/


CdmAdapter::CdmAdapter(
  const std::string& key_system,
  const std::string& cdm_path,
  const std::string& base_path,
  const CdmConfig& cdm_config,
  CdmAdapterClient *client)
: cdm_path_(cdm_path)
, cdm_base_path_(base_path)
, client_(client)
, key_system_(key_system)
, cdm_config_(cdm_config)
{
  //DCHECK(!key_system_.empty());
}

CdmAdapter::~CdmAdapter()
{
  UnloadCDM();
}

bool CdmAdapter::LoadCDM()
{
  UnloadCDM();

  base::NativeLibraryLoadError error;
  library_ = base::LoadNativeLibrary(cdm_path_, &error);

  if (!library_)
  {
    Log(LogLevel::ERROR, "Failed to load library: %s", error.ToString().c_str());
    return false;
  }

  init_cdm_func = reinterpret_cast<InitializeCdmModuleFunc>(
      base::GetFunctionPointerFromNativeLibrary(library_, MAKE_STRING(INITIALIZE_CDM_MODULE)));
  deinit_cdm_func = reinterpret_cast<DeinitializeCdmModuleFunc>(
      base::GetFunctionPointerFromNativeLibrary(library_, "DeinitializeCdmModule"));
  create_cdm_func = reinterpret_cast<CreateCdmFunc>(
      base::GetFunctionPointerFromNativeLibrary(library_, "CreateCdmInstance"));
  get_cdm_verion_func = reinterpret_cast<GetCdmVersionFunc>(
      base::GetFunctionPointerFromNativeLibrary(library_, "GetCdmVersion"));

  if (!init_cdm_func || !create_cdm_func || !get_cdm_verion_func || !deinit_cdm_func)
  {
    Log(LogLevel::ERROR, "Failed to binding CDM library functions");
    return false;
  }
  return true;
}

bool CdmAdapter::Initialize()
{
  m_initPromise = std::promise<void>{};
  m_initFuture = m_initPromise.get_future();
  m_provisioningCompleteOrStarted = false;
  m_isStoppingTimers = false;

  Log(LogLevel::DEBUG, "CDM version: %s", GetVersion().c_str());

#if defined(OS_WIN)
  // Load DXVA before sandbox lockdown to give CDM access to Output Protection
  // Manager (OPM).
  base::NativeLibraryLoadError error;
  base::LoadNativeLibrary("dxva2.dll", &error);
#endif  // defined(OS_WIN)

  init_cdm_func();
  
#ifndef TARGET_WEBOS
  cdm12_ = static_cast<cdm::ContentDecryptionModule_12*>(create_cdm_func(
      12, key_system_.data(), static_cast<uint32_t>(key_system_.size()), GetCdmHost, this));
#endif

  if (!cdm12_)
  {
    cdm11_ = static_cast<cdm::ContentDecryptionModule_11*>(create_cdm_func(
        11, key_system_.data(), static_cast<uint32_t>(key_system_.size()), GetCdmHost, this));

    if (!cdm11_)
    {
      cdm10_ = static_cast<cdm::ContentDecryptionModule_10*>(create_cdm_func(
          10, key_system_.data(), static_cast<uint32_t>(key_system_.size()), GetCdmHost, this));
    }
  }

  if (cdm12_ || cdm11_ || cdm10_)
  {
    if (cdm12_)
    {
      cdm12_->Initialize(cdm_config_.allow_distinctive_identifier,
                         cdm_config_.allow_persistent_state, cdm_config_.use_hw_secure_codecs);
    }
    else if (cdm11_)
    {
      cdm11_->Initialize(cdm_config_.allow_distinctive_identifier,
                         cdm_config_.allow_persistent_state, cdm_config_.use_hw_secure_codecs);
    }
    else if (cdm10_)
    {
      cdm10_->Initialize(cdm_config_.allow_distinctive_identifier,
                         cdm_config_.allow_persistent_state, cdm_config_.use_hw_secure_codecs);
    }

    // Wait for the CDM to be initialized
    // Add a maximum timeout in case we never hear back!
    if (m_initFuture.valid() &&
        m_initFuture.wait_for(INIT_FUTURE_TIMEOUT_SEC) != std::future_status::ready)
    {
      Log(LogLevel::ERROR, "CDM initialization timed out");
      return false;
    }

    if (!m_provisioningCompleteOrStarted)
    {
      Log(LogLevel::ERROR, "CDM initialization failed or not started");
      return false;
    }

    return true;
  }

  Log(LogLevel::ERROR, "Cannot get the ContentDecryptionModule interface");
  return false;
}

std::string CdmAdapter::GetVersion() const
{
  if (get_cdm_verion_func)
    return get_cdm_verion_func();

  return "";
}

void CdmAdapter::UnloadCDM()
{
  if (cdm12_)
  {
    cdm12_->Destroy();
    cdm12_ = nullptr;
  }
  else if (cdm11_)
  {
    cdm11_->Destroy();
    cdm11_ = nullptr;
  }
  else if (cdm10_)
  {
    cdm10_->Destroy();
    cdm10_ = nullptr;
  }

  init_cdm_func = nullptr;
  create_cdm_func = nullptr;
  get_cdm_verion_func = nullptr;

  if (deinit_cdm_func)
  {
    deinit_cdm_func();
    deinit_cdm_func = nullptr;
  }

  if (library_)
  {
    base::UnloadNativeLibrary(library_);
    library_ = nullptr;
  }
}

void CdmAdapter::SendClientMessage(const char* session, uint32_t session_size, CdmAdapterClient::CDMADPMSG msg, const uint8_t *data, size_t data_size, uint32_t status)
{
  std::lock_guard<std::mutex> guard(client_mutex_);
  if (client_)
    client_->OnCDMMessage(session, session_size, msg, data, data_size, status);
}

void CdmAdapter::SendOnKeyStatusChange(const std::string& sessionId,
                                       const std::vector<CdmAdapterClient::KeyInfo>& keysInfo)
{
  std::lock_guard<std::mutex> guard(client_mutex_);
  if (client_)
    client_->OnKeyStatusChange(sessionId, keysInfo);
}

void CdmAdapter::RemoveClient()
{
  {
    std::lock_guard<std::mutex> guard(client_mutex_);
    client_ = nullptr;
  }
  // The client goes away, so stop the pending timers and wait for their threads.
  // This also releases the references to this adapter held by the timer tasks
  std::vector<std::future<void>> timerTasks;
  {
    std::lock_guard<std::mutex> lock(m_timerMutex);
    m_isStoppingTimers = true;
    timerTasks.swap(m_asyncTimerTasks);
  }
  m_timerCond.notify_all();
  timerTasks.clear();
}

void CdmAdapter::SetServerCertificate(uint32_t promise_id,
  const uint8_t* server_certificate_data,
  uint32_t server_certificate_data_size)
{
  if (server_certificate_data_size < limits::kMinCertificateLength ||
    server_certificate_data_size > limits::kMaxCertificateLength) {
  return;
  }

  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->SetServerCertificate(promise_id, server_certificate_data, server_certificate_data_size);
  else if (cdm11_)
    cdm11_->SetServerCertificate(promise_id, server_certificate_data, server_certificate_data_size);
  else if (cdm10_)
    cdm10_->SetServerCertificate(promise_id, server_certificate_data, server_certificate_data_size);
}

void CdmAdapter::CreateSessionAndGenerateRequest(uint32_t promise_id,
  cdm::SessionType session_type,
  cdm::InitDataType init_data_type,
  const uint8_t* init_data,
  uint32_t init_data_size)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->CreateSessionAndGenerateRequest(promise_id, session_type, init_data_type, init_data,
                                            init_data_size);
  else if (cdm11_)
    cdm11_->CreateSessionAndGenerateRequest(promise_id, session_type, init_data_type, init_data,
                                            init_data_size);
  else if (cdm10_)
    cdm10_->CreateSessionAndGenerateRequest(promise_id, session_type, init_data_type, init_data,
                                            init_data_size);
}

void CdmAdapter::LoadSession(uint32_t promise_id,
  cdm::SessionType session_type,
  const char* session_id,
  uint32_t session_id_size)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->LoadSession(promise_id, session_type, session_id, session_id_size);
  else if (cdm11_)
    cdm11_->LoadSession(promise_id, session_type, session_id, session_id_size);
  else if (cdm10_)
    cdm10_->LoadSession(promise_id, session_type, session_id, session_id_size);
}

void CdmAdapter::UpdateSession(uint32_t promise_id,
  const char* session_id,
  uint32_t session_id_size,
  const uint8_t* response,
  uint32_t response_size)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->UpdateSession(promise_id, session_id, session_id_size, response, response_size);
  else if (cdm11_)
    cdm11_->UpdateSession(promise_id, session_id, session_id_size, response, response_size);
  else if (cdm10_)
    cdm10_->UpdateSession(promise_id, session_id, session_id_size, response, response_size);
}

void CdmAdapter::CloseSession(uint32_t promise_id,
  const char* session_id,
  uint32_t session_id_size)
{
  // The pending timers are not stopped here. The CDM timers are not bound to a session,
  // stopping them would stop also the license renewal of the other open sessions
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->CloseSession(promise_id, session_id, session_id_size);
  else if (cdm11_)
    cdm11_->CloseSession(promise_id, session_id, session_id_size);
  else if (cdm10_)
    cdm10_->CloseSession(promise_id, session_id, session_id_size);
}

void CdmAdapter::RemoveSession(uint32_t promise_id,
  const char* session_id,
  uint32_t session_id_size)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->RemoveSession(promise_id, session_id, session_id_size);
  else if (cdm11_)
    cdm11_->RemoveSession(promise_id, session_id, session_id_size);
  else if (cdm10_)
    cdm10_->RemoveSession(promise_id, session_id, session_id_size);
}

void CdmAdapter::TimerExpired(void* context)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (m_isStoppingTimers)
    return;

  if (cdm12_)
    cdm12_->TimerExpired(context);
  else if (cdm11_)
    cdm11_->TimerExpired(context);
  else if (cdm10_)
    cdm10_->TimerExpired(context);
}

cdm::Status CdmAdapter::Decrypt(const cdm::InputBuffer_2& encrypted_buffer,
                                cdm::DecryptedBlock* decrypted_buffer,
                                cdm::StreamType streamType)
{
  //We need this wait here for fast systems, during buffering
  //widewine stopps if some seconds (5??) are fetched too fast
  //std::this_thread::sleep_for(std::chrono::milliseconds(5));

  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);

  active_buffer_ = decrypted_buffer->DecryptedBuffer();
  cdm::Status ret{cdm::Status::kDecryptError};

  if (cdm12_)
    ret = cdm12_->Decrypt(encrypted_buffer, decrypted_buffer);
  else if (cdm11_)
#ifdef TARGET_WEBOS
    ret = cdm11_->Decrypt(encrypted_buffer, decrypted_buffer, streamType);
#else
    ret = cdm11_->Decrypt(encrypted_buffer, decrypted_buffer);
#endif
  else if (cdm10_)
#ifdef TARGET_WEBOS
    ret = cdm10_->Decrypt(encrypted_buffer, decrypted_buffer, streamType);
#else
    ret = cdm10_->Decrypt(encrypted_buffer, decrypted_buffer);
#endif

  active_buffer_ = 0;
  return ret;
}

cdm::Status CdmAdapter::InitializeAudioDecoder(
  const cdm::AudioDecoderConfig_2& audio_decoder_config)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    return cdm12_->InitializeAudioDecoder(audio_decoder_config);
  else if (cdm11_)
    return cdm11_->InitializeAudioDecoder(audio_decoder_config);
  else if (cdm10_)
    return cdm10_->InitializeAudioDecoder(audio_decoder_config);

  return cdm::kDeferredInitialization;
}

cdm::Status CdmAdapter::InitializeVideoDecoder(
  const cdm::VideoDecoderConfig_3& video_decoder_config)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    return cdm12_->InitializeVideoDecoder(video_decoder_config);
  else if (cdm11_)
    return cdm11_->InitializeVideoDecoder(ToVideoDecoderConfig_2(video_decoder_config));
  else if (cdm10_)
    return cdm10_->InitializeVideoDecoder(ToVideoDecoderConfig_2(video_decoder_config));

  return cdm::kDeferredInitialization;
}

void CdmAdapter::DeinitializeDecoder(cdm::StreamType decoder_type)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->DeinitializeDecoder(decoder_type);
  else if (cdm11_)
    cdm11_->DeinitializeDecoder(decoder_type);
  else if (cdm10_)
    cdm10_->DeinitializeDecoder(decoder_type);
}

void CdmAdapter::ResetDecoder(cdm::StreamType decoder_type)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->ResetDecoder(decoder_type);
  else if (cdm11_)
    cdm11_->ResetDecoder(decoder_type);
  else if (cdm10_)
    cdm10_->ResetDecoder(decoder_type);
}

cdm::Status CdmAdapter::DecryptAndDecodeFrame(const cdm::InputBuffer_2& encrypted_buffer,
  CdmVideoFrame* video_frame)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  cdm::Status ret(cdm::kDeferredInitialization);

  if (cdm12_)
    ret = cdm12_->DecryptAndDecodeFrame(encrypted_buffer, video_frame);
  else if (cdm11_)
    ret = cdm11_->DecryptAndDecodeFrame(encrypted_buffer, video_frame);
  else if (cdm10_)
    ret = cdm10_->DecryptAndDecodeFrame(encrypted_buffer, video_frame);

  active_buffer_ = 0;
  return ret;
}

cdm::Status CdmAdapter::DecryptAndDecodeSamples(const cdm::InputBuffer_2& encrypted_buffer,
  cdm::AudioFrames* audio_frames)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    return cdm12_->DecryptAndDecodeSamples(encrypted_buffer, audio_frames);
  else if (cdm11_)
    return cdm11_->DecryptAndDecodeSamples(encrypted_buffer, audio_frames);
  else if (cdm10_)
    return cdm10_->DecryptAndDecodeSamples(encrypted_buffer, audio_frames);
  return cdm::kDeferredInitialization;
}

void CdmAdapter::OnPlatformChallengeResponse(
  const cdm::PlatformChallengeResponse& response)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->OnPlatformChallengeResponse(response);
  else if (cdm11_)
    cdm11_->OnPlatformChallengeResponse(response);
  else if (cdm10_)
    cdm10_->OnPlatformChallengeResponse(response);
}

void CdmAdapter::OnQueryOutputProtectionStatus(cdm::QueryResult result,
  uint32_t link_mask,
  uint32_t output_protection_mask)
{
  std::lock_guard<std::recursive_mutex> lock(cdm_mutex_);
  if (cdm12_)
    cdm12_->OnQueryOutputProtectionStatus(result, link_mask, output_protection_mask);
  else if (cdm11_)
    cdm11_->OnQueryOutputProtectionStatus(result, link_mask, output_protection_mask);
  else if (cdm10_)
    cdm10_->OnQueryOutputProtectionStatus(result, link_mask, output_protection_mask);
}

/******************************** HOST *****************************************/

cdm::Buffer* CdmAdapter::Allocate(uint32_t capacity)
{
  if (active_buffer_)
  return active_buffer_;
  else
  return client_->AllocateBuffer(capacity);
}

void CdmAdapter::timerfunc(CdmAdapter* adp, int64_t delay, void* context)
{
  {
    std::unique_lock<std::mutex> lock(m_timerMutex);
    const bool isStopping = m_timerCond.wait_for(lock, std::chrono::milliseconds(delay),
                                                 [this] { return m_isStoppingTimers.load(); });
    if (isStopping)
      return;
  }
  adp->TimerExpired(context);
}

void CdmAdapter::SetTimer(int64_t delay_ms, void* context)
{
  //LICENSERENEWAL
  // Called by the CDM, also from a timer thread inside TimerExpired
  std::lock_guard<std::mutex> lock(m_timerMutex);
  if (m_isStoppingTimers)
    return;

  // Clean up async tasks which have finished
  for (auto itFuture = m_asyncTimerTasks.begin(); itFuture != m_asyncTimerTasks.end();)
  {
    if (!itFuture->valid() ||
        itFuture->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
      itFuture = m_asyncTimerTasks.erase(itFuture);
    }
    else
      itFuture++;
  }
  std::future<void> future = std::async(std::launch::async, &CdmAdapter::timerfunc,
                                        shared_from_this(), this, delay_ms, context);
  m_asyncTimerTasks.push_back(std::move(future));
}

cdm::Time CdmAdapter::GetCurrentWallTime()
{
  cdm::Time res = static_cast<cdm::Time>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  return res / 1000.0;
}

void CdmAdapter::OnResolvePromise(uint32_t promise_id)
{
}

std::future<std::string> CdmAdapter::PrepareSessionFuture(uint32_t promiseId)
{
  std::lock_guard<std::mutex> lock(m_sessionMx);
  std::promise<std::string> promise;
  auto future = promise.get_future();
  m_sessionPromises[promiseId] = std::move(promise);
  return future;
}

void CdmAdapter::OnResolveNewSessionPromise(uint32_t promise_id,
                                            const char* session_id,
                                            uint32_t session_id_size)
{
  std::string sessionStr(session_id, session_id_size);

  std::lock_guard<std::mutex> lock(m_sessionMx);
  auto it = m_sessionPromises.find(promise_id);
  if (it != m_sessionPromises.end())
  {
    it->second.set_value(sessionStr);
    m_sessionPromises.erase(it);
  }
  else
  {
    Log(LogLevel::ERROR, "Promise ID %u not found", promise_id);
  }
}

void CdmAdapter::OnSessionKeysChange(const char* session_id,
                                     uint32_t session_id_size,
                                     bool has_additional_usable_key,
                                     const cdm::KeyInformation* keys_info,
                                     uint32_t keys_info_count)
{
  const std::string sessionId{session_id, session_id_size};
  std::vector<CdmAdapterClient::KeyInfo> keysInfo;

  for (uint32_t i(0); i < keys_info_count; ++i)
  {
    const cdm::KeyInformation cdmKeyInfo = keys_info[i];

    if (cdmKeyInfo.system_code != 0)
    {
      char buffer[128];
      char* bufferPtr{buffer};
      for (uint32_t j{0}; j < keys_info[i].key_id_size; ++j)
        bufferPtr += std::snprintf(bufferPtr, 3, "%02X", (int)keys_info[i].key_id[j]);
      Log(LogLevel::WARNING, "OnSessionKeysChange: KID %s with status %d has error system code: %u",
          buffer, cdmKeyInfo.status, cdmKeyInfo.system_code);
    }

    CdmAdapterClient::KeyInfo keyInfo;
    keyInfo.kid.assign(cdmKeyInfo.key_id, cdmKeyInfo.key_id + cdmKeyInfo.key_id_size);

    switch (cdmKeyInfo.status)
    {
      case cdm::KeyStatus::kUsable:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kUsable;
        break;
      case cdm::KeyStatus::kInternalError:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kInternalError;
        break;
      case cdm::KeyStatus::kExpired:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kExpired;
        break;
      case cdm::KeyStatus::kOutputRestricted:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kOutputRestricted;
        break;
      case cdm::KeyStatus::kOutputDownscaled:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kOutputDownscaled;
        break;
      case cdm::KeyStatus::kStatusPending:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kStatusPending;
        break;
      case cdm::KeyStatus::kReleased:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kReleased;
        break;
      default:
        Log(LogLevel::ERROR, "OnSessionKeysChange: Unhandled KeyStatus %i, KID ignored",
            cdmKeyInfo.status);
        continue;
    }

    keysInfo.emplace_back(keyInfo);
  }

  SendOnKeyStatusChange(sessionId, keysInfo);
}

void CdmAdapter::OnSessionKeysChange(const char* session_id,
                                     uint32_t session_id_size,
                                     bool has_additional_usable_key,
                                     const cdm::KeyInformation_2* keys_info,
                                     uint32_t keys_info_count)
{
  const std::string sessionId{session_id, session_id_size};
  std::vector<CdmAdapterClient::KeyInfo> keysInfo;

  for (uint32_t i(0); i < keys_info_count; ++i)
  {
    const cdm::KeyInformation_2 cdmKeyInfo = keys_info[i];

    if (cdmKeyInfo.system_code != 0)
    {
      char buffer[128];
      char* bufferPtr{buffer};
      for (uint32_t j{0}; j < keys_info[i].key_id_size; ++j)
        bufferPtr += std::snprintf(bufferPtr, 3, "%02X", (int)keys_info[i].key_id[j]);
      Log(LogLevel::WARNING, "OnSessionKeysChange: KID %s with status %d has error system code: %u",
          buffer, cdmKeyInfo.status, cdmKeyInfo.system_code);
    }

    CdmAdapterClient::KeyInfo keyInfo;
    keyInfo.kid.assign(cdmKeyInfo.key_id, cdmKeyInfo.key_id + cdmKeyInfo.key_id_size);

    switch (cdmKeyInfo.status)
    {
      case cdm::KeyStatus_2::kUsable:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kUsable;
        break;
      case cdm::KeyStatus_2::kInternalError:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kInternalError;
        break;
      case cdm::KeyStatus_2::kExpired:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kExpired;
        break;
      case cdm::KeyStatus_2::kOutputRestricted:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kOutputRestricted;
        break;
      case cdm::KeyStatus_2::kOutputDownscaled:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kOutputDownscaled;
        break;
      case cdm::KeyStatus_2::kStatusPending:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kStatusPending;
        break;
      case cdm::KeyStatus_2::kReleased:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kReleased;
        break;
      case cdm::KeyStatus_2::kUsableInFuture:
        keyInfo.status = CdmAdapterClient::CDMKeyStatus::kUsableInFuture;
        break;
      default:
        Log(LogLevel::ERROR, "OnSessionKeysChange: Unhandled KeyStatus_2 %i, KID ignored",
            cdmKeyInfo.status);
        continue;
    }

    keysInfo.emplace_back(keyInfo);
  }

  SendOnKeyStatusChange(sessionId, keysInfo);
}

void CdmAdapter::OnExpirationChange(const char* session_id,
                  uint32_t session_id_size,
                  cdm::Time new_expiry_time)
{
  SendClientMessage(session_id, session_id_size, CdmAdapterClient::kSessionExpired, nullptr, 0, 0);
}

void CdmAdapter::OnSessionClosed(const char* session_id,
                 uint32_t session_id_size)
{
  SendClientMessage(session_id, session_id_size, CdmAdapterClient::kSessionClosed, nullptr, 0, 0);
}

void CdmAdapter::SendPlatformChallenge(const char* service_id,
                                    uint32_t service_id_size,
                                    const char* challenge,
                                    uint32_t challenge_size)
{
}

void CdmAdapter::EnableOutputProtection(uint32_t desired_protection_mask)
{
  QueryOutputProtectionStatus();
}

void CdmAdapter::QueryOutputProtectionStatus()
{
  OnQueryOutputProtectionStatus(cdm::kQuerySucceeded, cdm::kLinkTypeInternal, cdm::kProtectionHDCP);
}

void CdmAdapter::OnDeferredInitializationDone(cdm::StreamType stream_type,
                        cdm::Status decoder_status)
{
}

// The CDM owns the returned object and must call FileIO::Close() to release it.
cdm::FileIO* CdmAdapter::CreateFileIO(cdm::FileIOClient* client)
{
  return new CdmFileIoImpl(cdm_base_path_, client);
}

void CdmAdapter::OnResolveKeyStatusPromise(uint32_t promise_id, cdm::KeyStatus key_status)
{
}

void CdmAdapter::OnResolveKeyStatusPromise(uint32_t promise_id, cdm::KeyStatus_2 key_status)
{
}

void CdmAdapter::OnRejectPromise(uint32_t promise_id,
                                 cdm::Exception exception,
                                 uint32_t system_code,
                                 const char* error_message,
                                 uint32_t error_message_size)
{
  std::lock_guard<std::mutex> lk(m_sessionMx);
  auto it = m_sessionPromises.find(promise_id);
  if (it == m_sessionPromises.end())
    return;

  try
  {
    std::string msg;
    if (error_message && error_message_size)
    {
      msg.assign(error_message, error_message_size);
    }
    else
    {
      msg = "CDM reject: " + std::to_string(static_cast<int>(exception)) +
            " sys=" + std::to_string(system_code);
    }

    it->second.set_exception(std::make_exception_ptr(std::runtime_error(msg)));
  }
  catch (...)
  {
  }

  m_sessionPromises.erase(it);
}

void CdmAdapter::OnSessionMessage(const char* session_id, uint32_t session_id_size,
  cdm::MessageType message_type, const char* message, uint32_t message_size)
{
  SendClientMessage(session_id, session_id_size, CdmAdapterClient::kSessionMessage, reinterpret_cast<const uint8_t*>(message), message_size, 0);
}

void CdmAdapter::RequestStorageId(uint32_t version)
{
  if (cdm12_)
    cdm12_->OnStorageId(version, nullptr, 0);
  else if (cdm11_)
    cdm11_->OnStorageId(version, nullptr, 0);
  else if (cdm10_)
    cdm10_->OnStorageId(version, nullptr, 0);
}

void CdmAdapter::ReportMetrics(cdm::MetricName metric_name, uint64_t value)
{
}

void CdmAdapter::OnInitialized(bool success)
{
  // Notify the client that the CDM is initialized
  if (success)
  {
    m_provisioningCompleteOrStarted = true;
  }

  // Set the promise value so that execution can continue
  m_initPromise.set_value();

  Log(LogLevel::DEBUG, "CDM is initialized: %s", success ? "true" : "false");
}


/*******************************         CdmFileIoImpl        ****************************************/

CdmFileIoImpl::CdmFileIoImpl(std::string base_path, cdm::FileIOClient* client)
  : base_path_(base_path)
  , client_(client)
  , file_descriptor_(0)
  , data_buffer_(0)
  , opened_(false)
{
}

void CdmFileIoImpl::Open(const char* file_name, uint32_t file_name_size)
{
  if (!opened_)
  {
    opened_ = true;
    m_filepath.assign(file_name, file_name_size);
    m_filepath = base_path_ + m_filepath;
    client_->OnOpenComplete(cdm::FileIOClient::Status::kSuccess);
  }
  else
    client_->OnOpenComplete(cdm::FileIOClient::Status::kInUse);
}

void CdmFileIoImpl::Read()
{
  cdm::FileIOClient::Status status(cdm::FileIOClient::Status::kError);
  size_t sz(0);

  free(reinterpret_cast<void*>(data_buffer_));
  data_buffer_ = nullptr;

  file_descriptor_ = fopen(m_filepath.c_str(), "rb");

  if (file_descriptor_)
  {
    status = cdm::FileIOClient::Status::kSuccess;
    fseek(file_descriptor_, 0, SEEK_END);
    sz = ftell(file_descriptor_);
    if (sz)
    {
      fseek(file_descriptor_, 0, SEEK_SET);
      if ((data_buffer_ = reinterpret_cast<uint8_t*>(malloc(sz))) == nullptr || fread(data_buffer_, 1, sz, file_descriptor_) != sz)
      status = cdm::FileIOClient::Status::kError;
    }
  } else
    status = cdm::FileIOClient::Status::kSuccess;
  client_->OnReadComplete(status, data_buffer_, static_cast<uint32_t>(sz));
}

void CdmFileIoImpl::Write(const uint8_t* data, uint32_t data_size)
{
  if (!ExistsDir(base_path_.c_str()) && !CreateDirs(base_path_.c_str()))
  {
    Log(LogLevel::ERROR, "Cannot create directory: %s", base_path_.c_str());
    client_->OnWriteComplete(cdm::FileIOClient::Status::kError);
    return;
  }

  cdm::FileIOClient::Status status = cdm::FileIOClient::Status::kError;

  file_descriptor_ = fopen(m_filepath.c_str(), "wb");

  if (file_descriptor_)
  {
    if (fwrite(data, 1, data_size, file_descriptor_) == data_size)
    {
      status = cdm::FileIOClient::Status::kSuccess;
    }
  }

  client_->OnWriteComplete(status);
}

void CdmFileIoImpl::Close()
{
  if (file_descriptor_)
  {
    fclose(file_descriptor_);
    file_descriptor_ = 0;
  }
  client_ = 0;
  free(reinterpret_cast<void*>(data_buffer_));
  data_buffer_ = 0;
  delete this;
}

}  // namespace media

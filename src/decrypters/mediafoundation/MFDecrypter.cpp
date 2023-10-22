#include "MFDecrypter.h"
#include "MFCencSingleSampleDecrypter.h"
#include "../../utils/Base64Utils.h"
#include "../../utils/log.h"

#include <mfcdm/MediaFoundationCdm.h>

#include <kodi/Filesystem.h>
#include <mfcdm/Log.h>

using namespace UTILS;
using namespace DRM;
using namespace kodi::tools;

namespace
{
void MFLog(int level, char* msg)
{
  if (msg[std::strlen(msg) - 1] == '\n')
    msg[std::strlen(msg) - 1] = '\0';

  switch (level)
  {
    case MFCDM::MFLOG_ERROR:
      LOG::Log(LOGERROR, msg);
      break;
    case MFCDM::MFLOG_WARN:
      LOG::Log(LOGWARNING, msg);
      break;
    case MFCDM::MFLOG_INFO:
      LOG::Log(LOGINFO, msg);
      break;
    case MFCDM::MFLOG_DEBUG:
      LOG::Log(LOGDEBUG, msg);
      break;
    default:
      break;
  }
}
} // unnamed namespace


CMFDecrypter::CMFDecrypter()
  : m_cdm(nullptr)
{
  MFCDM::LogAll();
  MFCDM::SetMFMsgCallback(MFLog);
}

CMFDecrypter::~CMFDecrypter()
{
  delete m_cdm;
}

bool CMFDecrypter::Initialize()
{
  m_cdm = new MediaFoundationCdm();
  return m_cdm != nullptr;
}

std::string CMFDecrypter::SelectKeySytem(std::string_view keySystem)
{
  if (keySystem == "com.microsoft.playready")
    return "urn:uuid:9A04F079-9840-4286-AB92-E65BE0885F95";
  return "";
}

bool CMFDecrypter::OpenDRMSystem(std::string_view licenseURL,
                                 const std::vector<uint8_t>& serverCertificate,
                                 const uint8_t config)
{
  if (!m_cdm)
    return false;

  if (!(config & DRM::IDecrypter::CONFIG_PERSISTENTSTORAGE))
  {
    LOG::Log(LOGERROR, "MF PlayReady requires persistent storage to be optionally on or required.");
    return false;
  }

  m_strLicenseKey = licenseURL;

  return m_cdm->Initialize({true, true}, "com.microsoft.playready.recommendation",
                           m_strProfilePath);
}

Adaptive_CencSingleSampleDecrypter* CMFDecrypter::CreateSingleSampleDecrypter(
    std::vector<uint8_t>& pssh,
    std::string_view optionalKeyParameter,
    std::string_view defaultKeyId,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  CMFCencSingleSampleDecrypter* decrypter = new CMFCencSingleSampleDecrypter(
      *this, pssh, defaultKeyId, skipSessionMessage, cryptoMode);
  if (!decrypter->GetSessionId())
  {
    delete decrypter;
    decrypter = nullptr;
  }
  return decrypter;
}

void CMFDecrypter::DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (decrypter)
  {
    // close session before dispose
    dynamic_cast<CMFCencSingleSampleDecrypter*>(decrypter)->CloseSessionId();
    delete dynamic_cast<CMFCencSingleSampleDecrypter*>(decrypter);
  }
}

void CMFDecrypter::GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                                   std::string_view keyId,
                                   uint32_t media,
                                   IDecrypter::DecrypterCapabilites& caps)
{
  if (!decrypter)
  {
    caps = {0, 0, 0};
    return;
  }

  dynamic_cast<CMFCencSingleSampleDecrypter*>(decrypter)->GetCapabilities(keyId, media, caps);
}

bool CMFDecrypter::HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                                 std::string_view keyId)
{
  if (decrypter)
    return dynamic_cast<CMFCencSingleSampleDecrypter*>(decrypter)->HasKeyId(keyId);
  return false;
}

std::string CMFDecrypter::GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (!decrypter)
    return "";

  AP4_DataBuffer challengeData =
      dynamic_cast<CMFCencSingleSampleDecrypter*>(decrypter)->GetChallengeData();
  return BASE64::Encode(challengeData.GetData(), challengeData.GetDataSize());
}

bool CMFDecrypter::OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                    const VIDEOCODEC_INITDATA* initData)
{
  if (!decrypter || !initData)
    return false;

  m_decodingDecrypter = dynamic_cast<CMFCencSingleSampleDecrypter*>(decrypter);
  return m_decodingDecrypter->OpenVideoDecoder(initData);
}

VIDEOCODEC_RETVAL CMFDecrypter::DecryptAndDecodeVideo(
    kodi::addon::CInstanceVideoCodec* codecInstance, const DEMUX_PACKET* sample)
{
  if (!m_decodingDecrypter)
    return VC_ERROR;

  return m_decodingDecrypter->DecryptAndDecodeVideo(codecInstance, sample);
}

VIDEOCODEC_RETVAL CMFDecrypter::VideoFrameDataToPicture(
    kodi::addon::CInstanceVideoCodec* codecInstance, VIDEOCODEC_PICTURE* picture)
{
  if (!m_decodingDecrypter)
    return VC_ERROR;

  return m_decodingDecrypter->VideoFrameDataToPicture(codecInstance, picture);
}

void CMFDecrypter::ResetVideo()
{
  if (m_decodingDecrypter)
    m_decodingDecrypter->ResetVideo();
}

void CMFDecrypter::SetProfilePath(const std::string& profilePath)
{
  m_strProfilePath = profilePath;

  const char* pathSep{profilePath[0] && profilePath[1] == ':' && isalpha(profilePath[0]) ? "\\"
                                                                                         : "/"};

  if (!m_strProfilePath.empty() && m_strProfilePath.back() != pathSep[0])
    m_strProfilePath += pathSep;

  //let us make cdm userdata out of the addonpath and share them between addons
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 2));
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1));
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1) +
                          1);

  kodi::vfs::CreateDirectory(m_strProfilePath);
  m_strProfilePath += "cdm";
  m_strProfilePath += pathSep;
  kodi::vfs::CreateDirectory(m_strProfilePath);
}

/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaFoundationCdmFactory.h"

#include "MediaFoundationCdmModule.h"
#include "MediaFoundationCdm.h"
#include "utils/ScopedPropVariant.h"
#include "utils/Wide.h"
#include "Log.h"

#include <propsys.h>
#include <propvarutil.h>

using namespace UTILS;

static void InitPropVariantFromBSTR(const wchar_t* str, PROPVARIANT* propVariant)
{
    propVariant->vt = VT_BSTR;
    propVariant->bstrVal = SysAllocString(str);
}

MediaFoundationCdmFactory::MediaFoundationCdmFactory(std::string_view keySystem)
  : m_keySystem(keySystem)
{
}

bool MediaFoundationCdmFactory::Initialize()
{
    const winrt::com_ptr<IMFMediaEngineClassFactory4> classFactory = winrt::create_instance<IMFMediaEngineClassFactory4>(
          CLSID_MFMediaEngineClassFactory, CLSCTX_INPROC_SERVER);
    const std::wstring keySystemWide = ConvertUtf8ToWide(m_keySystem);

    return SUCCEEDED(classFactory->CreateContentDecryptionModuleFactory(
      keySystemWide.c_str(), IID_PPV_ARGS(&m_cdmFactory)));
}

bool MediaFoundationCdmFactory::IsTypeSupported(std::string_view keySystem) const
{
    return m_cdmFactory->IsTypeSupported(ConvertUtf8ToWide(keySystem).c_str(), nullptr);
}

/*!
 * \brief Returns a property store similar to EME MediaKeySystemMediaCapability.
 */
bool CreateVideoCapability(const MediaFoundationCdmConfig& cdm_config,
                           winrt::com_ptr<IPropertyStore>& video_capability)
{
    winrt::com_ptr<IPropertyStore> temp_video_capability;
    if(FAILED(PSCreateMemoryPropertyStore(IID_PPV_ARGS(&temp_video_capability))))
    {
      Log(MFCDM::MFLOG_ERROR, "Failed to create property store for video capabilities.");
      return false;
    }

    if (cdm_config.use_hw_secure_codecs) 
    {
        ScopedPropVariant robustness;
        robustness->vt = VT_BSTR;
        robustness->bstrVal = SysAllocString(L"HW_SECURE_ALL");
        temp_video_capability->SetValue(MF_EME_ROBUSTNESS, robustness.get());
    }

    video_capability = temp_video_capability;
    return true;
}

/*!
 * \brief Creates a IPropertyStore for CDM based on cdm config settings.
 * \link https://github.com/chromium/chromium/blob/ea198b54e3f6b0cfdd6bacbb01c2307fd1797b63/media/cdm/win/media_foundation_cdm_util.cc#L68
 * \link https://github.com/microsoft/media-foundation/blob/969f38b9fff9892f5d75bc353c72d213da807739/samples/MediaEngineEMEUWPSample/src/media/eme/MediaKeySystemConfiguration.cpp#L74
 */
bool BuildCdmAccessConfigurations(const MediaFoundationCdmConfig& cdmConfig,
                                  winrt::com_ptr<IPropertyStore>& properties)
{
    winrt::com_ptr<IPropertyStore> temp_configurations;
    if (FAILED(PSCreateMemoryPropertyStore(IID_PPV_ARGS(&temp_configurations))))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create property store for cdm access.");
        return false;
    }

    // Add an empty audio capability.
    ScopedPropVariant audio_capabilities;
    audio_capabilities->vt = VT_VARIANT | VT_VECTOR;
    audio_capabilities->capropvar.cElems = 0;
    if (FAILED(temp_configurations->SetValue(MF_EME_AUDIOCAPABILITIES,
                                             audio_capabilities.get())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to set audio capabilities.");
        return false;
    }

    // Add a video capability.
    winrt::com_ptr<IPropertyStore> video_capability;
    if (!CreateVideoCapability(cdmConfig, video_capability))
        return false;

    ScopedPropVariant videoConfig;
    videoConfig->vt = VT_UNKNOWN;
    videoConfig->punkVal = video_capability.detach();

    ScopedPropVariant videoCapabilities;
    videoCapabilities->vt = VT_VARIANT | VT_VECTOR;
    videoCapabilities->capropvar.cElems = 1;
    videoCapabilities->capropvar.pElems = static_cast<PROPVARIANT*>(CoTaskMemAlloc(sizeof(PROPVARIANT)));
    if (!videoCapabilities->capropvar.pElems)
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to allocate video capability array.");
        return false;
    }

    if (FAILED(PropVariantCopy(videoCapabilities->capropvar.pElems, videoConfig.ptr())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to set copy video config into video capabilities.");
        return false;
    }

    if (FAILED(temp_configurations->SetValue(MF_EME_VIDEOCAPABILITIES, videoCapabilities.get())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to set persisted state.");
        return false;
    }

    // Persistent state
    ScopedPropVariant persisted_state;
    if (FAILED(InitPropVariantFromUInt32(cdmConfig.allow_persistent_state
                                                ? MF_MEDIAKEYS_REQUIREMENT_REQUIRED
                                                : MF_MEDIAKEYS_REQUIREMENT_NOT_ALLOWED, 
                                         persisted_state.ptr())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create prop variant for persistent state.");
        return false;
    }

    if (FAILED(temp_configurations->SetValue(MF_EME_PERSISTEDSTATE, persisted_state.get())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to set persisted state.");
        return false;
    }

    // Distinctive ID
    ScopedPropVariant allow_distinctive_identifier;
    if (FAILED(InitPropVariantFromUInt32(cdmConfig.allow_distinctive_identifier
                                                ? MF_MEDIAKEYS_REQUIREMENT_REQUIRED
                                                : MF_MEDIAKEYS_REQUIREMENT_NOT_ALLOWED,
                                         allow_distinctive_identifier.ptr())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create prop variant for distinctive identifier.");
        return false;
    }

    if (FAILED(temp_configurations->SetValue(MF_EME_DISTINCTIVEID,
                                             allow_distinctive_identifier.get())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to set distinctive identifier.");
        return false;
    }

    properties = temp_configurations;
    return true;
}

bool BuildCdmProperties(const std::filesystem::path& storePath,
                        winrt::com_ptr<IPropertyStore>& properties)
{
    winrt::com_ptr<IPropertyStore> temp_properties;
    if (FAILED(PSCreateMemoryPropertyStore(IID_PPV_ARGS(&temp_properties))))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create property store for cdm properties.");
        return false;
    }

    ScopedPropVariant storePathVar;
    InitPropVariantFromBSTR(storePath.wstring().c_str(), storePathVar.ptr());

    if (FAILED(temp_properties->SetValue(MF_EME_CDM_STOREPATH, storePathVar.get())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to set CDM Storage Path.");
        return false;
    }

    properties = temp_properties;
    return true;
}

bool MediaFoundationCdmFactory::CreateMfCdm(const MediaFoundationCdmConfig& cdmConfig,
                                            const std::filesystem::path& cdmPath,
                                            std::unique_ptr<MediaFoundationCdmModule>& mfCdm) const
{
    const auto key_system_str = ConvertUtf8ToWide(m_keySystem);
    if (!m_cdmFactory->IsTypeSupported(key_system_str.c_str(), nullptr))
    {
        Log(MFCDM::MFLOG_ERROR, "%s is not supported by MF CdmFactory", m_keySystem);
        return false;
    }

    winrt::com_ptr<IPropertyStore> cdmConfigProp;
    if (!BuildCdmAccessConfigurations(cdmConfig, cdmConfigProp))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to build cdm access configuration.");
        return false;
    }

    winrt::com_ptr<IMFContentDecryptionModuleAccess> cdmAccess;
    IPropertyStore* configurations[] = {cdmConfigProp.get()};
    if (FAILED(m_cdmFactory->CreateContentDecryptionModuleAccess(
               key_system_str.c_str(), configurations, ARRAYSIZE(configurations), cdmAccess.put())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create module access.");
        return false;
    }
  
    // Ensure path exists to the cdm path.
    if (!std::filesystem::create_directory(cdmPath) && !std::filesystem::exists(cdmPath))
    {
        Log(MFCDM::MFLOG_ERROR, "CDM Path %s doesn't exist.", cdmPath.string());
        return false;
    }

    winrt::com_ptr<IPropertyStore> cdmProperties;
    if (!BuildCdmProperties(cdmPath, cdmProperties))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to build cdm properties.");
        return false;
    }

    winrt::com_ptr<IMFContentDecryptionModule> cdm;
    if (FAILED(cdmAccess->CreateContentDecryptionModule(cdmProperties.get(), cdm.put())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create cdm module.");
        return false;
    }

    mfCdm = std::make_unique<MediaFoundationCdmModule>(cdm);
    return true;
}

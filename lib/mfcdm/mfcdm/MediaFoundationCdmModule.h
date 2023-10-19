#include <unknwn.h>
#include <winrt/base.h>
#include <mfcontentdecryptionmodule.h>

/*!
 * \brief Wrapper around winrt::com_ptr of IMFContentDecryptionModule
 *
 * This is to prevent imports to Windows API & MF in MediaFoundationCdm.h
 */
class MediaFoundationCdmModule
{
public:
  ~MediaFoundationCdmModule() = default;

  MediaFoundationCdmModule(winrt::com_ptr<IMFContentDecryptionModule>& cdmModule)
  {
    std::swap(m_mfCdm, cdmModule);
  }

  inline HRESULT SetServerCertificate(const uint8_t* server_certificate_data,
                                      uint32_t server_certificate_data_size) const
  {
    return m_mfCdm->SetServerCertificate(server_certificate_data, server_certificate_data_size);
  }

  inline HRESULT SetPMPHostApp(IMFPMPHostApp* pmpHostApp) const
  {
    return m_mfCdm->SetPMPHostApp(pmpHostApp);
  }

  inline HRESULT CreateSession(MF_MEDIAKEYSESSION_TYPE sessionType,
                               IMFContentDecryptionModuleSessionCallbacks* callbacks,
                               IMFContentDecryptionModuleSession** session) const
  {
    return m_mfCdm->CreateSession(sessionType, callbacks, session);
  }

  template<typename To>
  inline winrt::com_ptr<To> As() const
  {
    return m_mfCdm.as<To>();
  };

private:
  winrt::com_ptr<IMFContentDecryptionModule> m_mfCdm;
};

/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <unknwn.h>
#include <winrt/base.h>
#include <mfidl.h>
#include <mfapi.h>

EXTERN_GUID(GUID_ObjectStream, 0x3e73735c, 0xe6c0, 0x481d, 0x82, 0x60, 0xee, 0x5d, 0xb1, 0x34, 0x3b, 0x5f);
EXTERN_GUID(GUID_ClassName, 0x77631a31, 0xe5e7, 0x4785, 0xbf, 0x17, 0x20, 0xf5, 0x7b, 0x22, 0x48, 0x02);
EXTERN_GUID(CLSID_EMEStoreActivate, 0x2df7b51e, 0x797b, 0x4d06, 0xbe, 0x71, 0xd1, 0x4a, 0x52, 0xcf, 0x84, 0x21);

class PMPHostWrapper : public winrt::implements<PMPHostWrapper, IMFPMPHostApp> {
public:
    explicit PMPHostWrapper(winrt::com_ptr<IMFPMPHost>& host) {
        std::swap(host, m_spIMFPMPHost);
    }
    ~PMPHostWrapper() override = default;

    IFACEMETHODIMP LockProcess() override {
        return m_spIMFPMPHost->LockProcess();
    }

    IFACEMETHODIMP UnlockProcess() override {
        return m_spIMFPMPHost->UnlockProcess();
    }

    IFACEMETHODIMP ActivateClassById(LPCWSTR id, IStream* stream, REFIID riid, void** activated_class) override {
        HRESULT ret;

        wchar_t guid[MAX_PATH] = {};
        StringFromGUID2(riid, guid, std::size(guid));

        winrt::com_ptr<IMFAttributes> creation_attributes;
        ret = MFCreateAttributes(creation_attributes.put(), 3);
        if (FAILED(ret))
            return ret;
        ret = creation_attributes->SetString(GUID_ClassName, id);
        if (FAILED(ret))
            return ret;

        if (stream) {
            STATSTG statstg;
            ret = stream->Stat(&statstg, STATFLAG_NOOPEN | STATFLAG_NONAME);
            if (FAILED(ret))
              return ret;

            std::vector<uint8_t> stream_blob(statstg.cbSize.LowPart);
            unsigned long read_size = 0;

            ret = stream->Read(&stream_blob[0], stream_blob.size(), &read_size);
            if (FAILED(ret))
              return ret;
            ret = creation_attributes->SetBlob(GUID_ObjectStream, &stream_blob[0], read_size);
            if (FAILED(ret))
              return ret;
        }

        // Serialize attributes
        winrt::com_ptr<IStream> output_stream;
        ret = CreateStreamOnHGlobal(nullptr, TRUE, output_stream.put());
        if (FAILED(ret))
            return ret;
        ret = MFSerializeAttributesToStream(creation_attributes.get(), 0, output_stream.get());
        if (FAILED(ret))
            return ret;
        ret = output_stream->Seek({}, STREAM_SEEK_SET, nullptr);
        if (FAILED(ret))
            return ret;

        winrt::com_ptr<IMFActivate> activator;
        ret = m_spIMFPMPHost->CreateObjectByCLSID(CLSID_EMEStoreActivate, output_stream.get(),
                                            IID_PPV_ARGS(&activator));
        if (FAILED(ret))
            return ret;
        ret = activator->ActivateObject(riid, activated_class);
        if (FAILED(ret))
            return ret;
        return S_OK;
    }
private:
    winrt::com_ptr<IMFPMPHost> m_spIMFPMPHost;
};

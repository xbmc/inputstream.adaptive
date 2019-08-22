/*****************************************************************
|
|    AP4 - Stdc File Byte Stream implementation
|
|    Copyright 2002-2008 Axiomatic Systems, LLC
|
|
|    This file is part of Bento4/AP4 (MP4 Atom Processing Library).
|
|    Unless you have obtained Bento4 under a difference license,
|    this version of Bento4 is Bento4|GPL.
|    Bento4|GPL is free software; you can redistribute it and/or modify
|    it under the terms of the GNU General Public License as published by
|    the Free Software Foundation; either version 2, or (at your option)
|    any later version.
|
|    Bento4|GPL is distributed in the hope that it will be useful,
|    but WITHOUT ANY WARRANTY; without even the implied warranty of
|    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
|    GNU General Public License for more details.
|
|    You should have received a copy of the GNU General Public License
|    along with Bento4|GPL; see the file COPYING.  If not, write to the
|    Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
|    02111-1307, USA.
|
****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <string.h>
#if !defined(_WIN32_WCE)
#include <errno.h>
#include <sys/stat.h>
#endif

#include "Ap4FileByteStream.h"

#include <kodi/Filesystem.h>

/*----------------------------------------------------------------------
|   compatibility wrappers
+---------------------------------------------------------------------*/
#if !defined(ENOENT)
#define ENOENT 2
#endif
#if !defined(EACCES)
#define EACCES 13
#endif

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream
+---------------------------------------------------------------------*/
class AP4_KodiFileByteStream: public AP4_ByteStream
{
public:
    // class methods
    static AP4_Result Create(AP4_FileByteStream*      delegator,
                             const char*              name,
                             AP4_FileByteStream::Mode mode,
                             AP4_ByteStream*&         stream);
                      
    // methods
    AP4_KodiFileByteStream(AP4_FileByteStream* delegator,
                           kodi::vfs::CFile*   file, 
                           AP4_LargeSize       size);
    
    ~AP4_KodiFileByteStream();

    // AP4_ByteStream methods
    AP4_Result ReadPartial(void*     buffer, 
                           AP4_Size  bytesToRead, 
                           AP4_Size& bytesRead);
    AP4_Result WritePartial(const void* buffer, 
                            AP4_Size    bytesToWrite, 
                            AP4_Size&   bytesWritten);
    AP4_Result Seek(AP4_Position position);
    AP4_Result Tell(AP4_Position& position);
    AP4_Result GetSize(AP4_LargeSize& size);
    AP4_Result Flush();

    // AP4_Referenceable methods
    void AddReference();
    void Release();

private:
    // members
    AP4_ByteStream* m_Delegator;
    AP4_Cardinal    m_ReferenceCount;
    kodi::vfs::CFile* m_File;
    AP4_Position    m_Position;
    AP4_LargeSize   m_Size;
};

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::Create
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::Create(AP4_FileByteStream*      delegator,
                               const char*              name, 
                               AP4_FileByteStream::Mode mode, 
                               AP4_ByteStream*&         stream)
{
    // default value
    stream = NULL;
    
    // check arguments
    if (name == NULL) return AP4_ERROR_INVALID_PARAMETERS;
    
    // open the file
    kodi::vfs::CFile* file = new kodi::vfs::CFile;
    AP4_Position size = 0;
    bool ret = false;
    switch (mode) {
        case AP4_FileByteStream::STREAM_MODE_READ:
            ret = file->OpenFile(name, 0);
            break;

        case AP4_FileByteStream::STREAM_MODE_WRITE:
            ret = file->OpenFileForWrite(name, true);
            break;

        case AP4_FileByteStream::STREAM_MODE_READ_WRITE:
            ret = file->OpenFileForWrite(name, false);
            break;                                  

        default:
            delete file;
            return AP4_ERROR_INVALID_PARAMETERS;
    }

    if (!ret) {
        delete file;
        return AP4_ERROR_CANNOT_OPEN_FILE;
    }

    size = file->GetLength();

    stream = new AP4_KodiFileByteStream(delegator, file, size);
    return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::AP4_KodiFileByteStream
+---------------------------------------------------------------------*/
AP4_KodiFileByteStream::AP4_KodiFileByteStream(AP4_FileByteStream* delegator,
                                               kodi::vfs::CFile*   file,
                                               AP4_LargeSize       size) :
    m_Delegator(delegator),
    m_ReferenceCount(1),
    m_File(file),
    m_Position(0),
    m_Size(size)
{
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::~AP4_KodiFileByteStream
+---------------------------------------------------------------------*/
AP4_KodiFileByteStream::~AP4_KodiFileByteStream()
{
    if (m_File) {
        delete m_File;
    }
}

/*----------------------------------------------------------------------
|   AP4_StdcFileByteStream::AddReference
+---------------------------------------------------------------------*/
void
AP4_KodiFileByteStream::AddReference()
{
    m_ReferenceCount++;
}

/*----------------------------------------------------------------------
|   AP4_StdcFileByteStream::Release
+---------------------------------------------------------------------*/
void
AP4_KodiFileByteStream::Release()
{
    if (--m_ReferenceCount == 0) {
        if (m_Delegator) {
            delete m_Delegator;
        } else {
            delete this;
        }
    }
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::ReadPartial
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::ReadPartial(void*     buffer, 
                                    AP4_Size  bytesToRead, 
                                    AP4_Size& bytesRead)
{
    size_t nbRead;

    nbRead = m_File->Read(buffer, bytesToRead);

    if (nbRead > 0) {
        bytesRead = (AP4_Size)nbRead;
        m_Position += nbRead;
        return AP4_SUCCESS;
    } else if (m_Position == m_Size) {
        bytesRead = 0;
        return AP4_ERROR_EOS;
    } else {
        bytesRead = 0;
        return AP4_ERROR_READ_FAILED;
    }
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::WritePartial
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::WritePartial(const void* buffer, 
                                     AP4_Size    bytesToWrite, 
                                     AP4_Size&   bytesWritten)
{
    size_t nbWritten;

    if (bytesToWrite == 0) return AP4_SUCCESS;
    nbWritten = m_File->Write(buffer, bytesToWrite);
    
    if (nbWritten > 0) {
        bytesWritten = (AP4_Size)nbWritten;
        m_Position += nbWritten;
        return AP4_SUCCESS;
    } else {
        bytesWritten = 0;
        return AP4_ERROR_WRITE_FAILED;
    }
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::Seek
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::Seek(AP4_Position position)
{
    // shortcut
    if (position == m_Position) return AP4_SUCCESS;
    
    std::int64_t result;
    result = m_File->Seek(position, SEEK_SET);
    if (result == position) {
        m_Position = position;
        return AP4_SUCCESS;
    } else {
        return AP4_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::Tell
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::Tell(AP4_Position& position)
{
    position = m_Position;
    return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::GetSize
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::GetSize(AP4_LargeSize& size)
{
    size = m_Size;
    return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_KodiFileByteStream::Flush
+---------------------------------------------------------------------*/
AP4_Result
AP4_KodiFileByteStream::Flush()
{
    int ret_val = 0;
    m_File->Flush();
	AP4_Result result((ret_val > 0) ? AP4_FAILURE : AP4_SUCCESS);
	if (AP4_SUCCEEDED(result) && GetObserver())
		return GetObserver()->OnFlush(this);
	return result;
}

/*----------------------------------------------------------------------
|   AP4_FileByteStream::Create
+---------------------------------------------------------------------*/
AP4_Result
AP4_FileByteStream::Create(const char*              name, 
                           AP4_FileByteStream::Mode mode,
                           AP4_ByteStream*&         stream)
{
    return AP4_KodiFileByteStream::Create(NULL, name, mode, stream);
}

#if !defined(AP4_CONFIG_NO_EXCEPTIONS)
/*----------------------------------------------------------------------
|   AP4_FileByteStream::AP4_FileByteStream
+---------------------------------------------------------------------*/
AP4_FileByteStream::AP4_FileByteStream(const char*              name, 
                                       AP4_FileByteStream::Mode mode)
{
    AP4_ByteStream* stream = NULL;
    AP4_Result result = AP4_KodiFileByteStream::Create(this, name, mode, stream);
    if (AP4_FAILED(result)) throw AP4_Exception(result);
    
    m_Delegate = stream;
}
#endif












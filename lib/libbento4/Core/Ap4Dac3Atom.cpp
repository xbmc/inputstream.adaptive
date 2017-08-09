/*****************************************************************
|
|    AP4 - dec3 Atoms
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
#include "Ap4Dac3Atom.h"
#include "Ap4AtomFactory.h"
#include "Ap4Utils.h"
#include "Ap4Types.h"

/*----------------------------------------------------------------------
|   dynamic cast support
+---------------------------------------------------------------------*/
AP4_DEFINE_DYNAMIC_CAST_ANCHOR(AP4_Dac3Atom)

/*----------------------------------------------------------------------
|   AP4_Dac3Atom::Create
+---------------------------------------------------------------------*/
AP4_Dac3Atom* 
AP4_Dac3Atom::Create(AP4_Size size, AP4_ByteStream& stream)
{
    // read the raw bytes in a buffer
    unsigned int payload_size = size-AP4_ATOM_HEADER_SIZE;
    AP4_DataBuffer payload_data(payload_size);
    AP4_Result result = stream.Read(payload_data.UseData(), payload_size);
    if (AP4_FAILED(result)) return NULL;
    
    // check the version
    const AP4_UI08* payload = payload_data.GetData();
    return new AP4_Dac3Atom(size, payload);
}

/*----------------------------------------------------------------------
|   AP4_Dac3Atom::AP4_Dac3Atom
+---------------------------------------------------------------------*/
AP4_Dac3Atom::AP4_Dac3Atom(AP4_UI32 size, const AP4_UI08* payload) :
    AP4_Atom(AP4_ATOM_TYPE_DAC3, size),
  m_bsmod(0),
  m_acmod(0),
  m_lfeon(0)
{
  // make a copy of our configuration bytes
  unsigned int payload_size = size-AP4_ATOM_HEADER_SIZE;
  m_RawBytes.SetData(payload, payload_size);

  // sanity check
  if (payload_size < 2) return;
    
  // parse the payload
  m_bsmod = (payload[1] >> 6) & 0x7;
  m_acmod = (payload[1] >> 3) & 0x7;
  m_lfeon = (payload[1] >> 2) & 0x1;
}

/*----------------------------------------------------------------------
|   AP4_Dac3Atom::WriteFields
+---------------------------------------------------------------------*/
AP4_Result
AP4_Dac3Atom::WriteFields(AP4_ByteStream& stream)
{
    return stream.Write(m_RawBytes.GetData(), m_RawBytes.GetDataSize());
}

/*----------------------------------------------------------------------
|   AP4_Dac3Atom::InspectFields
+---------------------------------------------------------------------*/
AP4_Result
AP4_Dac3Atom::InspectFields(AP4_AtomInspector& inspector)
{
  char value[256];
  AP4_FormatString(value, sizeof(value),
    "bsmod=%d, acmod=%d, lfeon=%d", (int)m_bsmod, (int)m_acmod, (int)m_lfeon);
  inspector.AddField("params", value);
  return AP4_SUCCESS;
}

AP4_UI08 AP4_Dac3Atom::GetChannels() const
{
  static const AP4_UI08 CC[] = { 2, 1, 2, 3, 3, 4, 4, 5 };
  return CC[m_acmod] + m_lfeon;
}

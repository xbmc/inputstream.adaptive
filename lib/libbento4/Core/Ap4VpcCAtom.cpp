/*****************************************************************
|
|    AP4 - VpcC Atom 
|
|    Copyright 2018 peak3d
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
#include "Ap4Types.h"
#include "Ap4VpcCAtom.h"
#include "Ap4Utils.h"

/*----------------------------------------------------------------------
|   AP4_UuidAtom Dynamic Cast Anchor
+---------------------------------------------------------------------*/
AP4_DEFINE_DYNAMIC_CAST_ANCHOR(AP4_VpcCAtom)

/*----------------------------------------------------------------------
|   AP4_SgpdAtom::Create
+---------------------------------------------------------------------*/
AP4_VpcCAtom*
AP4_VpcCAtom::Create(AP4_Size size, AP4_ByteStream& stream)
{
  AP4_UI08 version;
  AP4_UI32 flags;
  if (AP4_FAILED(AP4_Atom::ReadFullHeader(stream, version, flags))) return NULL;
  if (version > 1) return NULL;
  return new AP4_VpcCAtom(size, version, flags, stream);
}

/*----------------------------------------------------------------------
|   AP4_VpcCAtom::AP4_VpcCAtom
+---------------------------------------------------------------------*/
AP4_VpcCAtom::AP4_VpcCAtom(AP4_UI64 size, AP4_UI08 version, AP4_UI32 flags, AP4_ByteStream& stream) :
    AP4_Atom(AP4_ATOM_TYPE_VPCC, size, version, flags)
{
    // store the data
    m_Data.SetDataSize((AP4_Size)size - GetHeaderSize());
    stream.Read(m_Data.UseData(), m_Data.GetDataSize());
}

/*----------------------------------------------------------------------
|   AP4_VpcCAtom::WriteFields
+---------------------------------------------------------------------*/
AP4_Result AP4_VpcCAtom::WriteFields(AP4_ByteStream& stream)
{
  return stream.Write(m_Data.GetData(), m_Data.GetDataSize());
}

/*****************************************************************
|
|    AP4 - mvhd Atoms 
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

#ifndef _AP4_TRAF_ATOM_H_
#define _AP4_TRAF_ATOM_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Ap4ContainerAtom.h"

/*----------------------------------------------------------------------
|   AP4_TrafAtom
+---------------------------------------------------------------------*/
class AP4_TrafAtom : public AP4_ContainerAtom
{
public:
	AP4_IMPLEMENT_DYNAMIC_CAST_D(AP4_TrafAtom, AP4_ContainerAtom)

	static AP4_TrafAtom* Create(
		Type             type,
		AP4_UI64         size,
		bool             force_64,
		AP4_ByteStream&  stream,
		AP4_AtomFactory& atom_factory)
	{
		return new AP4_TrafAtom(type, size, force_64, stream, atom_factory);
	};

	AP4_Atom* Clone();

	void SetInternalTrackId(AP4_UI32 id){ m_InternalTrackId = id;};
	AP4_UI32 GetInternalTrackId()const{ return m_InternalTrackId; };
private:
	AP4_TrafAtom(Type type);

	AP4_TrafAtom(Type  type,
		AP4_UI64         size,
		bool             force_64,
		AP4_ByteStream&  stream,
		AP4_AtomFactory& atom_factory)
		:AP4_ContainerAtom(type, size, force_64, stream, atom_factory)
	{};

	AP4_UI32 m_InternalTrackId;
};

#endif // _AP4_TRAF_ATOM_H_

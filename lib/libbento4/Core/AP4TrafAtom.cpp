/*****************************************************************
|
|    AP4 - Traf Atoms
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
#include "Ap4Atom.h"
#include "Ap4TrafAtom.h"

/*----------------------------------------------------------------------
|   dynamic cast support
+---------------------------------------------------------------------*/
AP4_DEFINE_DYNAMIC_CAST_ANCHOR(AP4_TrafAtom)

/*----------------------------------------------------------------------
|   AP4_TrafAtom::AP4_TrafAtom
+---------------------------------------------------------------------*/
AP4_TrafAtom::AP4_TrafAtom(Type type) :
	AP4_ContainerAtom(type)
{
}

/*----------------------------------------------------------------------
|   AP4_ContainerAtom::Clone
+---------------------------------------------------------------------*/
AP4_Atom*
AP4_TrafAtom::Clone()
{
	AP4_TrafAtom* clone(new AP4_TrafAtom(m_Type));

	AP4_List<AP4_Atom>::Item* child_item = m_Children.FirstItem();
	while (child_item) {
		AP4_Atom* child_clone = child_item->GetData()->Clone();
		if (child_clone) clone->AddChild(child_clone);
		child_item = child_item->GetNext();
	}
	return clone;
}
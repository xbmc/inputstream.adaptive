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
/**
* @file 
* @brief UUID Atoms
*/

#ifndef _AP4_VPCC_ATOM_H_
#define _AP4_VPCC_ATOM_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Ap4Atom.h"
#include "Ap4ByteStream.h"

/*----------------------------------------------------------------------
|   AP4_VpcCAtom
+---------------------------------------------------------------------*/
/**
 * Base class for uuid atoms.
 */
class AP4_VpcCAtom : public AP4_Atom {
public:
    AP4_IMPLEMENT_DYNAMIC_CAST_D(AP4_VpcCAtom, AP4_Atom)

    // class methods
    static AP4_VpcCAtom* Create(AP4_Size size, AP4_ByteStream& stream);

    // constructor and destructor
    virtual ~AP4_VpcCAtom() {};

    // accessors
    const AP4_DataBuffer &GetData() { return m_Data; }

    // methods
    virtual AP4_Result InspectFields(AP4_AtomInspector& inspector) { return AP4_ERROR_NOT_SUPPORTED;  };
    virtual AP4_Result WriteFields(AP4_ByteStream& stream);

protected:
    // members
    AP4_VpcCAtom(AP4_UI64 size, AP4_UI08 version, AP4_UI32 flags, AP4_ByteStream& stream);

    AP4_DataBuffer m_Data;
};

#endif // _AP4_UUID_ATOM_H_

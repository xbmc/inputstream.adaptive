/*
 *  Copyright (C) 2003 Frank Thilo (thilo@unix-ag.org)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: RSA-MD
 *  See LICENSES/README.md for more information.
 */

// Converted to C++ class by Frank Thilo for bzflag (http://www.bzflag.org)

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <cstring>
#include <iostream>

namespace UTILS
{
namespace DIGEST
{
// a small class for calculating MD5 hashes of strings or byte arrays
// it is not meant to be fast or secure
//
// usage: 1) feed it blocks of uchars with update()
//      2) Finalize()
//      3) get HexDigest() string
//      or
//      MD5(std::string).HexDigest()
//
// assumes that char is 8 bit and int is 32 bit
class ATTR_DLL_LOCAL MD5
{
public:
  MD5();
  MD5(const std::string& text);
  void Update(const unsigned char* buf, uint32_t length);
  void Update(const char* buf, uint32_t length);
  MD5& Finalize();
  std::string HexDigest() const;
  friend std::ostream& operator<<(std::ostream& out, MD5& md5) { return out << md5.HexDigest(); }

private:
  typedef unsigned char uint1; //  8bit
  typedef unsigned int uint4; // 32bit
  enum
  {
    blocksize = 64
  }; // VC6 won't eat a const static int here

  void Init();
  void Transform(const uint1 block[blocksize]);
  void Decode(uint4 output[], const uint1 input[], uint32_t len);
  void Encode(uint1 output[], const uint4 input[], uint32_t len);

  // low level logic operations
  uint4 F(uint4 x, uint4 y, uint4 z);
  uint4 G(uint4 x, uint4 y, uint4 z);
  uint4 H(uint4 x, uint4 y, uint4 z);
  uint4 I(uint4 x, uint4 y, uint4 z);
  uint4 RotateLeft(uint4 x, int n);
  void FF(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac);
  void GG(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac);
  void HH(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac);
  void II(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac);

  bool m_finalized;
  uint1 m_buffer[blocksize]; // bytes that didn't fit in last 64 byte chunk
  uint4 m_count[2]; // 64bit counter for number of bits (lo, hi)
  uint4 m_state[4]; // digest so far
  uint1 m_digest[16]; // the result
};

std::string GenerateMD5(const std::string& str);
} // namespace DIGEST
} // namespace UTILS

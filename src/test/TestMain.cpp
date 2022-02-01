/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "gtest/gtest.h"

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  std::vector<std::string> args(argv + 1, argv + argc);
#ifdef _WIN32
  _putenv_s("DATADIR", args[0].c_str());
#else
  setenv("DATADIR", args[0].c_str(), 1);
#endif
  return RUN_ALL_TESTS();
}

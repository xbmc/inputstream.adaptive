/*
*      Copyright (C) 2017 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#pragma once

#include "../common/AdaptiveTree.h"
#include <sstream>

namespace adaptive
{

  class HLSTree : public AdaptiveTree
  {
  public:
    HLSTree() :m_containerType(CONTAINERTYPE_TS) {};

    virtual bool open(const char *url) override;
    virtual bool prepareRepresentation(Representation *rep) override;
    virtual ContainerType GetContainerType() override { return m_containerType; };
    virtual bool write_data(void *buffer, size_t buffer_size) override;
  private:
    std::stringstream m_stream;
    std::string m_audioCodec;
    ContainerType m_containerType;
  };

} // namespace

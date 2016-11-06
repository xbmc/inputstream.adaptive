#pragma once

#include "../common/AdaptiveTree.h"

namespace adaptive
{

  class SmoothTree : public AdaptiveTree
  {
  public:
    SmoothTree();
    virtual bool open(const char *url) override;
    virtual bool write_data(void *buffer, size_t buffer_size) override;

    void parse_protection();

    enum
    {
      SSMNODE_SSM = 1 << 0,
      SSMNODE_PROTECTION = 1 << 1,
      SSMNODE_STREAMINDEX = 1 << 2,
      SSMNODE_PROTECTIONHEADER = 1 << 3,
      SSMNODE_PROTECTIONTEXT = 1 << 4
    };

    uint64_t pts_helper_;
  };

}

cmake_minimum_required(VERSION 3.10)
project(cdm_aarch64_loader)

add_library(cdm_aarch64_loader SHARED
  cdm_loader.cpp
)

set_target_properties(cdm_aarch64_loader PROPERTIES POSITION_INDEPENDENT_CODE True)

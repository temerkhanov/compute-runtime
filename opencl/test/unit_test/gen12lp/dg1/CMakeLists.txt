#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

if(TESTS_DG1)
  set(IGDRCL_SRCS_tests_dg1
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/hw_helper_tests_dg1.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/hw_info_tests_dg1.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/test_hw_info_config_dg1.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/test_device_caps_dg1.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/excludes_dg1.cpp
  )
  target_sources(igdrcl_tests PRIVATE ${IGDRCL_SRCS_tests_dg1})
  add_subdirectories()
  neo_copy_test_files_with_revision(copy_test_files_dg1_0 dg1 0)
  add_dependencies(copy_test_files_per_product copy_test_files_dg1_0)
endif()

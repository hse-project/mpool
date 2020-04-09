#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

FUNCTION(SMOKE_TEST)

    SET(one_value_opts
        NAME            # (required) name for collection of object files
        )

    SET(multi_value_opts
        )

    CMAKE_PARSE_ARGUMENTS(
        param
        "" "${one_value_opts}" "${multi_value_opts}"
        ${ARGN})

    IF("${param_NAME}" STREQUAL "")
        MESSAGE(FATAL_ERROR "SMOKE_TEST: Must set NAME")
    ENDIF()

    IF(NOT "${param_UNPARSED_ARGUMENTS}" STREQUAL "")
        MESSAGE(
            FATAL_ERROR
            "SMOKE_TEST: Unknown args: ${param_UNPARSED_ARGUMENTS}")
    ENDIF()

    ADD_TEST(
        NAME     ${param_NAME}
        COMMAND  ${CMAKE_CURRENT_SOURCE_DIR}/smoke-from-ctest ${param_NAME})

    SET_TESTS_PROPERTIES(${param_NAME} PROPERTIES LABELS "smoke")
ENDFUNCTION()

FILE(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/tests/group.smoke smoke_test_list)

FOREACH(test ${smoke_test_list})
  IF("${test}" MATCHES "^group")
    FILE(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${test} group_members)
    FOREACH(group_member ${group_members})
      IF(NOT "${group_member}" MATCHES "^#")
        SMOKE_TEST(NAME ${group_member})
      ENDIF()
    ENDFOREACH()
  ELSE()
    IF(NOT "${test}" MATCHES "^#")
      SMOKE_TEST(NAME ${test})
    ENDIF()
  ENDIF()
ENDFOREACH()

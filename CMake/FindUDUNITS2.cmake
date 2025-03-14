# - Find UDUNITS2
# Find the native UDUNITS2 includes and library
#
#  UDUNITS2_INCLUDE_DIRS - where to find udunits2.h
#  UDUNITS2_LIBRARIES    - libraries to link with
#  UDUNITS2_FOUND        - True if UDUNITS2 was found.

if (UDUNITS2_INCLUDE_DIRS)
  # Already in cache, be silent
  set (UDUNITS2_FIND_QUIETLY TRUE)
endif (UDUNITS2_INCLUDE_DIRS)

find_path (UDUNITS2_INCLUDE_DIRS udunits2.h
  HINTS "${UDUNITS2_ROOT}/include" "$ENV{UDUNITS2_ROOT}/include"
  PATH_SUFFIXES "udunits2"
  DOC "Path to udunits2.h")

# UDUNITS2 headers might be in .../include or .../include/udunits2.
# We try both.
if (${UDUNITS2_INCLUDE_DIRS} MATCHES "udunits2/?$")
  string(REGEX REPLACE "/include/udunits2/?$" "/lib"
    UDUNITS2_LIB_HINT ${UDUNITS2_INCLUDE_DIRS})
else()
  string(REGEX REPLACE "/include/?$" "/lib"
    UDUNITS2_LIB_HINT ${UDUNITS2_INCLUDE_DIRS})
endif()

find_library (UDUNITS2_LIBRARIES
  NAMES udunits2
  HINTS ${UDUNITS2_LIB_HINT})

set(UDUNITS2_TEST_SRC "
#include <udunits2.h>

int main(int argc, char **argv) {
  ut_system *s = ut_read_xml(NULL);
  ut_free_system(s);
  return 0;
}
")

if ((NOT UDUNITS2_LIBRARIES) OR (NOT UDUNITS2_INCLUDE_DIRS))
  message(STATUS "Trying to find UDUNITS-2 using LD_LIBRARY_PATH (we're desperate)...")

  file(TO_CMAKE_PATH "$ENV{LD_LIBRARY_PATH}" LD_LIBRARY_PATH)

  find_library(UDUNITS2_LIBRARIES
    NAMES udunits2
    HINTS ${LD_LIBRARY_PATH})

  if (UDUNITS2_LIBRARIES)
    get_filename_component(UDUNITS2_LIB_DIR ${UDUNITS2_LIBRARIES} PATH)
    string(REGEX REPLACE "/lib/?$" "/include"
      UDUNITS2_H_HINT ${UDUNITS2_LIB_DIR})

    find_path (UDUNITS2_INCLUDE_DIRS udunits2.h
      HINTS ${UDUNITS2_H_HINT}
      PATH_SUFFIXES "udunits2"
      DOC "Path to udunits2.h")
  endif()
endif()

include (CheckCSourceRuns)

set(CMAKE_REQUIRED_INCLUDES ${UDUNITS2_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${UDUNITS2_LIBRARIES})
set(CMAKE_REQUIRED_QUIET TRUE)
check_c_source_runs("${UDUNITS2_TEST_SRC}" UDUNITS2_WORKS_WITHOUT_LEXPAT)

if(${UDUNITS2_WORKS_WITHOUT_LEXPAT})
  message(STATUS "Linking to UDUNITS-2 does not require '-lexpat'")
else()
  find_package(EXPAT REQUIRED)

  set(CMAKE_REQUIRED_INCLUDES ${UDUNITS2_INCLUDE_DIRS} ${EXPAT_INCLUDE_DIRS})
  set(CMAKE_REQUIRED_LIBRARIES ${UDUNITS2_LIBRARIES} ${EXPAT_LIBRARIES})
  set(CMAKE_REQUIRED_QUIET TRUE)
  check_c_source_runs("${UDUNITS2_TEST_SRC}" UDUNITS2_WORKS_WITH_LEXPAT)

  if(NOT ${UDUNITS2_WORKS_WITH_LEXPAT})
    message(FATAL_ERROR "UDUNITS-2 does not seem to work with or without -lexpat")
  endif()

  message(STATUS "Using '-lexpat' to link to UDUNITS-2")
  set (UDUNITS2_LIBRARIES "${UDUNITS2_LIBRARIES};${EXPAT_LIBRARIES}" CACHE STRING "" FORCE)
endif()

# handle the QUIETLY and REQUIRED arguments and set UDUNITS2_FOUND to TRUE if
# all listed variables are TRUE
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (UDUNITS2 DEFAULT_MSG UDUNITS2_LIBRARIES UDUNITS2_INCLUDE_DIRS)

mark_as_advanced (UDUNITS2_LIBRARIES UDUNITS2_INCLUDE_DIRS)

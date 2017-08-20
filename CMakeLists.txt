CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

PROJECT (gittest_selfup)

SET(GITTEST_COMMON_PREFIX ${CMAKE_SOURCE_DIR}/gittest_common CACHE STRING ".")
SET(GITTEST_COMMON_CONFIG_GENERATION_FILEPATH ${CMAKE_SOURCE_DIR}/data/GsConfig.conf CACHE STRING ".")

LIST(APPEND CMAKE_MODULE_PATH
  ${CMAKE_SOURCE_DIR}/cmake/Modules
  ${GITTEST_COMMON_PREFIX}/cmake/Modules
)

INCLUDE(GittestCommon)

GITTEST_COMMON_SET_GENERATION()
GITTEST_COMMON_SET_LIB()

# search for all needed packages

IF (WIN32)
  # search for all needed packages

  FIND_PACKAGE(LibGit2 REQUIRED)
  ## ZLIB is a dependency of LibGit2
  ##   if LibGit2 does not find ZLIB it will use bundled (happens on windows/MSVC)
  IF (NOT GITTEST_COMPILER_MSVC)
    FIND_PACKAGE(ZLIB REQUIRED)
  ENDIF ()
  FIND_PACKAGE(LibEvent REQUIRED)
  FIND_PACKAGE(Shlwapi REQUIRED)

  # set output variables

  SET(GITTEST_DEP_INCLUDE_DIRS
    ${LIBGIT2_INCLUDE_DIR}
    ${LIBEVENT_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIR}
  PARENT_SCOPE)
  SET (GITTEST_DEP_LIBRARIES
    ${LIBGIT2_LIBRARIES}
    ${LIBEVENT_LIBRARIES}
    # ZLIB must be on the link list AFTER LibGit2 to resolve symbols
    ${ZLIB_LIBRARIES}
    ${SHLWAPI_LIBRARIES}
  PARENT_SCOPE)
ELSEIF (UNIX)
  # search for all needed packages

  FIND_PACKAGE(LibGit2 REQUIRED)
  ## ZLIB is a dependency of LibGit2
  FIND_PACKAGE(ZLIB REQUIRED)
  FIND_PACKAGE(LibEvent REQUIRED)

  ## http://stackoverflow.com/questions/1620918/cmake-and-libpthread/29871891#29871891
  ## https://cmake.org/cmake/help/v3.6/module/FindThreads.html
  ##   extra magic for gcc linking with pthreads (-pthread)

  set(THREADS_PREFER_PTHREAD_FLAG ON)
  FIND_PACKAGE(Threads REQUIRED)

  # set output variables

  SET(GITTEST_DEP_INCLUDE_DIRS
    ${LIBGIT2_INCLUDE_DIR}
    ${LIBEVENT_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIR}
  PARENT_SCOPE)
  SET (GITTEST_DEP_LIBRARIES
    ${LIBGIT2_LIBRARIES}
    ${LIBEVENT_LIBRARIES}
    # ZLIB must be on the link list AFTER LibGit2 to resolve symbols
    ${ZLIB_LIBRARIES}
    Threads::Threads
  PARENT_SCOPE)
ENDIF ()

# gittest_ev2_serv

SET(GITTEST_EV2_SERV_HEADERS
  include/gittest/gittest_ev2_test.h
)

SET(GITTEST_EV2_SERV_SOURCES
  src/gittest_ev2_serv.cpp
  src/gittest_ev2_test_s.cpp
  src/gittest_ev2_common.cpp
)

ADD_EXECUTABLE(gittest_ev2_serv ${GITTEST_EV2_SERV_HEADERS} ${GITTEST_EV2_SERV_SOURCES})
TARGET_LINK_LIBRARIES(gittest_ev2_serv gittest_common)
TARGET_INCLUDE_DIRECTORIES(gittest_ev2_serv
  ${CMAKE_SOURCE_DIR}/include
  ${GITTEST_COMMON_PREFIX}/include    # for Gittest Common
  ${GITTEST_DEP_INCLUDE_DIRS}
)

# gittest_ev2_selfupdate, gittest_ev2_selfupdate_clone

SET(GITTEST_EV2_SELFUPDATE_HEADERS
  include/gittest/gittest_ev2_test.h
)

SET(GITTEST_EV2_SELFUPDATE_SOURCES
  src/gittest_ev2_selfupdate.cpp
  src/gittest_ev2_test_c.cpp
  src/gittest_ev2_test_su.cpp
  src/gittest_ev2_common.cpp
)

ADD_EXECUTABLE(gittest_ev2_selfupdate ${GITTEST_EV2_SELFUPDATE_HEADERS} ${GITTEST_EV2_SELFUPDATE_SOURCES})
ADD_EXECUTABLE(gittest_ev2_selfupdate_clone ${GITTEST_EV2_SELFUPDATE_HEADERS} ${GITTEST_EV2_SELFUPDATE_SOURCES})
TARGET_LINK_LIBRARIES(gittest_ev2_selfupdate gittest_common)
TARGET_LINK_LIBRARIES(gittest_ev2_selfupdate_clone gittest_common)
TARGET_INCLUDE_DIRECTORIES(gittest_ev2_selfupdate
  ${CMAKE_SOURCE_DIR}/include
  ${GITTEST_COMMON_PREFIX}/include    # for Gittest Common
  ${GITTEST_DEP_INCLUDE_DIRS}
)
TARGET_INCLUDE_DIRECTORIES(gittest_ev2_selfupdate_clone
  ${CMAKE_SOURCE_DIR}/include
  ${GITTEST_COMMON_PREFIX}/include    # for Gittest Common
  ${GITTEST_DEP_INCLUDE_DIRS}
)
TARGET_COMPILE_DEFINITIONS(gittest_ev2_selfupdate       PRIVATE GS_CONFIG_DEFS_GITTEST_EV2_SELFUPDATE_VERSUB="versub_ev2_selfupdate")
TARGET_COMPILE_DEFINITIONS(gittest_ev2_selfupdate_clone PRIVATE GS_CONFIG_DEFS_GITTEST_EV2_SELFUPDATE_VERSUB="versub_ev2_selfupdate_clone")

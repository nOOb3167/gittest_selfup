# gittest_lib

SET(GITTEST_LIB_HEADERS
  include/gittest/bypart_git.h
  include/gittest/frame.h
  include/gittest/gittest.h
)
SET(GITTEST_LIB_SOURCES
  src/bypart_git.cpp
  src/frame.cpp
  src/gittest.cpp
)

# gittest_ev2_maint

SET(GITTEST_EV2_MAINT_HEADERS
  ${GITTEST_LIB_HEADERS}
)

SET(GITTEST_EV2_MAINT_SOURCES
  src/gittest_ev2_maint.cpp
  ${GITTEST_LIB_SOURCES}
)

# gittest_ev2_serv

SET(GITTEST_EV2_SERV_HEADERS
  include/gittest/gittest_ev2_test.h
  ${GITTEST_LIB_HEADERS}
)

SET(GITTEST_EV2_SERV_SOURCES
  src/gittest_ev2_serv.cpp
  src/gittest_ev2_test_s.cpp
  src/gittest_ev2_common.cpp
  ${GITTEST_LIB_SOURCES}
)

# gittest_ev2_selfupdate, gittest_ev2_selfupdate_clone

SET(GITTEST_EV2_SELFUPDATE_HEADERS
  include/gittest/gittest_ev2_test.h
  include/gittest/gui.h
  include/gittest/net4.h
  ${GITTEST_LIB_HEADERS}
)

SET(GITTEST_EV2_SELFUPDATE_SOURCES
  src/gittest_ev2_selfupdate.cpp
  src/gittest_ev2_test_c.cpp
  src/gittest_ev2_test_su.cpp
  src/gittest_ev2_common.cpp
  src/gui.cpp
  src/gui_win.cpp
  imgpbempty_384_32_.h
  imgpbfull_384_32_.h
  imgpbblip_96_32_.h
  ${GITTEST_LIB_SOURCES}
)

# other platform headers and sources (for dummylib)

SET(GITTEST_PLAT_HEADERS_NIX
  include/gittest/net4.h
  ${GITTEST_COMMON_HEADERS_NIX}
)
SET(GITTEST_PLAT_SOURCES_NIX
  src/gui_nix.cpp
  src/net4_epoll.cpp
  src/net4_serv.cpp
  src/net4_serv_main.cpp
  ${GITTEST_COMMON_SOURCES_NIX}
)

# search for all needed packages

FIND_PACKAGE(LibGit2 REQUIRED)
## ZLIB is a dependency of LibGit2
##   if LibGit2 does not find ZLIB it will use bundled (happens on windows/MSVC)
IF (NOT MSVC)
  FIND_PACKAGE(ZLIB REQUIRED)
ENDIF ()
FIND_PACKAGE(LibEvent REQUIRED)

SET(GITTEST_DEP_INCLUDE_DIRS
  ${LIBGIT2_INCLUDE_DIR}
  ${LIBEVENT_INCLUDE_DIR}
  ${ZLIB_INCLUDE_DIR}
)
SET (GITTEST_DEP_LIBRARIES
  ${LIBGIT2_LIBRARIES}
  ${LIBEVENT_LIBRARIES}
  # ZLIB must be on the link list AFTER LibGit2 to resolve symbols
  ${ZLIB_LIBRARIES}
  # FIXME:
  Msimg32
)

SET(GITTEST_SELFUP_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/include
  ${GITTEST_COMMON_PREFIX}/include    # for Gittest Common
  ${GITTEST_DEP_INCLUDE_DIRS}
)

# define targets

ADD_EXECUTABLE(gittest_ev2_maint ${GITTEST_EV2_MAINT_HEADERS} ${GITTEST_EV2_MAINT_SOURCES})
ADD_EXECUTABLE(gittest_ev2_serv ${GITTEST_EV2_SERV_HEADERS} ${GITTEST_EV2_SERV_SOURCES})
ADD_EXECUTABLE(gittest_ev2_selfupdate ${GITTEST_EV2_SELFUPDATE_HEADERS} ${GITTEST_EV2_SELFUPDATE_SOURCES})
ADD_EXECUTABLE(gittest_ev2_selfupdate_clone ${GITTEST_EV2_SELFUPDATE_HEADERS} ${GITTEST_EV2_SELFUPDATE_SOURCES})
ADD_LIBRARY(dummy_lib STATIC EXCLUDE_FROM_ALL ${GITTEST_PLAT_HEADERS_NIX} ${GITTEST_PLAT_SOURCES_NIX})

SET_PROPERTY(TARGET gittest_ev2_maint PROPERTY SUFFIX ".exe")
SET_PROPERTY(TARGET gittest_ev2_serv PROPERTY SUFFIX ".exe")
SET_PROPERTY(TARGET gittest_ev2_selfupdate PROPERTY SUFFIX ".exe")
SET_PROPERTY(TARGET gittest_ev2_selfupdate_clone PROPERTY SUFFIX ".exe")

TARGET_LINK_LIBRARIES(gittest_ev2_maint gittest_common ${GITTEST_DEP_LIBRARIES})
TARGET_LINK_LIBRARIES(gittest_ev2_serv gittest_common ${GITTEST_DEP_LIBRARIES})
TARGET_LINK_LIBRARIES(gittest_ev2_selfupdate gittest_common ${GITTEST_DEP_LIBRARIES})
TARGET_LINK_LIBRARIES(gittest_ev2_selfupdate_clone gittest_common ${GITTEST_DEP_LIBRARIES})

TARGET_INCLUDE_DIRECTORIES(gittest_ev2_maint PUBLIC ${GITTEST_SELFUP_INCLUDE_DIRS})
TARGET_INCLUDE_DIRECTORIES(gittest_ev2_serv PUBLIC ${GITTEST_SELFUP_INCLUDE_DIRS})
TARGET_INCLUDE_DIRECTORIES(gittest_ev2_selfupdate PUBLIC ${GITTEST_SELFUP_INCLUDE_DIRS})
TARGET_INCLUDE_DIRECTORIES(gittest_ev2_selfupdate_clone PUBLIC ${GITTEST_SELFUP_INCLUDE_DIRS})
TARGET_INCLUDE_DIRECTORIES(dummy_lib PUBLIC ${GITTEST_SELFUP_INCLUDE_DIRS})

TARGET_COMPILE_DEFINITIONS(gittest_ev2_selfupdate       PRIVATE GS_CONFIG_DEFS_GITTEST_EV2_SELFUPDATE_VERSUB="versub_ev2_selfupdate")
TARGET_COMPILE_DEFINITIONS(gittest_ev2_selfupdate_clone PRIVATE GS_CONFIG_DEFS_GITTEST_EV2_SELFUPDATE_VERSUB="versub_ev2_selfupdate_clone")

# cruft

#### some of the headers used are to be generated

GITTEST_COMMON_GENERATE(${CMAKE_SOURCE_DIR}/data imgpbempty_384_32_ .data)
GITTEST_COMMON_GENERATE(${CMAKE_SOURCE_DIR}/data imgpbfull_384_32_ .data)
GITTEST_COMMON_GENERATE(${CMAKE_SOURCE_DIR}/data imgpbblip_96_32_ .data)

#### dummy_lib sources should be marked for no compilation

SET_SOURCE_FILES_PROPERTIES(${GITTEST_PLAT_HEADERS_NIX} ${GITTEST_PLAT_SOURCES_NIX} PROPERTIES HEADER_FILE_ONLY TRUE)

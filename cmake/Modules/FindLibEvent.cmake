FIND_PATH(LIBEVENT_INCLUDE_DIR NAMES event.h
  PATH_SUFFIXES event2)

FIND_LIBRARY(LIBEVENT_LIBRARY NAMES event_static)

SET(LIBEVENT_LIBRARIES_TMP ${LIBEVENT_LIBRARY})

# unfortunately extra libraries may be required
IF (WIN32)
  LIST(APPEND LIBEVENT_LIBRARIES_TMP ws2_32)
ENDIF ()

SET(LIBEVENT_LIBRARIES "${LIBEVENT_LIBRARIES_TMP}" CACHE FILEPATH "LibEvent library (static)" FORCE)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibEvent DEFAULT_MSG LIBEVENT_INCLUDE_DIR LIBEVENT_LIBRARY LIBEVENT_LIBRARIES)

MARK_AS_ADVANCED(LIBEVENT_INCLUDE_DIR LIBEVENT_LIBRARY LIBEVENT_LIBRARIES)

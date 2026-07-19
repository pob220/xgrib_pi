include(FetchContent)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Debian no longer provides a dependable Jasper development package.  Pin a
# minimal static build so catalogue packages do not depend on a distribution
# specific libjasper SONAME.
set(JAS_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
# Flatpak's standard builddir is inside its disposable source copy. Jasper's
# source and binary trees remain distinct under _deps, so this override does
# not permit an actual in-source Jasper build.
set(ALLOW_IN_SOURCE_BUILD ON CACHE BOOL "" FORCE)
set(JAS_ENABLE_CMAKE_PACKAGE_CONFIG OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LIBJPEG OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LIBHEIF OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_OPENGL OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_DOC OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LATEX OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_MULTITHREADING_SUPPORT OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_BMP_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_JPG_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_HEIC_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_MIF_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_PGX_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_PNM_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_RAS_CODEC OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  xgrib_jasper
  URL
    https://github.com/jasper-software/jasper/archive/63e106c80eb72af9fd4fa28772499ab0138b9994.tar.gz
  URL_HASH
    SHA256=b4354e819341dfe43824af3cc59b9e8c0963c000ca5e38ca6cf05b9b9479208c
)
FetchContent_GetProperties(xgrib_jasper)
if(NOT xgrib_jasper_POPULATED)
  FetchContent_Populate(xgrib_jasper)
  add_subdirectory(
    "${xgrib_jasper_SOURCE_DIR}"
    "${xgrib_jasper_BINARY_DIR}"
    EXCLUDE_FROM_ALL
  )
endif()

set(XGRIB_JASPER_TARGET libjasper)
set(XGRIB_JASPER_LICENSE "${xgrib_jasper_SOURCE_DIR}/LICENSE.txt")

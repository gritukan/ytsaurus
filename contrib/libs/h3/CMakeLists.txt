
# This file was generated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(contrib-libs-h3)
target_compile_options(contrib-libs-h3 PRIVATE
  -DH3_PREFIX=
  $<IF:$<CXX_COMPILER_ID:MSVC>,,-Wno-everything>
)
target_include_directories(contrib-libs-h3 PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/include
)
target_link_libraries(contrib-libs-h3 PUBLIC
  contrib-libs-linux-headers
)
target_sources(contrib-libs-h3 PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/algos.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/baseCells.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/bbox.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/coordijk.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/faceijk.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/geoCoord.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/h3Index.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/h3UniEdge.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/linkedGeo.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/localij.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/mathExtensions.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/polygon.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/vec2d.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/vec3d.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/vertex.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/h3/h3lib/lib/vertexGraph.c
)

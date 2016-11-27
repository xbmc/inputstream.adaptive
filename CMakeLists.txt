project(inputstream.adaptive)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)

set(ADP_SOURCES 
	src/main.cpp
	src/common/AdaptiveTree.cpp
	src/parser/DASHTree.cpp
	src/parser/SmoothTree.cpp
	src/common/AdaptiveStream.cpp
	src/helpers.cpp
  src/oscompat.cpp
  src/msl.cpp
)

list(APPEND DEPLIBS ${p8-platform_LIBRARIES})

include_directories(${INCLUDES}
                    ${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    lib/libbento4/Core
)
                    
if(WIN32)
 	add_definitions(-DXML_STATIC)
	include_directories(lib/expat/include)
	link_directories(${PROJECT_SOURCE_DIR}/lib/expat/lib)
	list(APPEND DEPLIBS ws2_32 expat)
else()
	include(FindEXPAT)
	find_package(EXPAT REQUIRED)
	include_directories(${EXPAT_INCLUDE_DIRS})
	list(APPEND DEPLIBS ${EXPAT_LIBRARIES})
	add_definitions(-D__STDC_FORMAT_MACROS)
endif()

if(CORE_SYSTEM_NAME STREQUAL android)
  set(DECRYPTERPATH "special://xbmcbinaddons")
else()
  set(DECRYPTERPATH "special://home/cdm")
endif()

add_subdirectory(lib/libbento4)
list(APPEND DEPLIBS bento4)
					
build_addon(inputstream.adaptive ADP DEPLIBS)

include(CPack)

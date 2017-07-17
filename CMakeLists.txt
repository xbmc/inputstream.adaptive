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
	src/parser/TTML.cpp
	src/common/AdaptiveStream.cpp
	src/helpers.cpp
	src/oscompat.cpp
)

set(ADP_HEADERS
	src/helpers.h
	src/main.h
	src/oscompat.h
	src/SSD_dll.h
	src/common/AdaptiveStream.h
	src/common/AdaptiveTree.h
	src/parser/DASHTree.h
	src/parser/SmoothTree.h
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
  add_subdirectory(wvdecrypter)
  set(DECRYPTERPATH "special://xbmcbinaddons")
  install(FILES $<TARGET_FILE:ssd_wv> DESTINATION lib/${APP_NAME_LC}/addons/inputstream.adaptive)
else()
  set(DECRYPTERPATH "special://home/cdm")
  add_subdirectory(lib/libbento4)
endif()

list(APPEND DEPLIBS bento4)
					
build_addon(inputstream.adaptive ADP DEPLIBS)

include(CPack)

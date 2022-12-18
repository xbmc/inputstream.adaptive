set(BINARY ${CMAKE_PROJECT_NAME}_test)

find_package( Threads )

add_definitions(-DINPUTSTREAM_TEST_BUILD)

add_executable(${BINARY}
    TestMain.cpp
    TestDASHTree.cpp
    TestHLSTree.cpp
    TestSmoothTree.cpp
    TestHelper.cpp
    ../parser/DASHTree.cpp
    ../parser/HLSTree.cpp
    ../parser/SmoothTree.cpp
    ../parser/PRProtectionParser.cpp
    ../common/AdaptiveStream.cpp
    ../common/AdaptiveTree.cpp
    ../common/Chooser.cpp
    ../common/ChooserAskQuality.cpp
    ../common/ChooserDefault.cpp
    ../common/ChooserFixedRes.cpp
    ../common/ChooserManualOSD.cpp
    ../common/ChooserTest.cpp
    ../common/ReprSelector.cpp
    ../oscompat.cpp
    ../utils/Base64Utils.cpp
    ../utils/PropertiesUtils.cpp
    ../utils/SettingsUtils.cpp
    ../utils/StringUtils.cpp
    ../utils/UrlUtils.cpp
    ../utils/Utils.cpp
    )

target_link_libraries(${BINARY} PRIVATE ${EXPAT_LIBRARIES} ${GTEST_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS})

set(TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/src/test/manifests")
add_test(NAME manifest_tests COMMAND ${BINARY} "${TEST_DATA_DIR}")

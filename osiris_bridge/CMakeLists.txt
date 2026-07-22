if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  add_executable(OsirisHost32 host.cpp protocol.h pipe.h)
  target_include_directories(OsirisHost32 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/lib"
    "${CMAKE_SOURCE_DIR}/scripts")
  target_compile_features(OsirisHost32 PRIVATE cxx_std_17)
  target_compile_definitions(OsirisHost32 PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  set_target_properties(OsirisHost32 PROPERTIES OUTPUT_NAME "OsirisHost32")
  install(TARGETS OsirisHost32 RUNTIME DESTINATION ${D3_GAMEDIR})
endif()

if(WIN32)
  add_library(OsirisBridgeTestLevel SHARED test_level.cpp)
  target_include_directories(OsirisBridgeTestLevel PRIVATE "${CMAKE_SOURCE_DIR}/scripts")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(OsirisBridgeTestLevel PRIVATE CHECKSUM=2273889835UL)
    target_sources(OsirisBridgeTestLevel PRIVATE "${CMAKE_SOURCE_DIR}/scripts/MS-LVL-X64.DEF")
  else()
    target_compile_definitions(OsirisBridgeTestLevel PRIVATE CHECKSUM=2273873307UL)
    target_sources(OsirisBridgeTestLevel PRIVATE "${CMAKE_SOURCE_DIR}/scripts/MS-LVL.DEF")
  endif()
  set_target_properties(OsirisBridgeTestLevel PROPERTIES PREFIX "")
endif()

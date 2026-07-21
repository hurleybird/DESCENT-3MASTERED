
IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
  ADD_DEFINITIONS (-DCHECKSUM=2273889835UL)
ELSE()
  ADD_DEFINITIONS (-DCHECKSUM=2273873307UL)
ENDIF()
SET (HEADERS )
SET (CPPS )
IF(UNIX)
SET (CPPS
	linux_lib.cpp)
ENDIF()

SET (SCRIPTS
	aigame2
	AIGame3
	aigame4
	AIGame
	barney
	BatteriesIncluded
	BossCamera
	CanyonsCTF
	CellTestLevel
	ChrisTest
	clutter
	GameGauge
	generic
	Geodomes
	HalfPipe
	InfernalBolt
	Inversion
	LEVEL0
	level10
	level11
	Level12
	level13
	level14
	LEVEL15
	Level16
	level17
	level1
	level2
	level3
	level4
	level5
	Level6
	level7
	level8
	Level9
	LevelS1
	levelS2
	Merc02
	Merc1
	Merc3
	Merc4
	merc5
	Merc6
	Merc7
	myPowerHouse
	Mysterious_Isle
	orbital
	Paranoia
	PiccuStation
	Polaris
	Quadsomniac
	RudeAwakening
	SewerRat
	testscript
	TrainingMission
	Y2K)

SET (SCRIPTTARGETS )

SET (GAME_SCRIPTS
  aigame2
  AIGame3
  aigame4
  AIGame
  clutter
  generic
  testscript)

IF(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  SET(WIN_SCRIPT_HOG_DIR "${CMAKE_BINARY_DIR}/scripts/win64hog")
  SET(WIN_SCRIPT_DEBUG_DIR "${CMAKE_BINARY_DIR}/scripts/debug")
  FILE(MAKE_DIRECTORY "${WIN_SCRIPT_HOG_DIR}" "${WIN_SCRIPT_DEBUG_DIR}")
  SET(WIN64_MODULE_STRING_NAMES
    Anarchy.str Anarchyi.str
    coop.str coopi.str
    CTF.str CTFi.str
    DMFC.str DMFCi.str
    entropy.str entropyi.str
    hoard.str hoardi.str
    Hyper.str hyperi.str
    lanclient.str lanclienti.str
    monster.str monsteri.str
    mtclient.str mtclienti.str
    tanarchy.str tanarchyi.str)
  SET(WIN64_MODULE_STRING_SOURCES)
  FOREACH(STRING_NAME ${WIN64_MODULE_STRING_NAMES})
    LIST(APPEND WIN64_MODULE_STRING_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/data/linuxfullhog/${STRING_NAME}")
  ENDFOREACH()
ENDIF()

FOREACH( SCRIPT ${SCRIPTS})
 
  ADD_LIBRARY(${SCRIPT} SHARED ${CPPS} "${SCRIPT}.cpp")
  set_target_properties(${SCRIPT} PROPERTIES PREFIX "")
  SET(SCRIPTTARGETS ${SCRIPTTARGETS} ${SCRIPT})

  IF(WIN32)
    LIST(FIND GAME_SCRIPTS ${SCRIPT} GAME_SCRIPT_INDEX)
    IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
      IF(GAME_SCRIPT_INDEX EQUAL -1)
        TARGET_SOURCES(${SCRIPT} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/MS-LVL-X64.DEF")
      ELSE()
        TARGET_SOURCES(${SCRIPT} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/MS-GAME-X64.DEF")
      ENDIF()
      SET_TARGET_PROPERTIES(${SCRIPT} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WIN_SCRIPT_HOG_DIR}"
        PDB_OUTPUT_DIRECTORY "${WIN_SCRIPT_DEBUG_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY "${WIN_SCRIPT_DEBUG_DIR}"
        COMPILE_PDB_NAME "${SCRIPT}")
      TARGET_LINK_OPTIONS(${SCRIPT} PRIVATE /INCREMENTAL:NO)
    ELSE()
      IF(GAME_SCRIPT_INDEX EQUAL -1)
        TARGET_SOURCES(${SCRIPT} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/MS-LVL.DEF")
      ELSE()
        TARGET_SOURCES(${SCRIPT} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/MS-GAME.DEF")
      ENDIF()
    ENDIF()
  ENDIF()

    IF(UNIX)
      GET_TARGET_PROPERTY(SCRIPT_LIB ${SCRIPT} LOCATION)
      ADD_CUSTOM_COMMAND(
	TARGET ${SCRIPT}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${SCRIPT_LIB} "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/"
	COMMENT "Copying file ${SCRIPT_LIB} to hogfile directory..."
	)
      ADD_CUSTOM_COMMAND(
	TARGET ${SCRIPT}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${SCRIPT_LIB} "${CMAKE_SOURCE_DIR}/scripts/data/linuxdemohog/"
	)

    ENDIF()

ENDFOREACH()

IF(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  ADD_CUSTOM_COMMAND(
    OUTPUT "${CMAKE_BINARY_DIR}/piccuengine-win.hog"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${WIN64_MODULE_STRING_SOURCES} "${WIN_SCRIPT_HOG_DIR}"
    COMMAND $<TARGET_FILE:hogdir> "${CMAKE_BINARY_DIR}/piccuengine-win.hog" "${WIN_SCRIPT_HOG_DIR}"
    DEPENDS hogdir ${SCRIPTTARGETS} ${WIN64_MODULE_STRING_SOURCES}
    COMMENT "Building the Win64 first-party script archive")
  ADD_CUSTOM_TARGET(piccuengine_win_hog ALL
    DEPENDS "${CMAKE_BINARY_DIR}/piccuengine-win.hog")
  INSTALL(FILES "${CMAKE_BINARY_DIR}/piccuengine-win.hog" DESTINATION ${D3_GAMEDIR})
ENDIF()

IF(UNIX)
  SET(HOGBUILDCMD "${CMAKE_SOURCE_DIR}/scripts/hogutils/hogUtils-i686")
  SET(HOGARCH "linux")
  IF(APPLE)
    SET(HOGBUILDCMD "${CMAKE_SOURCE_DIR}/scripts/hogutils/hogUtils-macosx")
    SET(HOGARCH "osx")
  ENDIF()


  ADD_CUSTOM_TARGET("FullLinuxHog"
    ALL
    COMMAND ${HOGBUILDCMD} "--makehog" "d3linuxfullhog.txt" 
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/d3-${HOGARCH}.hog"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/new.hog" "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/d3-${HOGARCH}.hog"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/d3-${HOGARCH}.hog" "${D3_GAMEDIR}"
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/new.hog"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/data/linuxfullhog/"
    DEPENDS ${SCRIPTTARGETS}
    COMMENT "Building platform specific hog."
    )
  ADD_CUSTOM_TARGET("DemoLinuxHog"
    ALL
    COMMAND ${HOGBUILDCMD} "--makehog" "d3linuxdemohog.txt" 
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_SOURCE_DIR}/scripts/data/linuxdemohog/d3-${HOGARCH}.hog"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/scripts/data/linuxdemohog/new.hog" "${CMAKE_SOURCE_DIR}/scripts/data/linuxdemohog/d3-${HOGARCH}.hog"
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_SOURCE_DIR}/scripts/data/linuxdemohog/new.hog"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/data/linuxdemohog/"
    DEPENDS ${SCRIPTTARGETS}
    COMMENT "Building platform specific hog."
    )
ENDIF()

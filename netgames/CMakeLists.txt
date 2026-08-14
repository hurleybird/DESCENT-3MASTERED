include_directories("includes")
ADD_SUBDIRECTORY(dmfc)
ADD_SUBDIRECTORY(anarchy)
ADD_SUBDIRECTORY(coop)
ADD_SUBDIRECTORY(ctf)
ADD_SUBDIRECTORY(entropy)
ADD_SUBDIRECTORY(hoard)
ADD_SUBDIRECTORY(hyperanarchy)
ADD_SUBDIRECTORY(monsterball)
ADD_SUBDIRECTORY(roboanarchy)
ADD_SUBDIRECTORY(tanarchy)

set (NETGAME_TARGETS
	anarchy coop ctf entropy hoard hyperanarchy monsterball roboanarchy tanarchy
	PARENT_SCOPE)

# Keep the developer build tree runnable without requiring an install step.
# The engine scans this directory beside the executable for native game modes.
foreach(NETGAME_TARGET
	anarchy coop ctf entropy hoard hyperanarchy monsterball roboanarchy tanarchy)
	set_target_properties(${NETGAME_TARGET} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/netgames3mastered"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/netgames3mastered")
endforeach()

#Transfer variables to root scope
set (DMFC_SOURCES
	${DMFC_SOURCES}
	PARENT_SCOPE)

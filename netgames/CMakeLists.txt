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

#Transfer variables to root scope
set (DMFC_SOURCES
	${DMFC_SOURCES}
	PARENT_SCOPE)

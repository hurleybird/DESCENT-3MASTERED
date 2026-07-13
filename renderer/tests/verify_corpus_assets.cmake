set(corpus_root "${CMAKE_CURRENT_LIST_DIR}/../..")

set(corpus_assets
	"scripts/data/linuxdemohog/level1.d3l|A67CADAF906D31A2373F1DC19FA5370B68967D10462DDB6427864C00056FB5E9"
	"scripts/data/linuxdemohog/Polaris.d3l|E406E85B4F111DEF13040EDB9953F6477EF8F81BBD487AD2303535D7D051D6CC"
	"scripts/data/linuxdemohog/Taurus.d3l|94EF815238A9071B8AC9B780F135F00910223964892E5A1847E1EAF87452B955"
	"scripts/data/linuxdemohog/thecore.d3l|08639458E8282DFDF2593ACC5E4E757E7C75D396E01F7CA96E817F51CC44BF5E")

foreach(entry IN LISTS corpus_assets)
	string(REPLACE "|" ";" fields "${entry}")
	list(GET fields 0 relative_path)
	list(GET fields 1 expected_hash)
	set(path "${corpus_root}/${relative_path}")
	if (NOT EXISTS "${path}")
		message(FATAL_ERROR "Missing deterministic renderer corpus asset: ${relative_path}")
	endif()
	file(SHA256 "${path}" actual_hash)
	string(TOUPPER "${actual_hash}" actual_hash)
	if (NOT actual_hash STREQUAL expected_hash)
		message(FATAL_ERROR
			"Renderer corpus asset hash mismatch: ${relative_path}\n"
			"expected: ${expected_hash}\nactual:   ${actual_hash}")
	endif()
endforeach()

message(STATUS "Verified 4 deterministic renderer corpus assets")

cmake_minimum_required(VERSION 3.21)

project(
	SRG
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "Simple. Retro. Game."
	HOMEPAGE_URL "www.no.com"
)

#
# Generate fileslist from iso.xml at build time
# This creates a string table with enums for indexing all the CD files
#
# Could also be read for tools as well
#
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/gen-fileslist.py")
	add_custom_command(
		OUTPUT "${CMAKE_SOURCE_DIR}/source/fileslist.h"
			     "${CMAKE_SOURCE_DIR}/source/fileslist.c"
					 COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gen-fileslist.py"
			     "${CMAKE_SOURCE_DIR}/iso.xml"
		DEPENDS "${CMAKE_SOURCE_DIR}/iso.xml"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Generating fileslist from iso.xml"
	)
	add_custom_target(generate-fileslist DEPENDS source/fileslist.h source/fileslist.c)
endif()

psn00bsdk_add_executable(game GPREL
					source/main.c
					source/cdfs.c
					source/filesystem.c
					source/input_pad.c
					source/memory_arena.c
					source/memory_card.c
					source/input_action.c
					source/render.c
					source/font.c
					source/serializer.c
					source/spu.c
					source/audio.c
					source/mml.c
					source/ps1_mem_limit.c
					source/fileslist.c)

include(cmake/audio.cmake)

psn00bsdk_add_cd_image(
	iso      # Target name
	game     # Output file name (= game.bin + game.cue)
	iso.xml  # Path to config file
	DEPENDS game system.cnf res/ ${AUDIO_ASSETS}
)

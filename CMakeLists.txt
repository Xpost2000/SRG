cmake_minimum_required(VERSION 3.21)

project(
	SRG
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "Simple. Retro. Game."
	HOMEPAGE_URL "www.no.com"
)

psn00bsdk_add_executable(game GPREL
				  source/main.c
				  source/cdfs.c
				  source/input_pad.c
				  source/memory_arena.c)

psn00bsdk_add_cd_image(
	iso      # Target name
	game     # Output file name (= game.bin + game.cue)
	iso.xml  # Path to config file
	DEPENDS game system.cnf
)

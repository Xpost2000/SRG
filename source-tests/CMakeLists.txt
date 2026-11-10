cmake_minimum_required(VERSION 3.21)

project(
	SRG-TESTS
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "Simple. Retro. Game. Tests."
	HOMEPAGE_URL "www.no.com"
)

#
# The tests are written using the modules for the
# PC version of the game (whenever that's a thing lol),
# but that technically the header files are the same for
# both platforms.
#
include_directories("../source/")

add_executable(world_tests world_tests.c)

enable_testing()

add_test(NAME world_tests COMMAND $<TARGET_FILE:world_tests>)

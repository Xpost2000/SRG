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
				  source/memory_arena.c
				  source/memory_card.c
				  source/input_action.c
				  source/render.c
				  source/font.c
				  source/serializer.c
				  source/spu.c
				  source/audio.c
          source/ps1_mem_limit.c)

# ----------------------------------------------------------------------------
#  Audio assets
#
#  res/audio/*.wav|mp3 are converted at build time by psxavenc (vendored in
#  toolchain-win64/bin) into SPU ADPCM .VAG files under run-tree/audio/, which
#  iso.xml then puts on the disc. Only the source files are committed. A rule
#  re-runs only when its source changes.
#
#    srg_audio_sfx(NAME path rate [extra psxavenc flags])
#      mono one-shot sample, whole file lives in sound RAM. Loop points are
#      taken from the .wav "smpl" chunk if present; pass -L to force a loop.
#
#    srg_audio_music(NAME path rate channels)
#      interleaved stream for the CD feeder: 4096-byte chunks (what spu.c
#      expects), loop flag at the end of every chunk. channels is 1 or 2.
#
#  Size rule of thumb: bytes per second = rate / 28 * 16 * channels, so
#  22050 Hz stereo is about 25 KB/s, 1.5 MB per minute.
# ----------------------------------------------------------------------------
find_program(PSXAVENC psxavenc HINTS ${PROJECT_SOURCE_DIR}/toolchain-win64/bin REQUIRED)

set(AUDIO_OUT_DIR ${CMAKE_BINARY_DIR}/audio)
file(MAKE_DIRECTORY ${AUDIO_OUT_DIR})
set(AUDIO_ASSETS "")

function(srg_audio_sfx name source rate)
	set(_out ${AUDIO_OUT_DIR}/${name}.VAG)
	add_custom_command(
		OUTPUT  ${_out}
		COMMAND ${PSXAVENC} -q -t vag -f ${rate} ${ARGN} ${PROJECT_SOURCE_DIR}/${source} ${_out}
		DEPENDS ${PROJECT_SOURCE_DIR}/${source}
		COMMENT "Encoding sfx ${name} (${rate} Hz mono)"
		VERBATIM
	)
	set(AUDIO_ASSETS ${AUDIO_ASSETS} ${_out} PARENT_SCOPE)
endfunction()

function(srg_audio_music name source rate channels)
	set(_out ${AUDIO_OUT_DIR}/${name}.VAG)
	add_custom_command(
		OUTPUT  ${_out}
		COMMAND ${PSXAVENC} -q -t vagi -f ${rate} -c ${channels} -L -i 4096 ${PROJECT_SOURCE_DIR}/${source} ${_out}
		DEPENDS ${PROJECT_SOURCE_DIR}/${source}
		COMMENT "Encoding music ${name} (${rate} Hz, ${channels} ch)"
		VERBATIM
	)
	set(AUDIO_ASSETS ${AUDIO_ASSETS} ${_out} PARENT_SCOPE)
endfunction()

srg_audio_sfx  (BLIP res/audio/blip.wav 22050)
srg_audio_music(LOOP res/audio/loop.mp3 22050 2)

psn00bsdk_add_cd_image(
	iso      # Target name
	game     # Output file name (= game.bin + game.cue)
	iso.xml  # Path to config file
	DEPENDS game system.cnf res/ ${AUDIO_ASSETS}
)

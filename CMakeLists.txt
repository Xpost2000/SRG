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
#  The instrument bank. res/audio/*.wav are converted at build time by
#  psxavenc (vendored in toolchain-win64/bin) into SPU ADPCM .VAG files under
#  run-tree/audio/, which iso.xml then puts on the disc and audio.c uploads
#  to sound RAM at boot. Only the source files are committed. A rule re-runs
#  only when its source changes.
#
#    srg_audio_sample(NAME path rate [extra psxavenc flags])
#      mono sample that sounds middle C (C4) when played at `rate`. Pass the
#      .wav's own rate so nothing is resampled. -L loops the whole sample,
#      for sustained single-cycle instruments.
#
#  Songs (res/audio/*.mml) are plain text and go on the disc as they are.
#
#  Size rule of thumb: bytes = samples / 28 * 16, so a second at 22050 Hz
#  is about 12.6 KB of sound RAM.
# ----------------------------------------------------------------------------
find_program(PSXAVENC psxavenc HINTS ${PROJECT_SOURCE_DIR}/toolchain-win64/bin REQUIRED)

set(AUDIO_OUT_DIR ${CMAKE_BINARY_DIR}/audio)
file(MAKE_DIRECTORY ${AUDIO_OUT_DIR})
set(AUDIO_ASSETS "")

function(srg_audio_sample name source rate)
	set(_out ${AUDIO_OUT_DIR}/${name}.VAG)
	add_custom_command(
		OUTPUT  ${_out}
		COMMAND ${PSXAVENC} -q -t vag -f ${rate} ${ARGN} ${PROJECT_SOURCE_DIR}/${source} ${_out}
		DEPENDS ${PROJECT_SOURCE_DIR}/${source}
		COMMENT "Encoding sample ${name} (${rate} Hz mono)"
		VERBATIM
	)
	set(AUDIO_ASSETS ${AUDIO_ASSETS} ${_out} PARENT_SCOPE)
endfunction()

# One-shots at 11025 Hz: half the sound RAM of 22050, and a PS1 through a TV
# hides the missing top octave. 21977 Hz = 84-sample single cycle at C4, see
# engine-docs/audio.md.
srg_audio_sample(PIANO res/audio/piano.wav 11025)
srg_audio_sample(BASS  res/audio/bass.wav  21977 -L)
srg_audio_sample(LEAD  res/audio/lead.wav  21977 -L)
srg_audio_sample(KICK  res/audio/kick.wav  11025)
srg_audio_sample(SNARE res/audio/snare.wav 11025)
srg_audio_sample(HAT   res/audio/hat.wav   11025)
srg_audio_sample(BLIP  res/audio/blip.wav  11025)   # resampled from 22050

file(GLOB AUDIO_SONGS ${PROJECT_SOURCE_DIR}/res/audio/*.mml)
list(APPEND AUDIO_ASSETS ${AUDIO_SONGS})

psn00bsdk_add_cd_image(
	iso      # Target name
	game     # Output file name (= game.bin + game.cue)
	iso.xml  # Path to config file
	DEPENDS game system.cnf res/ ${AUDIO_ASSETS}
)

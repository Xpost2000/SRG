@rem These use GCC to compile but you can also just use the visual studio C compiler
@rem they're not really that big of C programs, so do whatever.

@rem the environment variables set from setvars breaks the C compiler
@rem configuration for me though which is stupid :/

gcc fontbuild.c -o fontbuild
gcc worldcompile.c ../source/world_builder.c ../source/memory_arena.c ../source/serializer.c ../source/filesystem.c -o worldcompile -I ../source

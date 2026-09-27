@rem These use GCC to compile but you can also just use the visual studio C compiler
@rem they're not really that big of C programs, so do whatever.

@rem the environment variables set from setvars breaks the C compiler
@rem configuration for me though which is stupid :/

gcc fontbuild.c -o fontbuild

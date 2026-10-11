#ifndef MEMORY_ARENA_H
#define MEMORY_ARENA_H

#include "common.h"

//
// Double-ended linear allocator
//
typedef struct Memory_Arena Memory_Arena;

struct Memory_Arena {
  //
  // This is for debugging reasons.
  //
  char*		name;
  uint8_t*	memory;
  int		capacity;
  int		cursor;
  int		cursor_top;
};

//
// NOTE(jerry):
// no temporary allocator support, up to callers to do that behavior themselves
// with bump cursors.
//
// Also the memory arena itself uses a little memory to store itself.
//

uint32_t      align4(int amount);
Memory_Arena* memory_arena_create(char* name, uint8_t* memory, int capacity);
uint8_t*      memory_arena_push_unaligned(Memory_Arena* arena, int amount);
uint8_t*      memory_arena_push_top_unaligned(Memory_Arena* arena, int amount);
uint8_t*      memory_arena_push_aligned(Memory_Arena* arena, int amount);
uint8_t*      memory_arena_push_top_aligned(Memory_Arena* arena, int amount);
char*         memory_arena_clone_string(Memory_Arena* arena, char* string, size_t length);
size_t        memory_arena_get_used(Memory_Arena* arena);
size_t        memory_arena_get_cursor(Memory_Arena* arena);
size_t        memory_arena_get_cursor_top(Memory_Arena* arena);

//
// Both of these return the last cursor position just in-case it's needed.
//
size_t        memory_arena_reset_cursor_to(Memory_Arena* arena, size_t where);
size_t        memory_arena_reset_top_cursor_to(Memory_Arena* arena, size_t where);
void          memory_arena_reset_bottom(Memory_Arena* arena);
void          memory_arena_reset_top(Memory_Arena* arena);
void          memory_arena_reset(Memory_Arena* arena);

//
// Bump allocator over an address range the CPU cannot (or should not) touch:
// SPU sound RAM, VRAM, a region of the CD. Memory_Arena can't do this job
// because it hands back CPU pointers and memsets what it gives out; on the PS1
// sound RAM and VRAM are only reachable by DMA, so there is nothing to
// dereference. This only does the bookkeeping and returns addresses.
//
//   base                         cursor                     end
//    +--------+--------+-----------+--------------------------+
//    | alloc  | alloc  | alloc     |   free                   |
//    +--------+--------+-----------+--------------------------+
//
// No free, only reset: load a scene's worth, throw it all away on exit.
// memory_bump_push() returns 0 when full. No range we manage starts at
// address 0 (sound RAM 0 is hardware capture buffers, VRAM 0 is the
// framebuffer), so 0 is never a valid allocation.
//
typedef struct Memory_Bump Memory_Bump;

struct Memory_Bump {
  uint32_t base;
  uint32_t end;
  uint32_t cursor;
};

void     memory_bump_init(Memory_Bump* bump, uint32_t base, uint32_t end);
uint32_t memory_bump_push(Memory_Bump* bump, uint32_t size, uint32_t align);
void     memory_bump_reset(Memory_Bump* bump);

#endif

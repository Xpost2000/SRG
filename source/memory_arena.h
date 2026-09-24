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

#endif

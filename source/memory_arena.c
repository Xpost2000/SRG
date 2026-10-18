#include "memory_arena.h"

Memory_Arena* memory_arena_create(char* name, uint8_t* memory, int capacity)
{
  Memory_Arena* result = (Memory_Arena*) memory;
  result->name = name;
  result->capacity = capacity - sizeof(*result);
  result->cursor = result->cursor_top = 0;
  result->memory = memory += sizeof(*result);
  return result;
}

uint8_t* memory_arena_push_unaligned(Memory_Arena* arena, int amount)
{
  _debugprintf("[AREA] (%s) requesting allocation: %d bytes", arena->name, amount);
  uint8_t* result = arena->memory + arena->cursor;
  arena->cursor += amount;
  assert(arena->cursor + arena->cursor_top < arena->capacity && "[MEM-ARENA]: OOM!");
  memset(result, 0, amount);
  return result;
}

uint8_t* memory_arena_push_top_unaligned(Memory_Arena* arena, int amount)
{
  _debugprintf("[AREA-TOP] (%s) requesting allocation: %d bytes", arena->name, amount);
  arena->cursor_top += amount;
  uint8_t* result = (uint8_t*)(arena->memory+arena->capacity) - arena->cursor_top;
  assert(arena->cursor + arena->cursor_top < arena->capacity && "[MEM-ARENA]: OOM!");
  memset(result, 0, amount);
  return result;
}

void memory_arena_reset_bottom(Memory_Arena* arena)
{
  arena->cursor = 0;
}

void memory_arena_reset_top(Memory_Arena* arena)
{
  arena->cursor_top = 0;
}

size_t memory_arena_get_cursor(Memory_Arena* arena)
{
  return arena->cursor;
}

size_t memory_arena_get_cursor_top(Memory_Arena* arena)
{
  return arena->cursor_top;
}

size_t memory_arena_reset_cursor_to(Memory_Arena* arena, size_t where)
{
  size_t old = arena->cursor;
  arena->cursor = where;
  return old;
}

size_t memory_arena_reset_top_cursor_to(Memory_Arena* arena, size_t where)
{
  size_t old = arena->cursor_top;
  arena->cursor_top = where;
  return old;
}

void memory_arena_reset(Memory_Arena* arena)
{
  arena->cursor = arena->cursor_top = 0;
}

size_t memory_arena_get_used(Memory_Arena* arena)
{
  return (arena->cursor + arena->cursor_top);
}

uint8_t* memory_arena_push_aligned(Memory_Arena* arena, int amount)
{
  amount = align4(amount);
  return memory_arena_push_unaligned(arena, amount);
}

uint8_t* memory_arena_push_top_aligned(Memory_Arena* arena, int amount)
{
  amount = align4(amount);
  return memory_arena_push_top_unaligned(arena, amount);
}

uint32_t align4(int amount)
{
  uint32_t allocsize = ((amount+3)/4);
  return allocsize*4;
}

char* memory_arena_clone_string(Memory_Arena* arena, char* string, size_t length)
{
  int allocsize = (align4(length));
  char* buffer = (char*) memory_arena_push_unaligned(arena, allocsize);
  memcpy(buffer, string, length);
  buffer[length] = 0;
  return buffer;
}

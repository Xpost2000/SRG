#include "filesystem.h"

//
// maybe can use windows.h
// but for now this is just based on the
// C standard library implementation
//
#include <stdio.h>
#include <stdlib.h>

typedef struct Opened_File_State Opened_File_State;

struct Opened_File_State {
  uint32_t	file_size;

  //
  // Only used for seek, but due to the way this works,
  // it can only possibly be misaligned exactly once (the first read after seek)
  //
  uint16_t      offset_within_sector;
  FILE* f;
};

static Opened_File_State g_open_files[MAX_OPENED_FILES];

static int clib_map_seek_location(uint8_t oseek)
{
  switch (oseek) {
  case FILE_SYSTEM_SEEK_WHERE_START: {
    return SEEK_SET;
  } break;
  case FILE_SYSTEM_SEEK_WHERE_END: {
    return SEEK_END;
  } break;
  }
  return -1;
}

static size_t clib_get_file_size(FILE* f)
{
  size_t result;
  fseek(f, 0, SEEK_END);
  result = ftell(f);
  fseek(f, 0, SEEK_SET);
  return result;
}

File_Handle filesystem_open(const char* filepath)
{
  int i;

  for (i = 0; i < MAX_OPENED_FILES; ++i) {
    if (!g_open_files[i].f) {
      Opened_File_State* state = &g_open_files[i];
      //
      // NOTE: the C standard library allows doing this
      // any '+' mode is technically readable and writable.
      //
      FILE*              f     = fopen(filepath, "wb+");

      if (!f) {
        _debugprintf("[FILESYSTEM] Failed to open: %s", filepath);
        return 0;
      }

      state->f	       = f;
      state->file_size = clib_get_file_size(f);
      return (File_Handle)(i + 1);
    }
  }

  _debugprintf("[FILESYSTEM] Too many open files");
  assert(0);
  return 0;
}

void filesystem_seek(File_Handle file, uint8_t where, int32_t offset)
{
  int index                     = (int)(file - 1);
  Opened_File_State* state      = &g_open_files[index];
  assert(state->f && "[FILESYSTEM] File not open");
  fseek(state->f, offset, clib_map_seek_location(where));
}

size_t filesystem_get_file_size(File_Handle file)
{
  int index = (int)(file - 1);
  Opened_File_State* state = &g_open_files[index];
  assert(state->f && "[FILESYSTEM] File not open");
  return state->file_size;
}

int filesystem_read(File_Handle file, unsigned char* data, size_t size)
{
  size_t		remaining;
  size_t		bytes_read;
  unsigned char*	buffer;
  Opened_File_State*	state;
  int			index = (int)(file - 1);

  if (file == 0) {
    _debugprintf("[FILESYSTEM] Invalid file handle");
    assert(0);
    return -1;
  }

  state = &g_open_files[index];
  assert(state->f && "[FILESYSTEM] File not open");

  return fread(data, 1, size, state->f);
}

int filesystem_write(File_Handle file, unsigned char* data, size_t size)
{
  int			index = (int)(file - 1);
  Opened_File_State*	state = &g_open_files[index];
  if (file == 0) {
    _debugprintf("[FILESYSTEM] Invalid file handle");
    assert(0);
    return -1;
  }

  assert(state->f && "[FILESYSTEM] File not open");
  return fwrite(data, 1, size, state->f);
}

void filesystem_close(File_Handle file)
{
  int			index = (int)(file - 1);
  Opened_File_State*	state = &g_open_files[index];

  if (file == 0) {
    return; 
  }

  fclose(state->f);
  state->f = NULL;
}

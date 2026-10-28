#include "cdfs.h"
#include "filesystem.h"

typedef struct Opened_File_State Opened_File_State;

struct Opened_File_State {
  CD_File	file;
  unsigned char buffer[CD_SECTOR_SIZE*1];
  uint32_t	file_size_aligned;
  uint32_t	file_size_raw;

  //
  // Only used for seek, but due to the way this works,
  // it can only possibly be misaligned exactly once (the first read after seek)
  //
  uint16_t      offset_within_sector;
};

static Opened_File_State g_open_files[MAX_OPENED_FILES];

File_Handle filesystem_open(const char* filepath)
{
  int i;

  for (i = 0; i < MAX_OPENED_FILES; ++i) {
    if (!g_open_files[i].file.valid) {
      Opened_File_State*	state = &g_open_files[i];
      CD_File			file  = cd_file_open(filepath);

      if (!file.valid) {
        _debugprintf("[FILESYSTEM] Failed to open: %s", filepath);
        return 0;
      }

      state->file	       = file;
      state->file_size_raw     = cd_file_get_size(&file);
      state->file_size_aligned = cd_file_get_memory_required(&file);
      memset(state->buffer, 0, sizeof(state->buffer));
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
  uint32_t sector_offset        = offset / CD_SECTOR_SIZE;
  uint16_t offset_within_sector = offset % CD_SECTOR_SIZE;
  assert(state->file.valid && "[FILESYSTEM] File not open");

  state->offset_within_sector = offset_within_sector;

  //
  // the values line up for seeking so I'm just passing it through directly...
  //
  cd_file_seek(&state->file, where, sector_offset);
}

size_t filesystem_get_file_size(File_Handle file)
{
  int index = (int)(file - 1);
  Opened_File_State* state = &g_open_files[index];
  assert(state->file.valid && "[FILESYSTEM] File not open");
  return state->file_size_raw;
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
  assert(state->file.valid && "[FILESYSTEM] File not open");

  remaining	   = size;
  bytes_read	   = 0;
  buffer	   = state->buffer;

  while ((remaining > 0)) {
    size_t	to_read	       = (remaining < sizeof(state->buffer)) ? remaining : sizeof(state->buffer);
    size_t	sectors_needed = (to_read + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
    _debugprintf("sectors to read: %d, to read: %d, remaining %d", sectors_needed, to_read, remaining);
    size_t	cd_read;
    size_t	to_copy;

    cd_read = cd_file_read_sync_uncached(&state->file, buffer, sectors_needed * CD_SECTOR_SIZE);
    to_copy = (to_read < cd_read) ? to_read : cd_read;

    if (cd_read < 0) {
      _debugprintf("[FILESYSTEM] CD read error");
      return -1;
    }

    _debugprintf("cd_read: %d", cd_read);

    memcpy(data + bytes_read, buffer + state->offset_within_sector, to_copy);

    //
    // a "once-only" to skip-over sector bytes based
    // off of the seek.
    //
    state->offset_within_sector = 0;

    bytes_read	     += to_copy;
    remaining	     -= to_copy;
  }

  return (int)bytes_read;
}

void filesystem_close(File_Handle file)
{
  int			index = (int)(file - 1);
  Opened_File_State*	state = &g_open_files[index];

  if (file == 0) {
    return; 
  }

  state->file.valid = 0;
}

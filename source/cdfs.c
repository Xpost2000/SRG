#include "cdfs.h"

void cd_start(void)
{
  CdInit();
}

CD_File cd_file_open(const char* fpath)
{
  CD_File result = {};
  (CdReadSync(0, 0));
  result.valid = !!CdSearchFile(&result.file_index, fpath);
  _debugprintf("[CDFS] load : %s", fpath);
  return result;
}

size_t cd_file_get_size(CD_File* file)
{
  assert(file->valid && "cd_file_get_memory_required given an invalid file.");
  return (file->file_index.size);
}

size_t cd_file_get_memory_required(CD_File* file)
{
  assert(file->valid && "cd_file_get_memory_required given an invalid file.");
  return cd_file_get_aligned_size(file->file_index.size);
}

size_t cd_file_get_aligned_size(size_t size)
{
  return (size + 2047) & 0xfffff800;
}

static void _cd_file_issue_read(CD_File* file, unsigned char* buffer, size_t size)
{
  assert(file->valid && "_cd_file_issue_read given an invalid file.");
  size_t sector_count = size / CD_SECTOR_SIZE;
  CdControl(CdlSetloc, &file->file_index.pos, 0);
  CdRead(sector_count, buffer, CdlModeSpeed);
}

size_t cd_file_read_sync_uncached(CD_File* file, unsigned char* buffer, size_t size)
{
  assert(file->valid && "cd_file_read_sync_uncached given an invalid file.");
  CdReadSync(0, 0);
  _cd_file_issue_read(file, buffer, size);

  if (CdReadSync(0, 0) < 0) {
    return -1;
  }

  file->cursor += size;
  return size;
}

int cd_is_ready_to_receive_read(int blocking)
{
  return CdReadSync(!blocking, 0);
}

CD_File_Buffer cd_file_read_uncached(Memory_Arena* arena, CD_File* file)
{
  CD_File_Buffer result;
  size_t entry_size = cd_file_get_memory_required(file);
  result.length = entry_size;
  result.memory = memory_arena_push_unaligned(arena, entry_size);
  cd_file_read_sync_uncached(file, result.memory, result.length);
  return result;
}

#include "cdfs.h"

static void _cd_file_issue_read(CD_File* file, unsigned char* buffer, size_t size);

void cd_start(void)
{
  CdInit();
}

size_t cd_file_get_sector_total(CD_File* file)
{
  return cd_file_get_memory_required(file) / CD_SECTOR_SIZE;
}

static CdlLOC cdloc_add_sector_offset(CdlLOC loc, int n)
{
  CdlLOC result;
  int    lba_sector;

  lba_sector = CdPosToInt(&loc);
  lba_sector += n;
  CdIntToPos(lba_sector, &result);

  return result;
}

CD_File cd_file_open(const char* fpath)
{
  CD_File result     = {};
  int     sector_count;

  (CdReadSync(0, 0));

  result.valid = !!CdSearchFile(&result.file_index, fpath);
  result.start = result.file_index.pos;
  sector_count = cd_file_get_sector_total(&result);
  result.end   = cdloc_add_sector_offset(result.start, sector_count);

  _debugprintf("[CDFS] load : %s, it is %d sectors long", fpath, sector_count);
  _debugprintf("[CDFS] file starts at: <%d, %d, %d, %d> (minute, second, sector, track)",
	       result.start.minute, result.start.second, result.start.sector, result.start.track);
  _debugprintf("[CDFS] file ends at: <%d, %d, %d, %d> (minute, second, sector, track)",
	       result.end.minute, result.end.second, result.end.sector, result.end.track);
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
  assert(file->valid && "[CDFS] _cd_file_issue_read given an invalid file.");
  assert((size % 2048) == 0 && "[CDFS] cd_file_read_sync_uncached only accepts CD_SECTOR_SIZE reads for simplicity.");
  size_t sector_count = size / CD_SECTOR_SIZE;
  CdControl(CdlSetloc, (unsigned char*) &file->file_index.pos, 0);
  CdReadRetry(sector_count, (uint32_t*) buffer, CdlModeSpeed, 4);
}

void cd_file_seek(CD_File* file, uint8_t where, int32_t sector_offset)
{
  CdlLOC new_file_loc;

  switch (where) {
  case CD_FILE_SEEK_WHERE_BEGIN: {
    new_file_loc = file->start;
  } break;
  case CD_FILE_SEEK_WHERE_END: {
    new_file_loc = file->end;
  } break;
  default: {
    assert(0 && "[CDFS] File seek requires where to be a valid CD_File_Seek_Where");
  } break;
  }

  file->file_index.pos = cdloc_add_sector_offset(new_file_loc, sector_offset);
}

size_t cd_file_read_sync_uncached(CD_File* file, unsigned char* buffer, size_t size)
{
  assert(file->valid && "[CDFS] cd_file_read_sync_uncached given an invalid file.");
  assert((size % 2048) == 0 && "[CDFS] cd_file_read_sync_uncached only accepts CD_SECTOR_SIZE reads for simplicity.");

  CdReadSync(0, 0);
  _cd_file_issue_read(file, buffer, size);

  if (CdReadSync(0, 0) == -1) {
    return -1;
  }

  file->file_index.pos = cdloc_add_sector_offset(file->file_index.pos, size / CD_SECTOR_SIZE);

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

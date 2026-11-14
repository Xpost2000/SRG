#include "filesystem.h"

File_Handle filesystem_open(const char* filepath)
{
  UNUSED(filepath);
  assert(0 && "[FILESYSTEM] stub implementation.");
  return 0;
}

void filesystem_seek(File_Handle file, uint8_t where, int32_t offset)
{
  UNUSED(file);
  UNUSED(where);
  UNUSED(offset);
  assert(0 && "[FILESYSTEM] stub implementation.");
}

size_t filesystem_get_file_size(File_Handle file)
{
  UNUSED(file);
  assert(0 && "[FILESYSTEM] stub implementation.");
  return 0;
}

int filesystem_read(File_Handle file, unsigned char* data, size_t size)
{
  UNUSED(file);
  UNUSED(data);
  UNUSED(size);
  assert(0 && "[FILESYSTEM] stub implementation.");
  return 0;
}

int filesystem_write(File_Handle file, unsigned char* data, size_t size)
{
  UNUSED(file);
  UNUSED(data);
  UNUSED(size);
  assert(0 && "[FILESYSTEM] stub implementation.");
  return -1;
}

void filesystem_close(File_Handle file)
{
  UNUSED(file);
  assert(0 && "[FILESYSTEM] stub implementation.");
}

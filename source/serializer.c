#include "serializer.h"

#define _define_serializer_read_imp(T, fnname)                                          \
  T serializer_read##fnname(Serializer* serializer)                                     \
  {                                                                                   \
    T result;                                                                         \
    switch (serializer->type) {                                                        \
    case SERIALIZER_TYPE_MEMORY_BUFFER: {                                              \
      _serializer_check_memory_range(serializer, sizeof(T));                           \
      result = *(T*)(serializer->data + serializer->cursor);                           \
      serializer->cursor += sizeof(T);                                                 \
    } break;                                                                          \
    case SERIALIZER_TYPE_FILE_HANDLE: {                                                \
      int fs_read_result;                                                              \
      fs_read_result = filesystem_read(serializer->file_handle, (uint8_t*)&result, sizeof(T)); \
      assert(fs_read_result == sizeof(T) && "[SERIALIZER] file serializer outran the data source."); \
    } break;                                                                          \
    default: {                                                                        \
      assert(0 && "unknown serializer type");                                          \
      return 0;                                                                       \
    } break;                                                                          \
    }                                                                                 \
    return result;                                                                    \
  }

#define _define_serializer_read_inplace_imp(T, fnname)                       \
  void serializer_read##fnname##_inplace(Serializer* serializer, T* x)       \
  {                                                                        \
    *x = serializer_read##fnname(serializer);                               \
  }

#define _define_serializer_write_imp(T, fnname)                              \
  void serializer_write##fnname(Serializer* serializer, T* x)                \
  {                                                                        \
    serializer_write_bytes(serializer, (uint8_t*)x, sizeof(T));              \
  }

#define _define_serializer_serialize_imp(T, fnname)                          \
  void serializer_serialize##fnname(Serializer* serializer, T* x)            \
  {                                                                        \
    switch (serializer->mode) {                                             \
    case SERIALIZER_MODE_READ: {                                            \
      serializer_read##fnname##_inplace(serializer, x);                     \
    } break;                                                               \
    case SERIALIZER_MODE_WRITE: {                                           \
      serializer_write##fnname(serializer, x);                              \
    } break;                                                               \
    default: {                                                             \
      assert(0 && "[SERIALIZER] Unknown serializer mode.");                 \
    } break;                                                               \
    }                                                                      \
  }

static void _serializer_check_memory_range(Serializer* serializer, uint32_t bytes)
{
  _debugprintf("[SERIALIZER] cursor(%d) vs data_length(%d)", serializer->cursor, serializer->data_length);
  assert((serializer->cursor <= serializer->data_length) && "[SERIALIZER] memory serializer outran the buffer.");
  assert((bytes <= serializer->data_length - serializer->cursor) && "[SERIALIZER] memory serializer outran the buffer.");
}

Serializer serializer_from_memory(uint8_t* data, uint32_t data_length)
{
  Serializer result;

  result.type        = SERIALIZER_TYPE_MEMORY_BUFFER;
  result.mode        = SERIALIZER_MODE_READ;
  result.cursor      = 0;
  result.data_length = data_length;
  result.data	     = data;

  return result;
}

Serializer serializer_from_file_handle(File_Handle file_handle)
{
  Serializer result;

  result.type        = SERIALIZER_TYPE_FILE_HANDLE;
  result.mode        = SERIALIZER_MODE_READ;
  result.file_handle = file_handle;

  return result;
}

Serializer serializer_from_file(const char* filepath)
{
  File_Handle file_handle = filesystem_open(filepath);
  return serializer_from_file_handle(file_handle);
}

void serializer_set_mode(Serializer* serializer, uint8_t mode)
{
  assert((mode == SERIALIZER_MODE_READ || mode == SERIALIZER_MODE_WRITE) && "[SERIALIZER] Unknown serializer mode.");
  serializer->mode = mode;
}

_define_serializer_read_imp(uint32_t, u32);
_define_serializer_read_imp(uint16_t, u16);
_define_serializer_read_imp(uint8_t, u8);
_define_serializer_read_imp(int32_t, i32);
_define_serializer_read_imp(int16_t, i16);
_define_serializer_read_imp(int8_t, i8);

_define_serializer_read_inplace_imp(uint32_t, u32);
_define_serializer_read_inplace_imp(uint16_t, u16);
_define_serializer_read_inplace_imp(uint8_t, u8);
_define_serializer_read_inplace_imp(int32_t, i32);
_define_serializer_read_inplace_imp(int16_t, i16);
_define_serializer_read_inplace_imp(int8_t, i8);

_define_serializer_write_imp(uint32_t, u32);
_define_serializer_write_imp(uint16_t, u16);
_define_serializer_write_imp(uint8_t, u8);
_define_serializer_write_imp(int32_t, i32);
_define_serializer_write_imp(int16_t, i16);
_define_serializer_write_imp(int8_t, i8);

_define_serializer_serialize_imp(uint32_t, u32);
_define_serializer_serialize_imp(uint16_t, u16);
_define_serializer_serialize_imp(uint8_t, u8);
_define_serializer_serialize_imp(int32_t, i32);
_define_serializer_serialize_imp(int16_t, i16);
_define_serializer_serialize_imp(int8_t, i8);

uint8_t* serializer_read_bytes(Serializer* serializer, uint32_t bytes)
{
  uint8_t* ptr;

  assert(serializer->type == SERIALIZER_TYPE_MEMORY_BUFFER && "[SERIALIZER] due to the internal implementation, you have to use a memory buffer serializer for this to work.");
  _serializer_check_memory_range(serializer, bytes);

  ptr = serializer->data + serializer->cursor;
  serializer->cursor += bytes;

  return ptr;
}

void serializer_write_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes)
{
  switch (serializer->type) {
  case SERIALIZER_TYPE_MEMORY_BUFFER: {
    _serializer_check_memory_range(serializer, bytes);
    memcpy(serializer->data + serializer->cursor, dest, bytes);
    serializer->cursor += bytes;
  } break;
  case SERIALIZER_TYPE_FILE_HANDLE: {
    int written_bytes;
    written_bytes = filesystem_write(serializer->file_handle, dest, bytes);
    assert(written_bytes == bytes && "[SERIALIZER] file serializer did not write the same amount of bytes as requested.");
  } break;
  default: {
    assert(0 && "unknown serializer type");
  } break;
  }
}

void serializer_read_into_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes)
{
  switch (serializer->type) {
  case SERIALIZER_TYPE_MEMORY_BUFFER: {
    uint8_t* start_of_ptr = serializer_read_bytes(serializer, bytes);
    memcpy(dest, start_of_ptr, bytes);
  } break;
  case SERIALIZER_TYPE_FILE_HANDLE: {
    int read_bytes;
    read_bytes = filesystem_read(serializer->file_handle, dest, bytes);
    _debugprintf("[SERIALIZER] read_bytes(%d) vs. byte(%d)", read_bytes, bytes);
    assert(read_bytes == bytes && "[SERIALIZER] file serializer did not read the same amount of bytes as requested.");
  } break;
  default: {
    assert(0 && "unknown serializer type");
  } break;
  }
}

void serializer_serialize_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes)
{
  switch (serializer->mode) {
  case SERIALIZER_MODE_READ: {
    serializer_read_into_bytes(serializer, dest, bytes);
  } break;
  case SERIALIZER_MODE_WRITE: {
    serializer_write_bytes(serializer, dest, bytes);
  } break;
  default: {
    assert(0 && "[SERIALIZER] Unknown serializer mode.");
  } break;
  }
}

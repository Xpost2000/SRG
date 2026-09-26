#include "serializer.h"

#define _define_serializer_read_imp(T, fnname)			\
  T serializer_read##fnname(Serializer* serializer)		\
  {								\
    T result;							\
								\
    result = *(T*)(serializer->data + serializer->read_cursor); \
    serializer->read_cursor += sizeof(T);			\
    _serializer_check_read_cursor(serializer);			\
								\
    return result;						\
  }

static void _serializer_check_read_cursor(Serializer* serializer)
{
  assert((serializer->read_cursor < serializer->data_length) && "[SERIALIZER] memory serializer outran the data source.");
}

Serializer serializer_from_memory(uint8_t* data, uint32_t data_length)
{
  Serializer result;

  result.read_cursor = 0;
  result.data_length = data_length;
  result.data	     = data;

  return result;
}

_define_serializer_read_imp(uint32_t, u32);
_define_serializer_read_imp(uint16_t, u16);
_define_serializer_read_imp(uint8_t, u8);
_define_serializer_read_imp(int32_t, i32);
_define_serializer_read_imp(int16_t, i16);
_define_serializer_read_imp(int8_t, i8);

uint8_t* serializer_read_bytes(Serializer* serializer, uint32_t bytes)
{
  uint8_t* ptr;

  ptr = serializer->data + serializer->read_cursor;
  serializer->read_cursor += bytes;
  _serializer_check_read_cursor(serializer);

  return ptr;
}

void serializer_read_into_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes)
{
  uint8_t* start_of_ptr = serializer_read_bytes(serializer, bytes);

  memcpy(dest, start_of_ptr, bytes);
}

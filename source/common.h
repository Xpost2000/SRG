#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <sys/fcntl.h>

inline static char* __shorten_path_length(char* original, int depth) {
    if (depth < 0) depth = 1;
    // assumed from __FILE__...
    int length = strlen(original);

    char* ptr = original + (length-1);
    while (depth && *ptr) {
        ptr--;
        if (*ptr == '\\' || *ptr == '/') depth--;
    }

    return ptr;
}

#define UNUSED(x) ((void)(x))

#ifdef NDEBUG
#define RELEASE
#endif

#ifndef RELEASE
#define _debugprintfhead()   printf("[%s:%d:%s()]: " , __shorten_path_length((char*)__FILE__, 2), __LINE__, __func__)
#define _debugprintf1(...)  do { _debugprintfhead(); printf(__VA_ARGS__); } while(0)
#define _debugprintf(...)   do { _debugprintf1(__VA_ARGS__); printf("\n"); } while (0) 
#else
// #define _debugprintf(fmt, ...)  
// #define _debugprintfhead(fmt, ...)
// #define _debugprintf1(fmt, ...)
#define _debugprintf(...)  
#define _debugprintfhead()
#define _debugprintf1(...)
#endif

#define array_count(x) (sizeof(x)/sizeof(*x))
#define max(x, y) ((x > y) ? x : y)
#define min(x, y) ((x > y) ? y : x)
#define clamp(x, y, z) min(max(x, z), y)

#define memory_zero(c, s) memset(c, 0, s)
#define memory_zero_fixed(c) memory_zero(c, sizeof(c))

typedef struct Rectangle32  Rectangle32;
typedef struct Vector2      Vector2;

struct Rectangle32 {
  int x;
  int y;
  int w;
  int h;
};

struct Vector2 {
  int x;
  int y;
};

static void upcase_string(char* c, int s)
{
  for (int i = 0; i < s; ++i) {
    if (c[i] >= 'a' && c[i] <= 'z') {
      c[i] -= 32;
    }
  }
}

static int rectangle_point_overlap(Rectangle32 rectangle, Vector2 p)
{
  if ((p.x <= (rectangle.x + rectangle.w) && p.x >= (rectangle.x)) &&
      (p.y <= (rectangle.y + rectangle.h) && p.y >= (rectangle.y))) {
    return 1;
  }
  return 0;
}

static int rectangle_point_intersect(Rectangle32 rectangle, Vector2 p)
{
  if ((p.x < (rectangle.x + rectangle.w) && p.x > (rectangle.x)) &&
      (p.y < (rectangle.y + rectangle.h) && p.y > (rectangle.y))) {
    return 1;
  }
  return 0;
}

static int rectangle_rectangle_intersect(Rectangle32 a, Rectangle32 b)
{
  if (
    (a.x < (b.x + b.w) && a.x+a.w > (b.x)) &&
    (a.y < (b.y + b.h) && a.y+a.h > (b.y))
  ) {
    return 1;
  }
  return 0;
}

static int rectangle_rectangle_overlap(Rectangle32 a, Rectangle32 b)
{
  if (
    (a.x <= (b.x + b.w) && a.x+a.w >= (b.x)) &&
    (a.y <= (b.y + b.h) && a.y+a.h >= (b.y))
  ) {
    return 1;
  }
  return 0;
}

static inline uint32_t hash_bytes_fnv1a(uint8_t* bytes, size_t length)
{
    uint32_t offset_basis = 2166136261;
    uint32_t prime        = 16777619;

    uint32_t hash = offset_basis;

    for (unsigned index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= prime;
    }

    return hash;
}

static int is_whitespace(char c)
{
  switch (c) {
      case ' ': case '\t': case '\n': case '\r':
        return 1;
  }

  return 0;
}

static int is_digit(char c)
{
  if (c >= '0' && c <= '9')
    return 1;

  return 0;
}

static int is_valid_integer_string(char* s, int length)
{
  if (length == 0)
    return 0; // how?

  int first_non_hyphen = -1;
  int starts_with_digit = 0;
  int first_non_hyphen_is_digit = 0;

  if (is_digit(s[0])) {
    starts_with_digit = 1;
  }

  for (int i = 0; i < length; ++i) {
    if (starts_with_digit || first_non_hyphen_is_digit) {
      if (!is_digit(s[i])) {
        return -1; // -1 == not a number, but also traditionally wrong to most language parsers. (bad for identifier)
      }
    } else {
      if (first_non_hyphen == -1) {
        if (s[i] != '-') {
          first_non_hyphen = i;
          if (is_digit(s[first_non_hyphen])) {
            first_non_hyphen_is_digit = 1;
          }
        }
      }
    }
  }

  if (starts_with_digit || first_non_hyphen_is_digit) {
    return 1;
  }

  return 0;
}

static int cstring2_compare_case_insensitive(char* a, int len_a, char* b, int len_b)
{
  if (len_a != len_b)
    return 0;

  for (int i = 0; i < len_a; ++i) {
    char ca = a[i];
    char cb = b[i];

    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;

    if (ca != cb)
      return 0;
  }

  return 1;
}

static int cstring_compare_case_insensitive(char* a, char* b)
{
  int len_a = strlen(a);
  int len_b = strlen(b);

  if (len_a != len_b)
    return 0;

  for (int i = 0; i < len_a; ++i) {
    char ca = a[i];
    char cb = b[i];

    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;

    if (ca != cb)
      return 0;
  }

  return 1;
}

static uint64_t djb2_checksum(const void const *data, int size)
{
    // djb2 from: https://www.cse.yorku.ca/~oz/hash.html
    const char *bytes = (const char *)data;
    uint64_t hash = 5381;

    for (int i = 0; i < size; i++)
    {
        hash = ((hash << 5) + hash) + bytes[i];
    }

    return hash;
}

// Print out uint64_t as two uint32_t
static void debug_print_uint64_t(uint64_t x)
{
  //
  // this is an inline type so as to avoid conflicting
  // with <windows.h>'s QWORD
  //
  typedef union {
    uint32_t words[2];
    uint64_t qword;
  } __QWORD;
  __QWORD qword = (__QWORD){.qword = x};
  printf("[0] = %x, [1] = %x\n", qword.words[0], qword.words[1]);
}

#endif

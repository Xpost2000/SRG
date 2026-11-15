//
// This compiles the V1 text format for SRG worlds
// into the binary format to be consumed at runtime.
//
// The text format isn't being used for any reason so
// it doesn't "really exist outside of that"...
//

#include "common.h"
#include "world.h"
#include "world_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int     tile_count=0;
int     world_width;
int     world_height;
uint8_t tile_data[Kilobyte(16)];

//
// NOTE(jerry):
// maybe we should consider having a proper
// tokenizer thing, but the game engine (if we can help it)
// should avoid having to directly do any text (I know mml.c/mml.h handle
// text, and arguably that should also have a compiler but eh)
//
static char* nexttoken(char* s, int* len)
{
  static char token[4096];
  int         i;
  int         w;
  char        ch;

  i = 0;
  memset(token, 0, sizeof(token));
  while (1) {
    ch = s[i];

    if (ch == 0) {
      return NULL; 
    }

    if (ch == '\"') {
      i++;

      //
      // read a string
      //
      {
        while (1) {
          ch = s[i];
          if (ch == '\"') {
            i++;
            break;
          } else {
            token[w] = ch;
            i++;
          }
        }
      }
    } else if  (ch == ' ' || ch == '\t') {
      i++;
    } else {
      token[w] = ch;
      i++;
    } 
  }

  *len = strlen(token);
  return token;
}

static char* readline(FILE* f)
{
  static char linebuffer[4096];
  int         ch;
  int         i;

  i = 0;
  memset(linebuffer, 0, sizeof(linebuffer));
  while (1) {
    ch = fgetc(f);
    if (ch == EOF) {
      return NULL;
    }

    if (ch == '\n') {
      break;
    }

    linebuffer[i] = (char) ch;
  }

  return linebuffer;
}

void read_tiles(FILE* f)
{
  char* line;
  int   max_x=0;
  int   lines;
  int   i;
  int   s;

  lines = 0;
  while ((line = readline(f))) {
    if (strcmp(line, "tiles_end") == 0) {
      return;
    }

    s = strlen(line);
    max_x = max(max_x, s);
    for (i = 0; i < s; ++i) {
      tile_data[tile_count++] = line[i]; 
    }

    lines++;
  }

  world_width = max_x;
  world_height = lines;
}

void read_entities(FILE* f)
{
  
  char* line;
  char* tok;
  int   length;

  while ((line = readline(f))) {
    if (line[0] == '/' && line[1] == '/') {
      continue; 
    }

    if (strcmp(line, "entities_end") == 0) {
      return;
    }

    tok = nexttoken(line, &length);
    line += length;

    if (strcmp(tok, "player_spawn") == 0) {
      
    } else if (strcmp(tok, "end_stage_region") == 0) {
      
    } else if (strcmp(tok, "object_prop") == 0) {
      
    }
  }
}

static void process_map_file(FILE* f, FILE* fout)
{
  char* line;

  while ((line = readline(f))) {
    if (line[0] == '/' && line[1] == '/') {
      continue; 
    }

    if (strcmp(line, "tiles_begin") == 0) {
      read_tiles(f);
    } else if (strcmp(line, "entities_begin") == 0) {
      read_entities(f);
    }
  }
}

//
// static string scoped
// to the length
//
static char* remove_suffix(const char* s)
{
  static char* _buff[Kilobyte(4)];
  int i;
  int slen;
  int dot;

  slen = strlen(s);
  for (i = (slen-1); i >= 0; --i) {
    if (s[i] == '.') {
      dot = i;
      break;
    }
  }

  //
  // no bounds checking here
  //
  for (i = 0; i < dot; ++i) {
    _buff[i] = s[i];
  }
  _buff[i] = 0;

  return _buff;
}

int main(int argc, char** argv)
{
  int i;
  FILE* f;
  FILE* fout;
  char* filebufname[4096];

  if (argc < 2) {
    printf("Usage:\n\t%s <file0.map> ... <file1.map>\n", argv[0]);
    printf("worldcompile will output files file0.slvl ... file1.slvl which are the\n");
    printf("binary format for SRG levels.\n");
    return 0;
  }

  for (i = 1; i < argc; ++i) {
    snprintf(filebufname, sizeof(filebufname), "%s.slvl", remove_suffix(argv[i]));

    f = fopen(argv[i], "rb+");
    fout = fopen(filebufname, "wb+");

    process_map_file(f, fout);

    fclose(fout);
    fclose(f);
  }

  return 0; 
}

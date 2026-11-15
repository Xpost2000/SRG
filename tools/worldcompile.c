//
// This compiles the V1 text format for SRG worlds
// into the binary format to be consumed at runtime.
//
// The text format isn't being used for any reason so
// it doesn't "really exist outside of that"...
//

#include "common.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void process_map_file(FILE* f, FILE* fout)
{
  
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

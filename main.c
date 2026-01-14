#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Standard GNU Readline headers
#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"

//FILE READING HELPER
static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  
  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

//FILE EXECUTION
static void repl() {
  char line[1024];
  for (;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    // NEW LOGIC:
    ObjFunction* function = compile(line);
    if (function != NULL) {
        printf("Compiled to a function object!\n");
        // interpret(function); // Will be add this later
    }
  }
}

static void runFile(const char* path) {
  char* source = readFile(path);
  
  // NEW LOGIC:
  ObjFunction* function = compile(source);
  if (function == NULL) exit(65); // Compile error

  printf("Compiled to a function object!\n");
  // interpret(function); // We will add this later!

  free(source);
}

//MAIN ENTRY POINT
int main(int argc, const char* argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    // Updated usage message - 09/01/2026
    fprintf(stderr, "Usage: asharp [script.as]\n");
    exit(64);
  }

  freeVM();
  return 0;
}
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

// FILE READING HELPER
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

// FILE EXECUTION (Merged and Fixed)
static void runFile(const char* path) {
  // Enforce the .as extension
  const char* ext = strrchr(path, '.');

  // If there is no dot, or the extension isn't ".as", throw an error
  if (!ext || strcmp(ext, ".as") != 0) {
      fprintf(stderr, "Error: Source file must end with .as\n");
      exit(64);
  }

  char* source = readFile(path);
  
  // NEW LOGIC: Compile returns a function object
  ObjFunction* function = compile(source);
  if (function == NULL) {
    free(source);
    exit(65); // Compile error
  }

  InterpretResult result = interpret(function);
  
  free(source); 
  // Note: We don't free the function here yet because the VM 
  // or GC will handle object cleanup later.

  if (result == INTERPRET_COMPILE_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

// REPL
static void repl() {
  printf("============Welcome to A-Sharp v0.1============\n");
  printf("Press Ctrl+D to quit.\n");

  char* line;
  // readline returns a malloc'd string (or NULL on EOF)
  while ((line = readline("> ")) != NULL) {
    
    // Add the line to history so Up-Arrow works
    if (line[0] != '\0') {
      add_history(line);
    }

    ObjFunction* function = compile(line);
    if (function != NULL) {
      interpret(function);
    }

    // readline allocates memory for every line, so we must free it
    free(line); 
  }
}

// MAIN ENTRY POINT
int main(int argc, const char* argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: asharp [script.as]\n");
    exit(64);
  }

  freeVM();
  return 0;
}
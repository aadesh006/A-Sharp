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

// Only include the LLVM backend header when building with LLVM support.
// USE_LLVM is defined by the Makefile's 'make llvm' target.
#ifdef USE_LLVM
#include "llvm_backend.h"
#endif

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

// REPL — unchanged, always uses the bytecode VM
static void repl() {
  printf("============Welcome to A-Sharp v0.1============\n");
  printf("Press Ctrl+D to quit.\n");

  char* line;
  while ((line = readline("> ")) != NULL) {
    if (line[0] != '\0') {
      add_history(line);
    }

    ObjFunction* function = compile(line);
    if (function != NULL) {
        interpret(line);
    }

    free(line); 
  }
}

// Run a file using the existing bytecode VM (default mode)
static void runFile(const char* path) {
  char* source = readFile(path);
  InterpretResult result = interpret(source); 
  free(source); 

  if (result == INTERPRET_COMPILE_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

// Run a file using the LLVM JIT backend (--llvm mode)
#ifdef USE_LLVM
static void runFileWithLLVM(const char* path) {
  printf("[LLVM Backend] JIT-compiling %s...\n", path);
  char* source = readFile(path);
  LLVMBackendResult result = runWithLLVM(source);
  free(source);

  if (result == LLVM_COMPILE_ERROR) {
    fprintf(stderr, "[LLVM Backend] Compilation failed.\n");
    exit(65);
  }
  if (result == LLVM_RUNTIME_ERROR) {
    fprintf(stderr, "[LLVM Backend] Runtime error.\n");
    exit(70);
  }
}
#endif

// MAIN ENTRY POINT
int main(int argc, const char* argv[]) {

  // Usage:
  //   ./asharp                    → REPL (bytecode VM)
  //   ./asharp script.as          → run file (bytecode VM)
  //   ./asharp_llvm --llvm script.as  → run file (LLVM JIT backend)

#ifdef USE_LLVM
  // Check for --llvm flag: asharp_llvm --llvm <file>
  if (argc == 3 && strcmp(argv[1], "--llvm") == 0) {
    runFileWithLLVM(argv[2]);
    return 0;
  }
#endif

  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: asharp [script.as]\n");
#ifdef USE_LLVM
    fprintf(stderr, "       asharp_llvm --llvm [script.as]\n");
#endif
    exit(64);
  }

  freeVM();
  return 0;
}

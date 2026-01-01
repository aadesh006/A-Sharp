#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"

static void repl() {
    char* line;
    
    // readline returns NULL on EOF (Ctrl+D)
    while ((line = readline("> ")) != NULL) {
        // Allow empty lines to be skipped
        if (strlen(line) == 0) {
            free(line);
            continue;
        }

        // Added the line to history so Up/Down arrows work
        add_history(line);

        Chunk chunk;
        initChunk(&chunk);

        if (compile(line, &chunk)) {
            interpret(&chunk);
        }

        freeChunk(&chunk);
        
        // readline allocates a new string for every line, 
        // free the memory
        free(line); 
    }
}

int main(int argc, const char* argv[]) {
    initVM();
    repl();
    freeVM();
    return 0;
}
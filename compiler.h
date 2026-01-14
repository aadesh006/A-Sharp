#ifndef clox_compiler_h
#define clox_compiler_h

#include "chunk.h"
#include "scanner.h"

ObjFunction* compile(const char* source);


#endif
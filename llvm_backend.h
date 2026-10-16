#ifndef asharp_llvm_backend_h
#define asharp_llvm_backend_h

// Only compile this header when building with LLVM support.
// The USE_LLVM flag is defined by the Makefile's llvm target.
#ifdef USE_LLVM

#include "vm.h"

// Result codes for the LLVM backend execution,
// mirroring InterpretResult from vm.h so main.c can handle both uniformly.
typedef enum {
    LLVM_OK,
    LLVM_COMPILE_ERROR,
    LLVM_RUNTIME_ERROR,
} LLVMBackendResult;

// Entry point: compiles the A# source string using the existing A# compiler,
// then translates the resulting bytecode chunk to LLVM IR and JIT-executes it.
LLVMBackendResult runWithLLVM(const char* source);

#endif // USE_LLVM
#endif // asharp_llvm_backend_h

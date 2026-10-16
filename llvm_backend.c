#ifdef USE_LLVM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>

#include "llvm_backend.h"
#include "compiler.h"
#include "chunk.h"
#include "value.h"
#include "object.h"
#include "vm.h"

// Compile-time value stack — mirrors the VM's runtime stack.
// Holds LLVMValueRef handles instead of actual values.
// See explain.md for a full walkthrough.
#define LLVM_STACK_MAX 256

typedef struct {
    LLVMValueRef slots[LLVM_STACK_MAX];
    int top;
} LLVMValueStack;

static void stackInit(LLVMValueStack* s) { s->top = 0; }

static void stackPush(LLVMValueStack* s, LLVMValueRef val) {
    if (s->top >= LLVM_STACK_MAX) {
        fprintf(stderr, "[LLVM Backend] Stack overflow during compilation.\n");
        exit(1);
    }
    s->slots[s->top++] = val;
}

static LLVMValueRef stackPop(LLVMValueStack* s) {
    if (s->top <= 0) {
        fprintf(stderr, "[LLVM Backend] Stack underflow during compilation.\n");
        exit(1);
    }
    return s->slots[--s->top];
}

// Will be used in future updates (variables, closures).
static __attribute__((unused)) LLVMValueRef stackPeek(LLVMValueStack* s, int distance) {
    return s->slots[s->top - 1 - distance];
}

// Walks the bytecode chunk and emits equivalent LLVM IR instructions.
static bool translateChunk(Chunk* chunk, LLVMBuilderRef builder,
                            LLVMValueRef printf_fn, LLVMValueRef fmt_double,
                            LLVMValueRef fmt_bool_true, LLVMValueRef fmt_bool_false,
                            LLVMValueRef fmt_nil) {

    LLVMValueStack stack;
    stackInit(&stack);

    int i = 0;

    while (i < chunk->count) {
        uint8_t opcode = chunk->code[i];

        switch (opcode) {

            case OP_CONSTANT: {
                uint8_t idx = chunk->code[i + 1];
                Value val = chunk->constants.values[idx];

                if (IS_NUMBER(val)) {
                    stackPush(&stack, LLVMConstReal(LLVMDoubleType(), AS_NUMBER(val)));
                } else if (IS_BOOL(val)) {
                    stackPush(&stack, LLVMConstInt(LLVMInt1Type(), AS_BOOL(val) ? 1 : 0, 0));
                } else if (IS_OBJ(val) && IS_STRING(val)) {
                    stackPush(&stack, LLVMBuildGlobalStringPtr(builder, AS_CSTRING(val), "str"));
                } else {
                    fprintf(stderr, "[LLVM Backend] Unsupported constant type.\n");
                    return false;
                }
                i += 2;
                break;
            }

            case OP_NIL: {
                stackPush(&stack, LLVMBuildGlobalStringPtr(builder, "nil", "nil_str"));
                i++;
                break;
            }

            case OP_TRUE: {
                stackPush(&stack, LLVMConstInt(LLVMInt1Type(), 1, 0));
                i++;
                break;
            }

            case OP_FALSE: {
                stackPush(&stack, LLVMConstInt(LLVMInt1Type(), 0, 0));
                i++;
                break;
            }

            case OP_POP: {
                if (stack.top > 0) stackPop(&stack);
                i++;
                break;
            }

            case OP_NEGATE: {
                LLVMValueRef operand = stackPop(&stack);
                stackPush(&stack, LLVMBuildFNeg(builder, operand, "neg"));
                i++;
                break;
            }

            case OP_NOT: {
                LLVMValueRef operand = stackPop(&stack);
                LLVMValueRef zero = LLVMConstReal(LLVMDoubleType(), 0.0);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOEQ, operand, zero, "not");
                stackPush(&stack, LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "not_d"));
                i++;
                break;
            }

            case OP_ADD: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                stackPush(&stack, LLVMBuildFAdd(builder, a, b, "add"));
                i++;
                break;
            }

            case OP_SUBTRACT: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                stackPush(&stack, LLVMBuildFSub(builder, a, b, "sub"));
                i++;
                break;
            }

            case OP_MULTIPLY: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                stackPush(&stack, LLVMBuildFMul(builder, a, b, "mul"));
                i++;
                break;
            }

            case OP_DIVIDE: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                stackPush(&stack, LLVMBuildFDiv(builder, a, b, "div"));
                i++;
                break;
            }

            case OP_GREATER: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOGT, a, b, "gt");
                stackPush(&stack, LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "gt_d"));
                i++;
                break;
            }

            case OP_LESS: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOLT, a, b, "lt");
                stackPush(&stack, LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "lt_d"));
                i++;
                break;
            }

            case OP_EQUAL: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOEQ, a, b, "eq");
                stackPush(&stack, LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "eq_d"));
                i++;
                break;
            }

            case OP_PRINT: {
                LLVMValueRef val = stackPop(&stack);
                LLVMTypeRef printfArgTypes[] = { LLVMPointerType(LLVMInt8Type(), 0) };
                LLVMTypeRef printfType = LLVMFunctionType(LLVMInt32Type(), printfArgTypes, 1, 1);
                LLVMValueRef args[] = { fmt_double, val };
                LLVMBuildCall2(builder, printfType, printf_fn, args, 2, "");
                i++;
                break;
            }

            case OP_RETURN: {
                if (stack.top > 0) stackPop(&stack);
                LLVMBuildRetVoid(builder);
                i++;
                goto done;
            }

            default: {
                fprintf(stderr, "[LLVM Backend] Unhandled opcode %d at offset %d, skipping.\n", opcode, i);
                i++;
                break;
            }
        }
    }

done:
    {
        LLVMBasicBlockRef lastBlock = LLVMGetInsertBlock(builder);
        if (lastBlock && !LLVMGetBasicBlockTerminator(lastBlock)) {
            LLVMBuildRetVoid(builder);
        }
    }

    return true;
}

LLVMBackendResult runWithLLVM(const char* source) {
    initVM();

    ObjFunction* function = compile(source);
    if (function == NULL) {
        freeVM();
        return LLVM_COMPILE_ERROR;
    }

    Chunk* chunk = &function->chunk;

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    LLVMLinkInMCJIT();

    LLVMModuleRef module = LLVMModuleCreateWithName("asharp_module");

    LLVMTypeRef printfParamTypes[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef printfType = LLVMFunctionType(LLVMInt32Type(), printfParamTypes, 1, 1);
    LLVMValueRef printf_fn = LLVMAddFunction(module, "printf", printfType);

    LLVMTypeRef mainFnType = LLVMFunctionType(LLVMVoidType(), NULL, 0, 0);
    LLVMValueRef mainFn = LLVMAddFunction(module, "asharp_main", mainFnType);

    LLVMBasicBlockRef entryBlock = LLVMAppendBasicBlock(mainFn, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entryBlock);

    LLVMValueRef fmt_double     = LLVMBuildGlobalStringPtr(builder, "%g\n",    "fmt_double");
    LLVMValueRef fmt_bool_true  = LLVMBuildGlobalStringPtr(builder, "true\n",  "fmt_true");
    LLVMValueRef fmt_bool_false = LLVMBuildGlobalStringPtr(builder, "false\n", "fmt_false");
    LLVMValueRef fmt_nil        = LLVMBuildGlobalStringPtr(builder, "nil\n",   "fmt_nil");

    bool ok = translateChunk(chunk, builder, printf_fn,
                             fmt_double, fmt_bool_true, fmt_bool_false, fmt_nil);
    if (!ok) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        freeVM();
        return LLVM_COMPILE_ERROR;
    }

    char* error = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "[LLVM Backend] IR verification failed:\n%s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        freeVM();
        return LLVM_COMPILE_ERROR;
    }
    LLVMDisposeMessage(error);

    // Uncomment to dump generated IR during debugging:
    // LLVMDumpModule(module);

    LLVMExecutionEngineRef engine;
    error = NULL;
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error)) {
        fprintf(stderr, "[LLVM Backend] Failed to create JIT engine:\n%s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeBuilder(builder);
        freeVM();
        return LLVM_RUNTIME_ERROR;
    }

    typedef void (*ASharpMainFn)(void);
    ASharpMainFn asharpMain = (ASharpMainFn)(uintptr_t)
        LLVMGetFunctionAddress(engine, "asharp_main");

    if (asharpMain == NULL) {
        fprintf(stderr, "[LLVM Backend] Could not find JIT-compiled function.\n");
        LLVMDisposeExecutionEngine(engine);
        LLVMDisposeBuilder(builder);
        freeVM();
        return LLVM_RUNTIME_ERROR;
    }

    asharpMain();

    LLVMDisposeExecutionEngine(engine);
    LLVMDisposeBuilder(builder);
    freeVM();

    return LLVM_OK;
}

#endif // USE_LLVM

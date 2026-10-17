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

// ---------------------------------------------------------------------------
// COMPILE-TIME VALUE STACK
// Mirrors the VM's runtime stack but holds LLVMValueRef handles.
// ---------------------------------------------------------------------------
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

static LLVMValueRef stackPeek(LLVMValueStack* s, int distance) {
    return s->slots[s->top - 1 - distance];
}

// ---------------------------------------------------------------------------
// LOCAL VARIABLE SLOTS
// Each local maps to an 'alloca' in the IR function's entry block.
// ---------------------------------------------------------------------------
#define LOCALS_MAX 256

typedef struct {
    LLVMValueRef slots[LOCALS_MAX];
    int count;
} LocalSlots;

static void localsInit(LocalSlots* l) {
    l->count = 0;
    for (int i = 0; i < LOCALS_MAX; i++) l->slots[i] = NULL;
}

static LLVMValueRef ensureLocal(LocalSlots* l, int idx,
                                LLVMBuilderRef builder, LLVMValueRef fn) {
    if (l->slots[idx] != NULL) return l->slots[idx];

    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef firstInstr = LLVMGetFirstInstruction(entry);

    LLVMBuilderRef tmp = LLVMCreateBuilder();
    if (firstInstr)
        LLVMPositionBuilderBefore(tmp, firstInstr);
    else
        LLVMPositionBuilderAtEnd(tmp, entry);

    char name[32];
    snprintf(name, sizeof(name), "local_%d", idx);
    LLVMValueRef alloca = LLVMBuildAlloca(tmp, LLVMDoubleType(), name);
    LLVMDisposeBuilder(tmp);

    l->slots[idx] = alloca;
    if (idx >= l->count) l->count = idx + 1;
    return alloca;
}

// ---------------------------------------------------------------------------
// GLOBAL VARIABLES
// Each A# global becomes an LLVM global double in the module.
// ---------------------------------------------------------------------------
#define GLOBALS_MAX 256

typedef struct { const char* name; LLVMValueRef gv; } GlobalEntry;
typedef struct { GlobalEntry entries[GLOBALS_MAX]; int count; } GlobalMap;

static void globalsInit(GlobalMap* g) { g->count = 0; }

static LLVMValueRef findOrCreateGlobal(GlobalMap* g, const char* name,
                                       LLVMModuleRef module) {
    for (int i = 0; i < g->count; i++)
        if (strcmp(g->entries[i].name, name) == 0)
            return g->entries[i].gv;

    LLVMValueRef gv = LLVMAddGlobal(module, LLVMDoubleType(), name);
    LLVMSetInitializer(gv, LLVMConstReal(LLVMDoubleType(), 0.0));
    g->entries[g->count].name = name;
    g->entries[g->count].gv   = gv;
    g->count++;
    return gv;
}

// ---------------------------------------------------------------------------
// TRANSLATION CONTEXT
// ---------------------------------------------------------------------------
typedef struct {
    LLVMBuilderRef  builder;
    LLVMValueRef    fn;
    LLVMModuleRef   module;
    LLVMValueRef    printf_fn;
    LLVMValueRef    fmt_double;
    LLVMValueStack  stack;
    LocalSlots      locals;
    GlobalMap       globals;
} TranslateCtx;

// ---------------------------------------------------------------------------
// EMIT PRINT
// ---------------------------------------------------------------------------
static void emitPrint(TranslateCtx* ctx, LLVMValueRef val) {
    LLVMTypeRef pCharTy = LLVMPointerType(LLVMInt8Type(), 0);
    LLVMTypeRef printfArgTypes[] = { pCharTy };
    LLVMTypeRef printfType = LLVMFunctionType(LLVMInt32Type(), printfArgTypes, 1, 1);

    LLVMTypeRef valType = LLVMTypeOf(val);
    if (valType == pCharTy) {
        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder, "%s\n", "fmt_s");
        LLVMValueRef args[] = { fmt, val };
        LLVMBuildCall2(ctx->builder, printfType, ctx->printf_fn, args, 2, "");
    } else {
        LLVMValueRef args[] = { ctx->fmt_double, val };
        LLVMBuildCall2(ctx->builder, printfType, ctx->printf_fn, args, 2, "");
    }
}

// ---------------------------------------------------------------------------
// HELPER: close the current basic block with a branch to 'dest'
// if it doesn't already have a terminator (ret, br, etc.).
// ---------------------------------------------------------------------------
static void closeBranchTo(TranslateCtx* ctx, LLVMBasicBlockRef dest) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (cur && !LLVMGetBasicBlockTerminator(cur))
        LLVMBuildBr(ctx->builder, dest);
}

// ---------------------------------------------------------------------------
// MAIN TRANSLATION LOOP
//
// We do a two-pass approach for control flow:
//   Pass 1 (pre-scan): Walk the bytecode and record all jump targets,
//                      creating empty basic blocks for each.
//   Pass 2 (emit):     Walk again and emit IR, branching to pre-created blocks.
//
// This handles forward references (if/else) and back references (loops).
// ---------------------------------------------------------------------------

// Maps a bytecode offset to its corresponding LLVM basic block.
#define BLOCK_MAP_SIZE 4096

typedef struct {
    LLVMBasicBlockRef blocks[BLOCK_MAP_SIZE];
} BlockMap;

static void blockMapInit(BlockMap* bm) {
    for (int i = 0; i < BLOCK_MAP_SIZE; i++) bm->blocks[i] = NULL;
}

static LLVMBasicBlockRef blockMapGet(BlockMap* bm, TranslateCtx* ctx, int offset) {
    if (offset < 0 || offset >= BLOCK_MAP_SIZE) return NULL;
    if (bm->blocks[offset] == NULL) {
        char name[32];
        snprintf(name, sizeof(name), "bb_%d", offset);
        bm->blocks[offset] = LLVMAppendBasicBlock(ctx->fn, name);
    }
    return bm->blocks[offset];
}

static bool translateChunk(TranslateCtx* ctx, Chunk* chunk) {
    BlockMap bm;
    blockMapInit(&bm);

    // Pre-scan: create a basic block for every jump target.
    for (int i = 0; i < chunk->count; ) {
        uint8_t op = chunk->code[i];
        if (op == OP_JUMP_IF_FALSE || op == OP_JUMP) {
            uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
            int target = i + 3 + (int)off;
            blockMapGet(&bm, ctx, target);
            blockMapGet(&bm, ctx, i + 3); // fall-through also needs a block
            i += 3;
        } else if (op == OP_LOOP) {
            uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
            int target = i + 3 - (int)off;
            blockMapGet(&bm, ctx, target);
            blockMapGet(&bm, ctx, i + 3);
            i += 3;
        } else if (op == OP_CONSTANT || op == OP_GET_LOCAL || op == OP_SET_LOCAL ||
                   op == OP_GET_GLOBAL || op == OP_SET_GLOBAL || op == OP_DEFINE_GLOBAL ||
                   op == OP_GET_UPVALUE || op == OP_SET_UPVALUE) {
            i += 2;
        } else if (op == OP_CLOSURE) {
            // skip closure instruction and its upvalue pairs
            i += 2;
            ObjFunction* fn = AS_FUNCTION(chunk->constants.values[chunk->code[i-1]]);
            i += fn->upvalueCount * 2;
        } else {
            i++;
        }
    }

    // Emit pass: translate opcodes to IR.
    int i = 0;
    while (i < chunk->count) {
        // If this offset has a pre-created block, we need to switch to it.
        // First, close the current block with a fall-through branch if needed.
        if (bm.blocks[i] != NULL) {
            LLVMBasicBlockRef target = bm.blocks[i];
            closeBranchTo(ctx, target);
            LLVMPositionBuilderAtEnd(ctx->builder, target);
        }

        uint8_t opcode = chunk->code[i];

        switch (opcode) {

            // -- CONSTANTS & LITERALS --

            case OP_CONSTANT: {
                uint8_t idx = chunk->code[i + 1];
                Value val = chunk->constants.values[idx];
                if (IS_NUMBER(val)) {
                    stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), AS_NUMBER(val)));
                } else if (IS_BOOL(val)) {
                    stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), AS_BOOL(val) ? 1.0 : 0.0));
                } else if (IS_OBJ(val) && IS_STRING(val)) {
                    stackPush(&ctx->stack, LLVMBuildGlobalStringPtr(ctx->builder, AS_CSTRING(val), "str"));
                } else {
                    fprintf(stderr, "[LLVM Backend] Unsupported constant type.\n");
                    return false;
                }
                i += 2; break;
            }

            case OP_NIL:
                stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 0.0));
                i++; break;
            case OP_TRUE:
                stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 1.0));
                i++; break;
            case OP_FALSE:
                stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 0.0));
                i++; break;

            case OP_POP:
                if (ctx->stack.top > 0) stackPop(&ctx->stack);
                i++; break;

            // -- ARITHMETIC --

            case OP_NEGATE:
                stackPush(&ctx->stack, LLVMBuildFNeg(ctx->builder, stackPop(&ctx->stack), "neg"));
                i++; break;

            case OP_ADD: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFAdd(ctx->builder, a, b, "add"));
                i++; break;
            }
            case OP_SUBTRACT: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFSub(ctx->builder, a, b, "sub"));
                i++; break;
            }
            case OP_MULTIPLY: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFMul(ctx->builder, a, b, "mul"));
                i++; break;
            }
            case OP_DIVIDE: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFDiv(ctx->builder, a, b, "div"));
                i++; break;
            }

            // -- COMPARISON --

            case OP_NOT: {
                LLVMValueRef v = stackPop(&ctx->stack);
                LLVMValueRef cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, v,
                                                  LLVMConstReal(LLVMDoubleType(), 0.0), "not_cmp");
                stackPush(&ctx->stack, LLVMBuildUIToFP(ctx->builder, cmp, LLVMDoubleType(), "not"));
                i++; break;
            }
            case OP_GREATER: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                LLVMValueRef cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, a, b, "gt");
                stackPush(&ctx->stack, LLVMBuildUIToFP(ctx->builder, cmp, LLVMDoubleType(), "gt_d"));
                i++; break;
            }
            case OP_LESS: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                LLVMValueRef cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOLT, a, b, "lt");
                stackPush(&ctx->stack, LLVMBuildUIToFP(ctx->builder, cmp, LLVMDoubleType(), "lt_d"));
                i++; break;
            }
            case OP_EQUAL: {
                LLVMValueRef b = stackPop(&ctx->stack), a = stackPop(&ctx->stack);
                LLVMValueRef cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, a, b, "eq");
                stackPush(&ctx->stack, LLVMBuildUIToFP(ctx->builder, cmp, LLVMDoubleType(), "eq_d"));
                i++; break;
            }

            // -- LOCAL VARIABLES --

            case OP_GET_LOCAL: {
                uint8_t slot = chunk->code[i + 1];
                LLVMValueRef alloca = ensureLocal(&ctx->locals, slot, ctx->builder, ctx->fn);
                stackPush(&ctx->stack, LLVMBuildLoad2(ctx->builder, LLVMDoubleType(), alloca, "local"));
                i += 2; break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = chunk->code[i + 1];
                LLVMValueRef alloca = ensureLocal(&ctx->locals, slot, ctx->builder, ctx->fn);
                LLVMBuildStore(ctx->builder, stackPeek(&ctx->stack, 0), alloca);
                i += 2; break;
            }

            // -- GLOBAL VARIABLES --

            case OP_DEFINE_GLOBAL: {
                uint8_t idx = chunk->code[i + 1];
                const char* name = AS_CSTRING(chunk->constants.values[idx]);
                LLVMValueRef gv = findOrCreateGlobal(&ctx->globals, name, ctx->module);
                LLVMBuildStore(ctx->builder, stackPop(&ctx->stack), gv);
                i += 2; break;
            }
            case OP_GET_GLOBAL: {
                uint8_t idx = chunk->code[i + 1];
                const char* name = AS_CSTRING(chunk->constants.values[idx]);
                LLVMValueRef gv = findOrCreateGlobal(&ctx->globals, name, ctx->module);
                stackPush(&ctx->stack, LLVMBuildLoad2(ctx->builder, LLVMDoubleType(), gv, "global"));
                i += 2; break;
            }
            case OP_SET_GLOBAL: {
                uint8_t idx = chunk->code[i + 1];
                const char* name = AS_CSTRING(chunk->constants.values[idx]);
                LLVMValueRef gv = findOrCreateGlobal(&ctx->globals, name, ctx->module);
                LLVMBuildStore(ctx->builder, stackPeek(&ctx->stack, 0), gv);
                i += 2; break;
            }

            // -- CONTROL FLOW --
            //
            // We pre-created basic blocks for all jump targets in the pre-scan above.
            // Here we just emit the branch instructions to those blocks.

            case OP_JUMP_IF_FALSE: {
                uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
                int falseTarget = i + 3 + (int)off; // where to go if falsy
                int trueTarget  = i + 3;             // fall-through (condition was true)

                LLVMBasicBlockRef falseBlock = blockMapGet(&bm, ctx, falseTarget);
                LLVMBasicBlockRef trueBlock  = blockMapGet(&bm, ctx, trueTarget);

                LLVMValueRef cond = stackPop(&ctx->stack);
                LLVMValueRef zero = LLVMConstReal(LLVMDoubleType(), 0.0);
                LLVMValueRef isFalsy = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, cond, zero, "is_falsy");

                LLVMBuildCondBr(ctx->builder, isFalsy, falseBlock, trueBlock);
                i += 3; break;
            }

            case OP_JUMP: {
                uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
                int target = i + 3 + (int)off;
                LLVMBasicBlockRef dest = blockMapGet(&bm, ctx, target);
                closeBranchTo(ctx, dest);
                i += 3; break;
            }

            case OP_LOOP: {
                uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
                int target = i + 3 - (int)off; // backward jump
                LLVMBasicBlockRef dest = blockMapGet(&bm, ctx, target);
                closeBranchTo(ctx, dest);
                i += 3; break;
            }

            // -- PRINT --

            case OP_PRINT:
                emitPrint(ctx, stackPop(&ctx->stack));
                i++; break;

            // -- RETURN --

            case OP_RETURN:
                if (ctx->stack.top > 0) stackPop(&ctx->stack);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                    LLVMBuildRetVoid(ctx->builder);
                i++;
                goto done;

            // -- UPVALUES (skip for now, closures come later) --
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE:
                fprintf(stderr, "[LLVM Backend] Upvalues not yet supported.\n");
                i += 2; break;
            case OP_CLOSE_UPVALUE:
                i++; break;
            case OP_CLOSURE:
                fprintf(stderr, "[LLVM Backend] Closures not yet supported.\n");
                i += 2;
                i += AS_FUNCTION(chunk->constants.values[chunk->code[i-1]])->upvalueCount * 2;
                break;
            case OP_CALL:
                fprintf(stderr, "[LLVM Backend] Function calls not yet supported.\n");
                i += 2; break;

            default:
                fprintf(stderr, "[LLVM Backend] Unhandled opcode %d at offset %d.\n", opcode, i);
                i++; break;
        }
    }

done:
    {
        LLVMBasicBlockRef last = LLVMGetInsertBlock(ctx->builder);
        if (last && !LLVMGetBasicBlockTerminator(last))
            LLVMBuildRetVoid(ctx->builder);
    }
    return true;
}

// ---------------------------------------------------------------------------
// PUBLIC ENTRY POINT
// ---------------------------------------------------------------------------
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

    TranslateCtx ctx;
    ctx.builder    = builder;
    ctx.fn         = mainFn;
    ctx.module     = module;
    ctx.printf_fn  = printf_fn;
    ctx.fmt_double = LLVMBuildGlobalStringPtr(builder, "%g\n", "fmt_double");
    stackInit(&ctx.stack);
    localsInit(&ctx.locals);
    globalsInit(&ctx.globals);

    bool ok = translateChunk(&ctx, chunk);
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

    // Uncomment to inspect generated IR:
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

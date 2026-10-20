#ifdef USE_LLVM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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
// ---------------------------------------------------------------------------
#define LLVM_STACK_MAX 256

typedef struct {
    LLVMValueRef slots[LLVM_STACK_MAX];
    int top;
} LLVMValueStack;

static void stackInit(LLVMValueStack* s) { s->top = 0; }

static void stackPush(LLVMValueStack* s, LLVMValueRef val) {
    if (s->top >= LLVM_STACK_MAX) { fprintf(stderr, "[LLVM] Stack overflow.\n"); exit(1); }
    s->slots[s->top++] = val;
}

static LLVMValueRef stackPop(LLVMValueStack* s) {
    if (s->top <= 0) { fprintf(stderr, "[LLVM] Stack underflow.\n"); exit(1); }
    return s->slots[--s->top];
}

static LLVMValueRef stackPeek(LLVMValueStack* s, int dist) {
    return s->slots[s->top - 1 - dist];
}

// ---------------------------------------------------------------------------
// LOCAL VARIABLE SLOTS (alloca-based)
// ---------------------------------------------------------------------------
#define LOCALS_MAX 256

typedef struct { LLVMValueRef slots[LOCALS_MAX]; int count; } LocalSlots;

static void localsInit(LocalSlots* l) {
    l->count = 0;
    for (int i = 0; i < LOCALS_MAX; i++) l->slots[i] = NULL;
}

static LLVMValueRef ensureLocal(LocalSlots* l, int idx,
                                LLVMBuilderRef builder, LLVMValueRef fn) {
    if (l->slots[idx]) return l->slots[idx];
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    LLVMBuilderRef tmp = LLVMCreateBuilder();
    if (first) LLVMPositionBuilderBefore(tmp, first);
    else        LLVMPositionBuilderAtEnd(tmp, entry);
    char name[32]; snprintf(name, sizeof(name), "local_%d", idx);
    LLVMValueRef alloca = LLVMBuildAlloca(tmp, LLVMDoubleType(), name);
    LLVMDisposeBuilder(tmp);
    l->slots[idx] = alloca;
    if (idx >= l->count) l->count = idx + 1;
    return alloca;
}

// ---------------------------------------------------------------------------
// GLOBAL VARIABLES
// ---------------------------------------------------------------------------
#define GLOBALS_MAX 256

typedef struct { const char* name; LLVMValueRef gv; } GlobalEntry;
typedef struct { GlobalEntry entries[GLOBALS_MAX]; int count; } GlobalMap;

static void globalsInit(GlobalMap* g) { g->count = 0; }

static LLVMValueRef findOrCreateGlobal(GlobalMap* g, const char* name, LLVMModuleRef mod) {
    for (int i = 0; i < g->count; i++)
        if (strcmp(g->entries[i].name, name) == 0) return g->entries[i].gv;
    LLVMValueRef gv = LLVMAddGlobal(mod, LLVMDoubleType(), name);
    LLVMSetInitializer(gv, LLVMConstReal(LLVMDoubleType(), 0.0));
    g->entries[g->count].name = name;
    g->entries[g->count].gv   = gv;
    g->count++;
    return gv;
}

// ---------------------------------------------------------------------------
// FUNCTION REGISTRY
//
// Maps ObjFunction* → LLVMValueRef (the compiled LLVM function).
// We pre-compile every function before translating call sites.
// All A# functions have the signature: double fn(double, double, ...)
// where the number of doubles equals the function's arity.
// ---------------------------------------------------------------------------
#define FUNC_REGISTRY_MAX 256

typedef struct {
    ObjFunction*  objFn;
    LLVMValueRef  llvmFn;
    LLVMTypeRef   llvmFnType;
} FuncEntry;

typedef struct {
    FuncEntry entries[FUNC_REGISTRY_MAX];
    int count;
} FuncRegistry;

static void funcRegistryInit(FuncRegistry* r) { r->count = 0; }

static LLVMTypeRef makeFnType(int arity) {
    // Build: double (double, double, ...) for 'arity' params
    LLVMTypeRef* paramTypes = NULL;
    if (arity > 0) {
        paramTypes = (LLVMTypeRef*)malloc(arity * sizeof(LLVMTypeRef));
        for (int i = 0; i < arity; i++) paramTypes[i] = LLVMDoubleType();
    }
    LLVMTypeRef fnType = LLVMFunctionType(LLVMDoubleType(), paramTypes, arity, 0);
    if (paramTypes) free(paramTypes);
    return fnType;
}

// Declare (but don't yet define) an LLVM function for an ObjFunction.
static FuncEntry* registerFunction(FuncRegistry* r, ObjFunction* objFn, LLVMModuleRef mod) {
    // Check if already registered
    for (int i = 0; i < r->count; i++)
        if (r->entries[i].objFn == objFn) return &r->entries[i];

    char name[64];
    if (objFn->name != NULL)
        snprintf(name, sizeof(name), "asharp_fn_%s", objFn->name->chars);
    else
        snprintf(name, sizeof(name), "asharp_fn_%p", (void*)objFn);

    LLVMTypeRef fnType = makeFnType(objFn->arity);
    LLVMValueRef llvmFn = LLVMAddFunction(mod, name, fnType);

    FuncEntry* e = &r->entries[r->count++];
    e->objFn     = objFn;
    e->llvmFn    = llvmFn;
    e->llvmFnType = fnType;
    return e;
}

static FuncEntry* lookupFunction(FuncRegistry* r, ObjFunction* objFn) {
    for (int i = 0; i < r->count; i++)
        if (r->entries[i].objFn == objFn) return &r->entries[i];
    return NULL;
}

// ---------------------------------------------------------------------------
// NATIVE FUNCTION REGISTRY
// Maps A# native function names to their LLVM function declarations.
// ---------------------------------------------------------------------------
typedef struct { const char* name; LLVMValueRef fn; LLVMTypeRef type; } NativeEntry;
#define NATIVES_MAX 16
typedef struct { NativeEntry entries[NATIVES_MAX]; int count; } NativeRegistry;

static void nativesInit(NativeRegistry* n) { n->count = 0; }

static void registerNative(NativeRegistry* n, const char* name,
                           LLVMValueRef fn, LLVMTypeRef type) {
    n->entries[n->count].name = name;
    n->entries[n->count].fn   = fn;
    n->entries[n->count].type = type;
    n->count++;
}

static NativeEntry* lookupNative(NativeRegistry* n, const char* name) {
    for (int i = 0; i < n->count; i++)
        if (strcmp(n->entries[i].name, name) == 0) return &n->entries[i];
    return NULL;
}
#define BLOCK_MAP_SIZE 4096

typedef struct { LLVMBasicBlockRef blocks[BLOCK_MAP_SIZE]; } BlockMap;

static void blockMapInit(BlockMap* bm) {
    for (int i = 0; i < BLOCK_MAP_SIZE; i++) bm->blocks[i] = NULL;
}

static LLVMBasicBlockRef blockMapGet(BlockMap* bm, LLVMValueRef fn, int offset) {
    if (offset < 0 || offset >= BLOCK_MAP_SIZE) return NULL;
    if (!bm->blocks[offset]) {
        char name[32]; snprintf(name, sizeof(name), "bb_%d", offset);
        bm->blocks[offset] = LLVMAppendBasicBlock(fn, name);
    }
    return bm->blocks[offset];
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
    GlobalMap*      globals;
    FuncRegistry*   funcs;
    NativeRegistry* natives;
    NativeEntry*    pendingNative; // set by OP_GET_GLOBAL when callee is native
    bool            isTopLevel;
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

static void closeBranchTo(TranslateCtx* ctx, LLVMBasicBlockRef dest) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (cur && !LLVMGetBasicBlockTerminator(cur))
        LLVMBuildBr(ctx->builder, dest);
}

// ---------------------------------------------------------------------------
// CORE TRANSLATION LOOP
// Translates a single Chunk (top-level or function body) to LLVM IR.
// ---------------------------------------------------------------------------
static bool translateChunk(TranslateCtx* ctx, Chunk* chunk) {
    BlockMap bm;
    blockMapInit(&bm);

    // Pre-scan: create basic blocks for all jump targets.
    for (int i = 0; i < chunk->count; ) {
        uint8_t op = chunk->code[i];
        if (op == OP_JUMP_IF_FALSE || op == OP_JUMP) {
            uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
            blockMapGet(&bm, ctx->fn, i + 3 + (int)off);
            blockMapGet(&bm, ctx->fn, i + 3);
            i += 3;
        } else if (op == OP_LOOP) {
            uint16_t off = (uint16_t)((chunk->code[i+1] << 8) | chunk->code[i+2]);
            blockMapGet(&bm, ctx->fn, i + 3 - (int)off);
            blockMapGet(&bm, ctx->fn, i + 3);
            i += 3;
        } else if (op == OP_CONSTANT || op == OP_GET_LOCAL || op == OP_SET_LOCAL ||
                   op == OP_GET_GLOBAL || op == OP_SET_GLOBAL || op == OP_DEFINE_GLOBAL ||
                   op == OP_GET_UPVALUE || op == OP_SET_UPVALUE || op == OP_CALL) {
            i += 2;
        } else if (op == OP_CLOSURE) {
            i += 2;
            ObjFunction* fn = AS_FUNCTION(chunk->constants.values[chunk->code[i-1]]);
            i += fn->upvalueCount * 2;
        } else {
            i++;
        }
    }

    // Emit pass.
    int i = 0;
    while (i < chunk->count) {
        // Switch to pre-created block if this offset has one.
        if (i < BLOCK_MAP_SIZE && bm.blocks[i]) {
            closeBranchTo(ctx, bm.blocks[i]);
            LLVMPositionBuilderAtEnd(ctx->builder, bm.blocks[i]);
        }

        uint8_t op = chunk->code[i];

        switch (op) {

            // -- CONSTANTS & LITERALS --
            case OP_CONSTANT: {
                uint8_t idx = chunk->code[i+1];
                Value val = chunk->constants.values[idx];
                if (IS_NUMBER(val))
                    stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), AS_NUMBER(val)));
                else if (IS_BOOL(val))
                    stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), AS_BOOL(val) ? 1.0 : 0.0));
                else if (IS_OBJ(val) && IS_STRING(val))
                    stackPush(&ctx->stack, LLVMBuildGlobalStringPtr(ctx->builder, AS_CSTRING(val), "str"));
                else { fprintf(stderr, "[LLVM] Unsupported constant.\n"); return false; }
                i += 2; break;
            }
            case OP_NIL:   stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 0.0)); i++; break;
            case OP_TRUE:  stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 1.0)); i++; break;
            case OP_FALSE: stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 0.0)); i++; break;
            case OP_POP:   if (ctx->stack.top > 0) stackPop(&ctx->stack); i++; break;

            // -- ARITHMETIC --
            case OP_NEGATE:
                stackPush(&ctx->stack, LLVMBuildFNeg(ctx->builder, stackPop(&ctx->stack), "neg"));
                i++; break;
            case OP_ADD: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFAdd(ctx->builder,a,b,"add")); i++; break; }
            case OP_SUBTRACT: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFSub(ctx->builder,a,b,"sub")); i++; break; }
            case OP_MULTIPLY: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFMul(ctx->builder,a,b,"mul")); i++; break; }
            case OP_DIVIDE: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                stackPush(&ctx->stack, LLVMBuildFDiv(ctx->builder,a,b,"div")); i++; break; }

            // -- COMPARISON --
            case OP_NOT: {
                LLVMValueRef cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ,
                    stackPop(&ctx->stack), LLVMConstReal(LLVMDoubleType(),0.0), "not_cmp");
                stackPush(&ctx->stack, LLVMBuildUIToFP(ctx->builder,cmp,LLVMDoubleType(),"not"));
                i++; break;
            }
            case OP_GREATER: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                LLVMValueRef c=LLVMBuildFCmp(ctx->builder,LLVMRealOGT,a,b,"gt");
                stackPush(&ctx->stack,LLVMBuildUIToFP(ctx->builder,c,LLVMDoubleType(),"gt_d")); i++; break; }
            case OP_LESS: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                LLVMValueRef c=LLVMBuildFCmp(ctx->builder,LLVMRealOLT,a,b,"lt");
                stackPush(&ctx->stack,LLVMBuildUIToFP(ctx->builder,c,LLVMDoubleType(),"lt_d")); i++; break; }
            case OP_EQUAL: { LLVMValueRef b=stackPop(&ctx->stack), a=stackPop(&ctx->stack);
                LLVMValueRef c=LLVMBuildFCmp(ctx->builder,LLVMRealOEQ,a,b,"eq");
                stackPush(&ctx->stack,LLVMBuildUIToFP(ctx->builder,c,LLVMDoubleType(),"eq_d")); i++; break; }

            // -- LOCAL VARIABLES --
            case OP_GET_LOCAL: {
                uint8_t slot = chunk->code[i+1];
                LLVMValueRef alloca = ensureLocal(&ctx->locals, slot, ctx->builder, ctx->fn);
                stackPush(&ctx->stack, LLVMBuildLoad2(ctx->builder,LLVMDoubleType(),alloca,"local"));
                i += 2; break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = chunk->code[i+1];
                LLVMValueRef alloca = ensureLocal(&ctx->locals, slot, ctx->builder, ctx->fn);
                LLVMBuildStore(ctx->builder, stackPeek(&ctx->stack,0), alloca);
                i += 2; break;
            }

            // -- GLOBAL VARIABLES --
            case OP_DEFINE_GLOBAL: {
                uint8_t idx = chunk->code[i+1];
                const char* name = AS_CSTRING(chunk->constants.values[idx]);
                LLVMValueRef gv = findOrCreateGlobal(ctx->globals, name, ctx->module);
                LLVMBuildStore(ctx->builder, stackPop(&ctx->stack), gv);
                i += 2; break;
            }
            case OP_GET_GLOBAL: {
                uint8_t idx = chunk->code[i+1];
                const char* name = AS_CSTRING(chunk->constants.values[idx]);
                // Check if this is a native function call
                NativeEntry* native = lookupNative(ctx->natives, name);
                if (native) {
                    // Mark pending native; push a dummy 0.0 to keep stack balanced
                    ctx->pendingNative = native;
                    stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 0.0));
                } else {
                    ctx->pendingNative = NULL;
                    LLVMValueRef gv = findOrCreateGlobal(ctx->globals, name, ctx->module);
                    stackPush(&ctx->stack, LLVMBuildLoad2(ctx->builder,LLVMDoubleType(),gv,"global"));
                }
                i += 2; break;
            }
            case OP_SET_GLOBAL: {
                uint8_t idx = chunk->code[i+1];
                const char* name = AS_CSTRING(chunk->constants.values[idx]);
                LLVMValueRef gv = findOrCreateGlobal(ctx->globals, name, ctx->module);
                LLVMBuildStore(ctx->builder, stackPeek(&ctx->stack,0), gv);
                i += 2; break;
            }

            // -- CONTROL FLOW --
            case OP_JUMP_IF_FALSE: {
                uint16_t off = (uint16_t)((chunk->code[i+1]<<8)|chunk->code[i+2]);
                LLVMBasicBlockRef falseBlk = blockMapGet(&bm, ctx->fn, i+3+(int)off);
                LLVMBasicBlockRef trueBlk  = blockMapGet(&bm, ctx->fn, i+3);
                LLVMValueRef cond = stackPop(&ctx->stack);
                LLVMValueRef isFalsy = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, cond,
                                                      LLVMConstReal(LLVMDoubleType(),0.0), "falsy");
                LLVMBuildCondBr(ctx->builder, isFalsy, falseBlk, trueBlk);
                i += 3; break;
            }
            case OP_JUMP: {
                uint16_t off = (uint16_t)((chunk->code[i+1]<<8)|chunk->code[i+2]);
                closeBranchTo(ctx, blockMapGet(&bm, ctx->fn, i+3+(int)off));
                i += 3; break;
            }
            case OP_LOOP: {
                uint16_t off = (uint16_t)((chunk->code[i+1]<<8)|chunk->code[i+2]);
                closeBranchTo(ctx, blockMapGet(&bm, ctx->fn, i+3-(int)off));
                i += 3; break;
            }

            // -- FUNCTIONS --
            //
            // OP_CLOSURE: The bytecode stores an ObjFunction in the constants array.
            // We look it up in our FuncRegistry (pre-registered before this pass),
            // then push the LLVM function pointer cast to double onto the value stack.
            // This is our way of representing "a callable" on the stack without
            // building a full tagged-value system.
            //
            // OP_CALL argCount: Pop argCount args + the callee from the stack.
            // Emit an LLVM call instruction using the callee's type from FuncRegistry.
            case OP_CLOSURE: {
                uint8_t idx = chunk->code[i+1];
                ObjFunction* objFn = AS_FUNCTION(chunk->constants.values[idx]);
                FuncEntry* entry = lookupFunction(ctx->funcs, objFn);
                if (!entry) {
                    fprintf(stderr, "[LLVM] Function not pre-registered: %s\n",
                            objFn->name ? objFn->name->chars : "<anon>");
                    return false;
                }
                // Cast function pointer to double so it fits on our value stack.
                // We cast it back to a function pointer at the call site.
                LLVMValueRef fnPtrAsInt = LLVMBuildPtrToInt(ctx->builder,
                    entry->llvmFn, LLVMInt64Type(), "fn_as_int");
                LLVMValueRef fnPtrAsDouble = LLVMBuildBitCast(ctx->builder,
                    fnPtrAsInt, LLVMDoubleType(), "fn_as_double");
                stackPush(&ctx->stack, fnPtrAsDouble);
                // Skip upvalue payload (closures not supported yet)
                i += 2 + objFn->upvalueCount * 2;
                break;
            }

            case OP_CALL: {
                uint8_t argCount = chunk->code[i+1];

                // Stack layout at call site: [callee, arg0, arg1, ..., argN-1]
                // Collect args first (they sit on top), then pop callee.
                LLVMValueRef* args = (LLVMValueRef*)malloc(argCount * sizeof(LLVMValueRef));
                for (int a = argCount - 1; a >= 0; a--)
                    args[a] = stackPop(&ctx->stack);

                // Pop the callee slot
                LLVMValueRef calleeVal = stackPop(&ctx->stack);

                LLVMValueRef result;

                if (ctx->pendingNative) {
                    // Native function: call the pre-declared LLVM extern directly
                    NativeEntry* nat = ctx->pendingNative;
                    ctx->pendingNative = NULL;
                    result = LLVMBuildCall2(ctx->builder, nat->type,
                                           nat->fn, args, argCount, "native_result");
                    
                    // Special handling for clock(): returns int64, needs conversion to seconds
                    if (strcmp(nat->name, "clock") == 0) {
                        // Convert clock_t (int64) to double
                        LLVMValueRef ticks = result;
                        LLVMValueRef ticksDouble = LLVMBuildSIToFP(ctx->builder,
                            ticks, LLVMDoubleType(), "ticks_double");
                        // Divide by CLOCKS_PER_SEC (typically 1000000 on Linux)
                        LLVMValueRef divisor = LLVMConstReal(LLVMDoubleType(), 1000000.0);
                        result = LLVMBuildFDiv(ctx->builder, ticksDouble, divisor, "seconds");
                    }
                } else {
                    // User-defined function: callee is a fn-ptr stored as double
                    LLVMValueRef calleeAsInt = LLVMBuildBitCast(ctx->builder,
                        calleeVal, LLVMInt64Type(), "callee_int");
                    LLVMTypeRef fnType = makeFnType(argCount);
                    LLVMTypeRef fnPtrType = LLVMPointerType(fnType, 0);
                    LLVMValueRef calleeFnPtr = LLVMBuildIntToPtr(ctx->builder,
                        calleeAsInt, fnPtrType, "callee_ptr");
                    result = LLVMBuildCall2(ctx->builder, fnType,
                        calleeFnPtr, args, argCount, "call_result");
                }

                free(args);
                stackPush(&ctx->stack, result);
                i += 2; break;
            }

            // -- PRINT --
            case OP_PRINT:
                emitPrint(ctx, stackPop(&ctx->stack));
                i++; break;

            // -- RETURN --
            //
            // Top-level script: ret void and stop translation.
            // Functions: emit ret double and CONTINUE the loop — the next block
            // (post-if merge block) still needs to be translated.
            // Only goto done when we're at the very last OP_RETURN of the chunk.
            case OP_RETURN: {
                LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
                if (cur && !LLVMGetBasicBlockTerminator(cur)) {
                    if (ctx->isTopLevel) {
                        if (ctx->stack.top > 0) stackPop(&ctx->stack);
                        LLVMBuildRetVoid(ctx->builder);
                    } else {
                        LLVMValueRef retVal = ctx->stack.top > 0
                            ? stackPop(&ctx->stack)
                            : LLVMConstReal(LLVMDoubleType(), 0.0);
                        LLVMBuildRet(ctx->builder, retVal);
                    }
                } else {
                    // Block already terminated (e.g. we're unreachable here)
                    if (ctx->stack.top > 0) stackPop(&ctx->stack);
                }
                i++;
                // Only stop translation if this is the final return at end of chunk
                if (i >= chunk->count) goto done;
                break;
            }

            // -- UPVALUES (closures — future work) --
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE:
                stackPush(&ctx->stack, LLVMConstReal(LLVMDoubleType(), 0.0));
                i += 2; break;
            case OP_CLOSE_UPVALUE:
                if (ctx->stack.top > 0) stackPop(&ctx->stack);
                i++; break;

            default:
                fprintf(stderr, "[LLVM] Unhandled opcode %d at offset %d.\n", op, i);
                i++; break;
        }
    }

done:
    {
        // Terminate any basic blocks that were pre-created but never given a
        // terminator. This happens when an early 'return' inside an if-body
        // causes us to skip some blocks entirely.
        LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(ctx->fn);
        while (bb) {
            if (!LLVMGetBasicBlockTerminator(bb)) {
                LLVMPositionBuilderAtEnd(ctx->builder, bb);
                if (ctx->isTopLevel)
                    LLVMBuildRetVoid(ctx->builder);
                else
                    LLVMBuildRet(ctx->builder, LLVMConstReal(LLVMDoubleType(), 0.0));
            }
            bb = LLVMGetNextBasicBlock(bb);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// PRE-REGISTER: walk ALL functions reachable from a chunk (recursively).
// We must declare every LLVM function before compiling any call sites.
// ---------------------------------------------------------------------------
static void preRegisterFunctions(Chunk* chunk, FuncRegistry* reg, LLVMModuleRef mod) {
    for (int i = 0; i < chunk->constants.count; i++) {
        Value v = chunk->constants.values[i];
        if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_FUNCTION) {
            ObjFunction* fn = AS_FUNCTION(v);
            registerFunction(reg, fn, mod);
            // Recurse into this function's own constants (nested functions)
            preRegisterFunctions(&fn->chunk, reg, mod);
        }
    }
}

// ---------------------------------------------------------------------------
// COMPILE A SINGLE FUNCTION BODY into its pre-declared LLVM function.
// ---------------------------------------------------------------------------
static bool compileFunctionBody(ObjFunction* objFn, FuncEntry* entry,
                                LLVMModuleRef mod, LLVMBuilderRef builder,
                                LLVMValueRef printf_fn, LLVMValueRef fmt_double,
                                GlobalMap* globals, FuncRegistry* funcs,
                                NativeRegistry* natives) {
    LLVMBasicBlockRef entryBlock = LLVMAppendBasicBlock(entry->llvmFn, "entry");
    LLVMPositionBuilderAtEnd(builder, entryBlock);

    TranslateCtx ctx;
    ctx.builder       = builder;
    ctx.fn            = entry->llvmFn;
    ctx.module        = mod;
    ctx.printf_fn     = printf_fn;
    ctx.fmt_double    = fmt_double;
    ctx.globals       = globals;
    ctx.funcs         = funcs;
    ctx.natives       = natives;
    ctx.pendingNative = NULL;
    ctx.isTopLevel    = false;
    stackInit(&ctx.stack);
    localsInit(&ctx.locals);

    // Map parameters into local alloca slots (slot 0 = function itself, skip it)
    for (int p = 0; p < objFn->arity; p++) {
        LLVMValueRef param = LLVMGetParam(entry->llvmFn, p);
        LLVMValueRef alloca = ensureLocal(&ctx.locals, p + 1, builder, entry->llvmFn);
        LLVMBuildStore(builder, param, alloca);
    }

    return translateChunk(&ctx, &objFn->chunk);
}

// ---------------------------------------------------------------------------
// PUBLIC ENTRY POINT
// ---------------------------------------------------------------------------
LLVMBackendResult runWithLLVM(const char* source) {
    initVM();

    ObjFunction* topLevel = compile(source);
    if (topLevel == NULL) { freeVM(); return LLVM_COMPILE_ERROR; }

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    LLVMLinkInMCJIT();

    LLVMModuleRef module = LLVMModuleCreateWithName("asharp_module");

    // Declare printf
    LLVMTypeRef pCharTy = LLVMPointerType(LLVMInt8Type(), 0);
    LLVMTypeRef printfArgTypes[] = { pCharTy };
    LLVMTypeRef printfType = LLVMFunctionType(LLVMInt32Type(), printfArgTypes, 1, 1);
    LLVMValueRef printf_fn = LLVMAddFunction(module, "printf", printfType);

    // Declare native math functions (from C stdlib — JIT finds them at runtime)
    LLVMTypeRef d_d[]  = { LLVMDoubleType() };
    LLVMTypeRef d_dd[] = { LLVMDoubleType(), LLVMDoubleType() };
    LLVMTypeRef sqrt_type  = LLVMFunctionType(LLVMDoubleType(), d_d,  1, 0);
    LLVMTypeRef floor_type = LLVMFunctionType(LLVMDoubleType(), d_d,  1, 0);
    LLVMTypeRef pow_type   = LLVMFunctionType(LLVMDoubleType(), d_dd, 2, 0);
    LLVMValueRef sqrt_fn  = LLVMAddFunction(module, "sqrt",  sqrt_type);
    LLVMValueRef floor_fn = LLVMAddFunction(module, "floor", floor_type);
    LLVMValueRef pow_fn   = LLVMAddFunction(module, "pow",   pow_type);

    // Declare clock() — returns clock_t (integer) but we'll cast to double
    // In A# VM: clockNative() calls C's clock() and divides by CLOCKS_PER_SEC
    // In LLVM: we'll create a wrapper that does the same
    // For simplicity, declare the C clock() directly and handle division in IR
    LLVMTypeRef clock_type = LLVMFunctionType(LLVMInt64Type(), NULL, 0, 0);
    LLVMValueRef clock_fn  = LLVMAddFunction(module, "clock", clock_type);

    // Build native registry — maps A# native names to LLVM function refs
    NativeRegistry natives;
    nativesInit(&natives);
    registerNative(&natives, "sqrt",  sqrt_fn,  sqrt_type);
    registerNative(&natives, "floor", floor_fn, floor_type);
    registerNative(&natives, "pow",   pow_fn,   pow_type);
    registerNative(&natives, "clock", clock_fn, clock_type);

    // Shared state
    GlobalMap globals;  globalsInit(&globals);
    FuncRegistry funcs; funcRegistryInit(&funcs);

    // Pre-register all user-defined functions found in constants
    preRegisterFunctions(&topLevel->chunk, &funcs, module);

    // Create shared builder
    LLVMBuilderRef builder = LLVMCreateBuilder();

    LLVMValueRef fmt_double = NULL; // will be set per-function

    // Create the top-level asharp_main function first so we can create
    // the fmt_double global string pointer (needs a builder position).
    LLVMTypeRef mainFnType = LLVMFunctionType(LLVMVoidType(), NULL, 0, 0);
    LLVMValueRef mainFn = LLVMAddFunction(module, "asharp_main", mainFnType);
    LLVMBasicBlockRef mainEntry = LLVMAppendBasicBlock(mainFn, "entry");
    LLVMPositionBuilderAtEnd(builder, mainEntry);

    fmt_double = LLVMBuildGlobalStringPtr(builder, "%g\n", "fmt_double");

    // Compile all registered user functions
    for (int f = 0; f < funcs.count; f++) {
        FuncEntry* entry = &funcs.entries[f];
        if (!compileFunctionBody(entry->objFn, entry, module, builder,
                                 printf_fn, fmt_double, &globals, &funcs, &natives)) {
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(module);
            freeVM();
            return LLVM_COMPILE_ERROR;
        }
    }

    // Translate the top-level script
    LLVMPositionBuilderAtEnd(builder, mainEntry);

    TranslateCtx mainCtx;
    mainCtx.builder       = builder;
    mainCtx.fn            = mainFn;
    mainCtx.module        = module;
    mainCtx.printf_fn     = printf_fn;
    mainCtx.fmt_double    = fmt_double;
    mainCtx.globals       = &globals;
    mainCtx.funcs         = &funcs;
    mainCtx.natives       = &natives;
    mainCtx.pendingNative = NULL;
    mainCtx.isTopLevel    = true;
    stackInit(&mainCtx.stack);
    localsInit(&mainCtx.locals);

    if (!translateChunk(&mainCtx, &topLevel->chunk)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        freeVM();
        return LLVM_COMPILE_ERROR;
    }

    // Verify
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

    // Uncomment to inspect IR:
    // LLVMDumpModule(module);

    LLVMExecutionEngineRef engine;
    error = NULL;
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error)) {
        fprintf(stderr, "[LLVM Backend] JIT engine failed:\n%s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeBuilder(builder);
        freeVM();
        return LLVM_RUNTIME_ERROR;
    }

    typedef void (*ASharpMainFn)(void);
    ASharpMainFn asharpMain = (ASharpMainFn)(uintptr_t)
        LLVMGetFunctionAddress(engine, "asharp_main");

    if (!asharpMain) {
        fprintf(stderr, "[LLVM Backend] Could not find asharp_main.\n");
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

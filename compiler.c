#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h" 

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

#define UINT8_COUNT (UINT8_MAX +1)

typedef enum {
  PREC_NONE, PREC_ASSIGNMENT, PREC_OR, PREC_AND, PREC_EQUALITY,
  PREC_COMPARISON, PREC_TERM, PREC_FACTOR, PREC_UNARY, PREC_CALL, PREC_PRIMARY
} Precedence;

typedef struct {
  Token name;
  int depth;
}Local;

typedef struct{
  Local locals[UINT8_COUNT];
  int localCount;
  int scopeDepth;
}Compiler;

typedef void (*ParseFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

//FORWARD DECLARATIONS: let compiler know this fn exists
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static Chunk* currentChunk() { return compilingChunk; }

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  
  fprintf(stderr, "[line %d] Error", token->line);
  if (token->type == TOKEN_EOF) fprintf(stderr, " at end");

  else if (token->type != TOKEN_ERROR) fprintf(stderr, " at '%.*s'", token->length, token->start);

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) { errorAt(&parser.previous, message); }
static void errorAtCurrent(const char* message) { errorAt(&parser.current, message); }

//HELPER FUNCTIONS MOVED TO TOP

static void advance() {
  parser.previous = parser.current;
  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;
    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) { advance(); return; }
  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}
static void emitBytes(uint8_t byte1, uint8_t byte2) { emitByte(byte1); emitByte(byte2); }
static void emitReturn() { emitByte(OP_RETURN); }

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }
  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, (uint8_t)addConstant(currentChunk(), value));
}


static uint8_t identifierConstant(Token* name) {
  // Take the string "x" from the source and create a String Object for it
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1; // -1 means "declared but not ready for use yet"
}

static bool identifierEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static void declareVariable() {
  if (current->scopeDepth == 0) return;

  Token* name = &parser.previous;
  
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break; 
    }

    // FIX IS HERE: pass 'name', not '&name'
    if (identifierEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable(); //Track locals
  if (current->scopeDepth > 0) return 0; // Return dummy 0 for locals

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  if (current->scopeDepth == 0) return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized(); // <--- NEW
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static void varDeclaration() {
  // 1. Parse the variable name ("x")
  uint8_t global = parseVariable("Expect variable name.");

  // 2. Parse the initializer ("= 10")
  if (match(TOKEN_EQUAL)) {
    expression(); // Compiles "10"
  } else {
    // Handling "var x;" (no value) -> default to nil
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  // 3. Emit the instruction to save it
  defineVariable(global);
}


static void declaration() {
  if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }
}
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// --- GRAMMAR ---

static void number() {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string() {
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void grouping() { expression(); consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression."); }

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifierEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1; // Not found in locals
}

static void namedVariable(Token name) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);

  if (arg != -1) {
    // It is a local variable
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else {
    // It IS NOT local, assume Global
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

static void variable() {
  namedVariable(parser.previous);
}

static void unary() {
  TokenType operatorType = parser.previous.type;
  parsePrecedence(PREC_UNARY);
  switch (operatorType) {
    case TOKEN_BANG:  emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default: return;
  }
}

static void binary() {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));
  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;

    default : return;
  }
}

static void literal() {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL:   emitByte(OP_NIL); break;
    case TOKEN_TRUE:  emitByte(OP_TRUE); break;
    default: return;
  }
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER] =    {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) { error("Expect expression."); return; }
  prefixRule();
  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule();
  }
}

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

//STATEMENTS

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

//Depth of Scope
static void beginScope() {
  current->scopeDepth++;
}

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    emitByte(OP_POP);
    current->localCount--; // Forget it in the compiler
  }
}

//How compiler will handle { and }
static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } 
  
  else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } 
  
  else {
    expressionStatement();
  }
}


//Compile Entry Point

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  
  // Set up the compiler state
  Compiler compiler;
  compiler.localCount = 0;
  compiler.scopeDepth = 0;

  current = &compiler;
  
  compilingChunk = chunk;
  parser.hadError = false;
  parser.panicMode = false;
  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  emitReturn();
  return !parser.hadError;
}
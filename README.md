# A-Sharp Programming Language

A bytecode virtual machine and interpreter for the A-Sharp programming language, written in C. This implementation follows the architecture described in Part III of **Crafting Interpreters** by Robert Nystrom.

**Project Status:** Active development — Core features including control flow, scoping, and basic data types are fully functional.

---

## Overview

A-Sharp is a dynamically-typed scripting language with a stack-based bytecode VM. It features:

- **Clean syntax** for variables, expressions, and control flow
- **Lexical scoping** with local and global variables
- **Interactive REPL** with line editing and command history
- **Efficient bytecode execution** via a custom virtual machine

---

## Features

### Implemented
- **Lexical Analysis**: Tokenizes source code into a stream of tokens
- **Compilation**: Pratt parser with single-pass bytecode generation
- **Virtual Machine**: Stack-based bytecode interpreter with optimized dispatch
- **Data Types**: Numbers, strings, booleans, and nil
- **Variables**: Local and global variables with lexical scoping
- **Operators**: Arithmetic (`+`, `-`, `*`, `/`), comparison (`<`, `>`, `<=`, `>=`), equality (`==`, `!=`), logical (`and`, `or`, `not`)
- **Control Flow**: 
  - `if`/`else` statements for conditional execution
  - `while` loops for iteration
  - `for` loops with C-style syntax
- **I/O**: `print` statement for output
- **REPL**: Interactive shell with readline support

### Upcoming
- Functions and closures
- Classes and objects
- Standard library
- Module system

---

## Quick Start

### Prerequisites

- C compiler (GCC or Clang)
- GNU Make
- Readline library (for REPL features)

**Installation on Ubuntu/Debian/WSL:**
```bash
sudo apt update
sudo apt install build-essential libreadline-dev
```

**Installation on macOS:**
```bash
brew install readline
# Note: You may need to set CPPFLAGS and LDFLAGS for the compiler
export CPPFLAGS="-I/opt/homebrew/opt/readline/include"
export LDFLAGS="-L/opt/homebrew/opt/readline/lib"
```

### Build

```bash
# Clone the repository
git clone <https://github.com/aadesh006/A-Sharp.git>
cd asharp

# Build the interpreter
make

# Clean build (if needed)
make clean && make
```

The compiled executable `asharp` will be created in the project root.

---

## Usage

### Interactive REPL

Start the interactive shell:
```bash
./asharp
```

Try some expressions:
```javascript
> print 1 + 2 * 3;
7
> var name = "A-Sharp";
> print "Hello from " + name;
Hello from A-Sharp
```

### Running Scripts

Execute a source file (`.as` extension):
```bash
./asharp script.as
```

### Example Programs

**Basic Arithmetic:**
```javascript
print 1 + 2 * (3 - 4);  // -1
```

**Variables and Scoping:**
```javascript
var a = "Programming with ";
var b = "A-Sharp";

{
  // Local scope
  var a = 15;
  var b = 15;
  print a + b;  // 30
}

print a + b;  // Programming with A-Sharp
```

**Conditional Statements:**
```javascript
var age = 20;

if (age >= 18) {
  print "Adult";
} else {
  print "Minor";
}
```

**While Loops:**
```javascript
var i = 0;
while (i < 5) {
  print i;
  i = i + 1;
}
```

**For Loops:**
```javascript
for (var i = 0; i < 10; i = i + 1) {
  print i * i;
}
```

**Fibonacci Generator:**
```javascript
var a = 0;
var b = 1;

for (var i = 0; i < 10; i = i + 1) {
  print a;
  var temp = a + b;
  a = b;
  b = temp;
}
```

More examples can be found in `test.as`.

---

## Project Structure

```
asharp/
├── main.c              # Entry point and CLI interface
├── scanner.{c,h}       # Lexical analyzer (tokenizer)
├── compiler.{c,h}      # Parser and bytecode compiler
├── vm.{c,h}            # Virtual machine and bytecode interpreter
├── chunk.{c,h}         # Bytecode chunk data structure
├── value.{c,h}         # Runtime value representation
├── object.{c,h}        # Heap-allocated objects (strings, functions)
├── memory.{c,h}        # Memory management and garbage collection helpers
├── table.{c,h}         # Hash table for global variables
├── debug.{c,h}         # Bytecode disassembler and debugging utilities
├── Makefile            # Build configuration
├── test.as             # Sample programs
└── build/              # Build artifacts (generated)
```

### Architecture Overview

1. **Scanner** (`scanner.c/h`) — Converts source text into tokens
2. **Compiler** (`compiler.c/h`) — Parses tokens using Pratt parsing and emits bytecode
3. **Chunk** (`chunk.c/h`) — Stores bytecode instructions and constants
4. **VM** (`vm.c/h`) — Executes bytecode on a stack-based virtual machine
5. **Object** (`object.c/h`) — Manages heap-allocated runtime objects
6. **Memory** (`memory.c/h`) — Handles dynamic allocation and garbage collection

---

## Changelog

| Date       | Update                                                    |
|------------|-----------------------------------------------------------|
| 2026-01-07 | Added `if`/`else` statement support                       |
| 2026-01-08 | Implemented `while` loops                                 |
| 2026-01-10 | Added `for` loop statement                                |
| 2026-01-11 | Created `test.as` with example programs                   |

---

## Development

### Developer Notes

**Architecture Principles:**
- The compiler is a **single-pass compiler** that emits bytecode directly without building an AST. This makes compilation fast but means some optimizations aren't possible.
- The VM uses a **stack-based architecture** rather than register-based. All operations push/pop values from the stack.
- **Hash tables** (`table.c/h`) are used for global variable lookup with string interning for efficiency.
- Memory management currently uses manual allocation; a garbage collector is planned for future releases.

**Code Organization:**
- Each module has a clear separation between interface (`.h`) and implementation (`.c`).
- The `Chunk` structure holds both bytecode instructions and a constant pool for literals.
- String objects are heap-allocated and interned for efficient comparison and memory usage.
- The compiler uses **Pratt parsing** (precedence climbing) for expression parsing, which elegantly handles operator precedence.

**Important Implementation Details:**
- Local variables are resolved at compile-time and accessed by stack offset (no runtime lookup).
- Global variables use runtime hash table lookup by name.
- Control flow uses bytecode jump instructions with backpatching for forward jumps.
- The REPL runs in the same VM instance, maintaining global state between statements.

**Performance Considerations:**
- Bytecode is designed to be compact; most instructions are single-byte opcodes.
- The VM's instruction dispatch uses a computed goto (if supported) or switch statement for fast execution.
- Constants are stored in a separate array and referenced by index to keep bytecode small.

### Adding New Features

The codebase structure:

1. **Add tokens** in `scanner.c` if new keywords are needed
2. **Extend the parser** in `compiler.c` with parsing rules
3. **Emit bytecode** for the new feature
4. **Handle execution** in the VM (`vm.c`)

**Example workflow for adding a new operator:**
```c
// 1. Add token type in scanner.h
TOKEN_MODULO,

// 2. Add to scanner.c switch statement
case '%': addToken(TOKEN_MODULO); break;

// 3. Add parse rule in compiler.c
[TOKEN_MODULO] = {NULL, binary, PREC_FACTOR},

// 4. Handle in binary() function to emit OP_MODULO

// 5. Add VM case in vm.c
case OP_MODULO: BINARY_OP(%, NUMBER_VAL); break;
```

### Debugging

Use the built-in disassembler to inspect generated bytecode:

```c
#include "debug.h"

disassembleChunk(&chunk, "my chunk");
```

Compile with debug flags:
```bash
make CFLAGS="-g -O0"
```

**Debugging Tips:**
- Enable trace execution in `vm.c` by uncommacing `#define DEBUG_TRACE_EXECUTION` to see each instruction as it executes.
- Use `#define DEBUG_PRINT_CODE` in `compiler.c` to automatically disassemble compiled chunks.
- The debugger prints the stack state before each instruction when tracing is enabled.

### Testing

Run the test suite:
```bash
./asharp test.as
```

---

## Contributing

Contributions are welcome! Areas for improvement:

- Function declarations and calls
- First-class functions and closures
- Object-oriented features (classes, inheritance)
- Standard library functions
- Error messages and diagnostics
- Performance optimizations

Please open an issue to discuss major changes before submitting a pull request.

---

## Author
*Aadesh Chaudhari\
@aadesh006*
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct{
    const char* start;
    const char* current;
    int line;
}Scanner;

Scanner scanner;

void initScanner(const char* source){
    scanner.start = source;
    scanner.current = source;
    scanner.line =1;
}

static bool isAtEnd(){
    return *scanner.current == '\0';
}

static char advance(){
    scanner.current++;
    return scanner.current[-1];
}

static char peek (){
    return *scanner.current;
}

static char peekNext(){
    if(isAtEnd) return '\0';
    return scanner.current[1];
}

static Token makeToken(TokenType type){
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

// Create an error token to tell the compiler something went wrong
static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}

//skip the whitespaces and comments
static void skipWhitespace(){
    for(;;){
        char c= peek();
        switch (c){
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if(peekNext() == '/'){
                    while(peek()!='\n'&& !isAtEnd()) advance();
                } else{
                    return;
                }
                break;
            default:
                return;

        }
    }
}

static Token number(){
    while(peek() >= '0' && peek()<='9') advance();

    if (peek() == '.' && (peekNext() >= '0' && peekNext() <= '9')){
        advance();
        while(peek() >= '0' && peek() <= '9') advance();
    }
    return makeToken(TOKEN_NUMBER);
}

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;
  scanner.current++;
  return true;
}

//The Main Scan Function

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type){
    if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0) {
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static TokenType identifierType(){
    switch (scanner.start[0]){
    case 'f': return checkKeyword(1,4,"alse", TOKEN_FALSE);
    case 'n': return checkKeyword(1,2,"il", TOKEN_NIL);
    case 't': return checkKeyword(1,3,"rue", TOKEN_TRUE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier() {
  while ((peek() >= 'a' && peek() <= 'z') || 
         (peek() >= 'A' && peek() <= 'Z') || 
          peek() == '_') {
    advance();
  }
  return makeToken(identifierType());
}

static Token string(){
    while (peek() != '"' && !isAtEnd()){
        if (peek()=='\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated String.");

    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken(){
    skipWhitespace();
    scanner.start = scanner.current;

    if(isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();

    //check for letters
    if((c>='a'&&c<='z') || (c>='A'&&c<='Z')||c=='_'){
        return identifier();
    }

    //Check for Numbers;
    if(c>= '0' && c<='9') return number();



    switch(c){
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);

        case '"': return string();

        // Two-character operators
        case '!':
        return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
        return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
        return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
        return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    }
    return errorToken("Unexpected Character.");
}
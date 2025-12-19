#include <stdio.h>
#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm;

void initVM(){
    vm.stackTop = vm.stack;
}

void freeVM(){

}

void push(Value value){
    *vm.stackTop = value; //put the value in empty slot
    vm.stackTop++; //Move the pointer up
}//Not checking for overflow yet!

void pop(){
    vm.stackTop--;
    return *vm.stackTop;
}

//The Engine Loop
static InterpretResult run(){
    //macro: Read the byte currently pointed at by ip, then advance ip
    #define READ_BYTE() (*vm.ip++)
    //macro: Read the next, use it as an index and look up the constant
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    for(;;){
        
    }
}
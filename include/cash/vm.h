#ifndef CASH_VM_H_
#define CASH_VM_H_

#include <cash/ast.h>

void run_program(struct Program* program);
void run_command(struct Command* command);

#endif

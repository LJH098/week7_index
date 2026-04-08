#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "hard_parser.h"

/*
 * Execute one parsed SQL statement.
 * Returns SUCCESS on success, FAILURE on error.
 */
int executor_execute(const SqlStatement *statement);

#endif

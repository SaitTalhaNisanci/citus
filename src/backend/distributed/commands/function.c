/*-------------------------------------------------------------------------
 *
 * function.c
 *    Commands for FUNCTION statements.
 *    The following functions will be supported in Citus:
 *      - this
 *      - that
 *      - TODO: create a list here maybe
 *
 * Copyright (c) 2019, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "distributed/commands.h"
#include "distributed/deparser.h"


List *
PlanAlterFunctionStmt(AlterFunctionStmt *alterFunctionStmt,
					  const char *querystring)
{
	const char *alterStmtSql = NULL;

	alterStmtSql = deparse_alter_function_stmt(alterFunctionStmt);
	ereport(LOG, (errmsg("deparsed alter function statement"),
				  errdetail("sql: %s", alterStmtSql)));

	return NIL;
}


List *
PlanDropFunctionStmt(DropStmt *dropStmt,
					 const char *queryString)
{
	const char *dropStmtSql = NULL;

	dropStmtSql = deparse_drop_function_stmt(dropStmt);
	ereport(LOG, (errmsg("deparsed drop function statement"),
				  errdetail("sql: %s", dropStmtSql)));

	return NIL;
}

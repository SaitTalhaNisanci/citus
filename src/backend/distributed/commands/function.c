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

#include <server/nodes/parsenodes.h>
#include <server/lib/stringinfo.h>

static const char * deparse_drop_function_stmt(DropStmt *stmt);
static void appendDropFunctionStmt(StringInfo buf, DropStmt *stmt);
static void appendFunctionNameList(StringInfo buf, List *objects);


List *
PlanAlterFunctionStmt(AlterFunctionStmt *alterFunctionStatement,
					  const char *alterFunctionCommand)
{
	ereport(DEBUG4, ((errmsg("ALTER FUNC distribution not implemented yet"),
					  errhint("check function.c for more info"))));

	return NIL;
}


List *
PlanDropFunctionStmt(DropStmt *dropFunctionStatement,
					 const char *dropFunctionCommand)
{
	ereport(DEBUG4, ((errmsg("DROP FUNC distribution not implemented yet"),
					  errhint("check function.c for more info"))));

	return NIL;
}


static const char * format_function_be_qualified(Oid oid);

static const char *
deparse_drop_function_stmt(DropStmt *stmt)
{
	StringInfoData str = { 0 };
	initStringInfo(&str);

	Assert(stmt->removeType == OBJECT_FUNCTION);

	appendDropFunctionStmt(&str, stmt);

	return str.data;
}


static void
appendDropFunctionStmt(StringInfo buf, DropStmt *stmt)
{
	/*
	 * TODO: check that this comment is still valid. (I copy pasted)
	 * already tested at call site, but for future it might be collapsed in a
	 * deparse_function_stmt so be safe and check again
	 */
	Assert(stmt->removeType == OBJECT_FUNCTION);

	appendStringInfo(buf, "DROP FUNCTION ");
	appendFunctionNameList(buf, stmt->objects);

	/*
	 * TODO: check for optional argmode argname argtype options here
	 */

	if (stmt->behavior == DROP_CASCADE)
	{
		appendStringInfoString(buf, " CASCADE");
	}
	appendStringInfoString(buf, ";");
}


static void
appendFunctionNameList(StringInfo buf, List *objects)
{
	ListCell *objectCell = NULL;
	foreach(objectCell, objects)
	{
		/*
		 * TODO: figure out how to iterate over all functions and get their
		 * names etc here
		 */
	}
}


static const char *
format_function_be_qualified(Oid oid)
{
	return NULL;
}

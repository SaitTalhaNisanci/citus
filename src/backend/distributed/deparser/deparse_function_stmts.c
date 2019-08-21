/*-------------------------------------------------------------------------
 *
 * deparse_function_stmts.c
 *
 * Copyright (c) 2019, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/namespace.h"
#include "distributed/deparser.h"
#include "lib/stringinfo.h"
#include "nodes/makefuncs.h"
#include "parser/parse_type.h"

/* forward declaration for deparse functions */
static void appendAlterFunctionStmt(StringInfo buf, AlterFunctionStmt *stmt);
static void appendDropFunctionStmt(StringInfo buf, DropStmt *stmt);
static void appendFunctionNameList(StringInfo buf, List *objects);
static void appendFunctionName(StringInfo buf, ObjectWithArgs *func);

const char *
deparse_alter_function_stmt(AlterFunctionStmt *stmt)
{
	StringInfoData str = { 0 };
	initStringInfo(&str);

	appendAlterFunctionStmt(&str, stmt);

	return str.data;
}


const char *
deparse_drop_function_stmt(DropStmt *stmt)
{
	StringInfoData str = { 0 };
	initStringInfo(&str);

	Assert(stmt->removeType == OBJECT_FUNCTION);

	appendDropFunctionStmt(&str, stmt);

	return str.data;
}


static void
appendAlterFunctionStmt(StringInfo buf, AlterFunctionStmt *stmt)
{
	appendStringInfo(buf, "ALTER FUNCTION ");
	appendFunctionName(buf, stmt->func);

	/* TODO: use other attributes to deparse the query here  */

	appendStringInfoString(buf, ";");
}


static void
appendDropFunctionStmt(StringInfo buf, DropStmt *stmt)
{
	/*
	 * TODO: Hanefi you should check that this comment is still valid.
	 *
	 * already tested at call site, but for future it might be collapsed in a
	 * deparse_function_stmt so be safe and check again
	 */
	Assert(stmt->removeType == OBJECT_FUNCTION);

	appendStringInfo(buf, "DROP FUNCTION ");
	if (stmt->missing_ok)
	{
		appendStringInfoString(buf, "IF EXISTS ");
	}
	appendFunctionNameList(buf, stmt->objects);
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
		Node *object = lfirst(objectCell);
		ObjectWithArgs *func = NULL;

		if (objectCell != list_head(objects))
		{
			appendStringInfo(buf, ", ");
		}

		Assert(IsA(object, ObjectWithArgs));
		func = castNode(ObjectWithArgs, object);

		appendFunctionName(buf, func);
	}
}


static void
appendFunctionName(StringInfo buf, ObjectWithArgs *func)
{
	char *name = NameListToString(func->objname);
	char *args = TypeNameListToString(func->objargs);

	appendStringInfoString(buf, name);

	/* append the optional arg list if provided */
	if (args)
	{
		appendStringInfo(buf, "(%s)", args);
	}
}

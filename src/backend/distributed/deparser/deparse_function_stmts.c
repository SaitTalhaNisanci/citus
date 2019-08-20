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
static void appendDropFunctionStmt(StringInfo buf, DropStmt *stmt);
static void appendFunctionNameList(StringInfo buf, List *objects);

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
		ObjectWithArgs *objectWithArgs = NULL;
		char *name = NULL;
		char *args = NULL;

		if (objectCell != list_head(objects))
		{
			appendStringInfo(buf, ", ");
		}

		/*
		 * TODO: Check that this assertion is correct
		 *
		 * I made a blind guess and assumed this object is always an instance
		 * of ObjectWithArgs.
		 */
		Assert(IsA(object, ObjectWithArgs));

		objectWithArgs = castNode(ObjectWithArgs, object);
		name = NameListToString(objectWithArgs->objname);
		args = TypeNameListToString(objectWithArgs->objargs);

		appendStringInfoString(buf, name);

		/* append the optional arg list if provided */
		if (args)
		{
			appendStringInfo(buf, "(%s)", args);
		}
	}
}

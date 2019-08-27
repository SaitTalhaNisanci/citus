/*-------------------------------------------------------------------------
 *
 * deparse_function_stmts.c
 *
 * Copyright (c) 2019, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/namespace.h"
#include "catalog/pg_proc.h"
#include "distributed/commands.h"
#include "distributed/deparser.h"
#include "lib/stringinfo.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/value.h"
#include "parser/parse_func.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

/* forward declaration for deparse functions */
static void appendAlterFunctionStmt(StringInfo buf, AlterFunctionStmt *stmt);
static void appendDropFunctionStmt(StringInfo buf, DropStmt *stmt);
static void appendFunctionNameList(StringInfo buf, List *objects);
static void appendFunctionName(StringInfo buf, ObjectWithArgs *func);

static void appendDefElem(StringInfo buf, DefElem *def);
static void appendDefElemStrict(StringInfo buf, DefElem *def);
static void appendDefElemVolatility(StringInfo buf, DefElem *def);
static void appendDefElemLeakproof(StringInfo buf, DefElem *def);
static void appendDefElemSecurity(StringInfo buf, DefElem *def);
static void appendDefElemParallel(StringInfo buf, DefElem *def);
static void appendDefElemCost(StringInfo buf, DefElem *def);
static void appendDefElemRows(StringInfo buf, DefElem *def);
static void appendDefElemSet(StringInfo buf, DefElem *def);

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
	ListCell *actionCell = NULL;
	appendStringInfo(buf, "ALTER FUNCTION ");
	appendFunctionName(buf, stmt->func);

	foreach(actionCell, stmt->actions)
	{
		DefElem *def = castNode(DefElem, lfirst(actionCell));
		ereport(LOG, (errmsg("defElem found"),
					  errdetail("name: %s", def->defname)));

		appendDefElem(buf, def);
	}

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


static char *
qualified_function_name(ObjectWithArgs *func)
{
	Oid funcid = InvalidOid;
	HeapTuple proctup;
	Form_pg_proc procform;
	char *proname = NULL;
	char *nspname = NULL;
	char *result = NULL;

	funcid = LookupFuncWithArgs(OBJECT_FUNCTION, func, false);
	proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcid));
	if (!HeapTupleIsValid(proctup))
	{
		elog(ERROR, "cache lookup failed for function %u", funcid);
	}

	procform = (Form_pg_proc) GETSTRUCT(proctup);
	proname = NameStr(procform->proname);
	nspname = get_namespace_name(procform->pronamespace);
	result = quote_qualified_identifier(nspname, proname);

	return result;
}


static void
appendFunctionName(StringInfo buf, ObjectWithArgs *func)
{
	char *qualifiedFuncName = qualified_function_name(func);
	char *args = TypeNameListToString(func->objargs);

	appendStringInfoString(buf, qualifiedFuncName);

	/* append the optional arg list if provided */
	if (args)
	{
		appendStringInfo(buf, "(%s)", args);
	}
}


static void
appendDefElem(StringInfo buf, DefElem *def)
{
	if (strcmp(def->defname, "strict") == 0)
	{
		appendDefElemStrict(buf, def);
	}
	else if (strcmp(def->defname, "volatility") == 0)
	{
		appendDefElemVolatility(buf, def);
	}
	else if (strcmp(def->defname, "leakproof") == 0)
	{
		appendDefElemLeakproof(buf, def);
	}
	else if (strcmp(def->defname, "security") == 0)
	{
		appendDefElemSecurity(buf, def);
	}
	else if (strcmp(def->defname, "parallel") == 0)
	{
		appendDefElemParallel(buf, def);
	}
	else if (strcmp(def->defname, "cost") == 0)
	{
		appendDefElemCost(buf, def);
	}
	else if (strcmp(def->defname, "rows") == 0)
	{
		appendDefElemRows(buf, def);
	}
	else if (strcmp(def->defname, "set") == 0)
	{
		appendDefElemSet(buf, def);
	}
}


static void
appendDefElemStrict(StringInfo buf, DefElem *def)
{
	if (intVal(def->arg) == 1)
	{
		appendStringInfo(buf, " STRICT");
	}
	else
	{
		appendStringInfo(buf, " CALLED ON NULL INPUT");
	}
}


static void
appendDefElemVolatility(StringInfo buf, DefElem *def)
{
	appendStringInfo(buf, " %s", strVal(def->arg));
}


static void
appendDefElemLeakproof(StringInfo buf, DefElem *def)
{
	if (intVal(def->arg) == 0)
	{
		appendStringInfo(buf, " NOT");
	}
	appendStringInfo(buf, " LEAKPROOF");
}


static void
appendDefElemSecurity(StringInfo buf, DefElem *def)
{
	if (intVal(def->arg) == 0)
	{
		appendStringInfo(buf, " SECURITY INVOKER");
	}
	else
	{
		appendStringInfo(buf, " SECURITY DEFINER");
	}
}


static void
appendDefElemParallel(StringInfo buf, DefElem *def)
{
	appendStringInfo(buf, " PARALLEL %s", strVal(def->arg));
}


static void
appendDefElemCost(StringInfo buf, DefElem *def)
{
	appendStringInfo(buf, " COST %s", strVal(def->arg));
}


static void
appendDefElemRows(StringInfo buf, DefElem *def)
{
	appendStringInfo(buf, " ROWS %s", strVal(def->arg));
}


static void
appendDefElemSet(StringInfo buf, DefElem *def)
{
	VariableSetStmt *setStmt = castNode(VariableSetStmt, def->arg);
	char *setVariableArgs = ExtractSetVariableArgs(setStmt);

	switch (setStmt->kind)
	{
		case VAR_SET_VALUE:
		{
			appendStringInfo(buf, " SET %s = %s", setStmt->name, setVariableArgs);
			break;
		}

		case VAR_SET_CURRENT:
		{
			appendStringInfo(buf, " SET %s FROM CURRENT", setStmt->name);
			break;
		}

		case VAR_SET_DEFAULT:
		{
			appendStringInfo(buf, " SET %s TO DEFAULT", setStmt->name);
			break;
		}

		case VAR_RESET:
		{
			appendStringInfo(buf, " RESET %s", setStmt->name);
			break;
		}

		case VAR_RESET_ALL:
		{
			appendStringInfoString(buf, " RESET ALL");
			break;
		}

		/* VAR_SET_MULTI is a special case for SET TRANSACTION that should not occur here */
		case VAR_SET_MULTI:
		default:
		{
			/* TODO improve this error message */
			ereport(ERROR, (errmsg("Unable to deparse SET statement")));
			break;
		}
	}
}

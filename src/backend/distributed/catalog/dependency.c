/*-------------------------------------------------------------------------
 *
 * dependency.c
 *	  Functions to reason about distributed objects and their dependencies
 *
 * Copyright (c) 2019, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/skey.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_depend.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"

#include "distributed/dist_catalog/dependency.h"
#include "distributed/dist_catalog/distobject.h"


static bool SupportedDependencyByCitus(const ObjectAddress *toFollow);
static bool IsObjectAddressInList(const ObjectAddress *findAddress, List *addressList);
static bool IsObjectAddressOwnedByExtension(const ObjectAddress *target);

static void recurse_pg_depend(const ObjectAddress *target,
							  bool (*follow)(void *context, const Form_pg_depend row),
							  void (*apply)(void *context, const Form_pg_depend row),
							  void *context);
static bool follow_order_object_address(void *context, const Form_pg_depend pg_depend);
static bool follow_get_dependencies_for_object(void *context, const Form_pg_depend
											   pg_depend);
static void apply_add_to_target_list(void *context, const Form_pg_depend pg_depend);


/*
 * GetDependenciesForObject returns a list of ObjectAddesses to be created in order
 * before the target object could safely be created on a worker. Some of the object might
 * already be created on a worker. It should be created in an idempotent way.
 */
void
GetDependenciesForObject(const ObjectAddress *target, List **dependencyList)
{
	recurse_pg_depend(target,
					  &follow_get_dependencies_for_object,
					  &apply_add_to_target_list,
					  dependencyList);
}


/*
 * IsObjectAddressInList is a helper function that can check if an ObjectAddress is
 * already in a (unsorted) list of ObjectAddress'.
 */
static bool
IsObjectAddressInList(const ObjectAddress *findAddress, List *addressList)
{
	ListCell *addressCell = NULL;
	foreach(addressCell, addressList)
	{
		ObjectAddress *currentAddress = (ObjectAddress *) lfirst(addressCell);

		/* equality check according as used in postgres for object_address_present */
		if (findAddress->classId == currentAddress->classId && findAddress->objectId ==
			currentAddress->objectId)
		{
			if (findAddress->objectSubId == currentAddress->objectSubId ||
				currentAddress->objectSubId == 0)
			{
				return true;
			}
		}
	}

	return false;
}


/*
 * ShouldFollowDependency applies a couple of rules to test if we need to traverse the
 * dependencies of this objects.
 *
 * Most important check; we nee to support the object for distribution, otherwise we do
 * not need to traverse it and its dependencies. For simplicity in the implementation we
 * actually check this as the last check.
 *
 * Other checks we do:
 *  - do not follow if the object is owned by an extension.
 *    As extensions are created separately on each worker we do not need to take into
 *    account objects that are the result of CREATE EXTENSION.
 *
 *  - do not follow objects that citus has already distributed.
 *    Objects only need to be distributed once. If citus has already distributed the
 *    object we do not follow its dependencies.
 */
static bool
SupportedDependencyByCitus(const ObjectAddress *toFollow)
{
	/*
	 * looking at the type of a object to see if we should follow this dependency to
	 * create on the workers.
	 */
	switch (getObjectClass(toFollow))
	{
		case OCLASS_SCHEMA:
		{
			/* always follow */
			return true;
		}

		default:
		{
			/* unsupported type */
			return false;
		}
	}

	/*
	 * all types should have returned above, compilers complaining about a non return path
	 * indicate a bug int the above switch. Fix it there instead of adding a return here.
	 */
}


/*
 * IsObjectAddressOwnedByExtension returns whether or not the object is owned by an
 * extension. It is assumed that an object having a dependency on an extension is created
 * by that extension and therefore owned by that extension.
 */
static bool
IsObjectAddressOwnedByExtension(const ObjectAddress *target)
{
	Relation depRel = NULL;
	ScanKeyData key[2] = { 0 };
	SysScanDesc depScan = NULL;
	HeapTuple depTup = NULL;
	bool result = false;

	depRel = heap_open(DependRelationId, AccessShareLock);

	/* scan pg_depend for classid = $1 AND objid = $2 using pg_depend_depender_index */
	ScanKeyInit(&key[0], Anum_pg_depend_classid, BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(target->classId));
	ScanKeyInit(&key[1], Anum_pg_depend_objid, BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(target->objectId));
	depScan = systable_beginscan(depRel, DependDependerIndexId, true, NULL, 2, key);

	while (HeapTupleIsValid(depTup = systable_getnext(depScan)))
	{
		Form_pg_depend pg_depend = (Form_pg_depend) GETSTRUCT(depTup);
		if (pg_depend->deptype == DEPENDENCY_EXTENSION)
		{
			result = true;
			break;
		}
	}

	systable_endscan(depScan);
	relation_close(depRel, AccessShareLock);

	return result;
}


/*
 * OrderObjectAddressListInDependencyOrder given a list of ObjectAddress' return a new
 * list of the same ObjectAddress'.
 *
 * The algortihm traveses pg_depend in a depth first order starting at the first object in
 * the provided list. By traversing depth first it will create to first dependency first
 * and subsequent dependencies later.
 *
 * If the object is already in the list it is skipped for traversal. This happens when an
 * object was already added to the target list before it occurred in the input list.
 */
List *
OrderObjectAddressListInDependencyOrder(List *objectAddressList)
{
	List *targetList = NIL;
	ListCell *ojectAddressCell = NULL;
	foreach(ojectAddressCell, objectAddressList)
	{
		ObjectAddress *objectAddress = (ObjectAddress *) lfirst(ojectAddressCell);
		if (IsObjectAddressInList(objectAddress, targetList))
		{
			/* skip objects that are already ordered */
			continue;
		}

		recurse_pg_depend(objectAddress,
						  &follow_order_object_address,
						  &apply_add_to_target_list,
						  &targetList);

		/* all dependencies are added before, now add the current object */
		targetList = lappend(targetList, objectAddress);
	}

	return targetList;
}


/*
 * recurse_pg_depend recursively visits pg_depend entries, starting at the target
 * ObjectAddress. For every entry the follow function will be called. When follow returns
 * true it will recursively visit the dependencies of that object. recurse_pg_depend will
 * visit therefore all pg_depend entries.
 *
 * Visiting will happen in depth first order, which is useful to create or sort lists of
 * dependencies to create.
 *
 * For all pg_depend entries that should be visit the apply function will be called. This
 * function is designed to be the mutating function for the context being passed. Although
 * nothing prevents the follow functions to also mutate the context.
 *
 *  - follow will be called on the way down, so the invocation order is top to bottom of
 *    the dependency tree
 *  - allow is called on the way back, so the invocation order is bottom to top. Allow is
 *    not called for entries for which follow has returned false.
 */
static void
recurse_pg_depend(const ObjectAddress *target,
				  bool (*follow)(void *context, const Form_pg_depend row),
				  void (*apply)(void *context, const Form_pg_depend row),
				  void *context)
{
	Relation depRel = NULL;
	ScanKeyData key[2] = { 0 };
	SysScanDesc depScan = NULL;
	HeapTuple depTup = NULL;

	depRel = heap_open(DependRelationId, AccessShareLock);

	/* scan pg_depend for classid = $1 AND objid = $2 using pg_depend_depender_index */
	ScanKeyInit(&key[0], Anum_pg_depend_classid, BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(target->classId));
	ScanKeyInit(&key[1], Anum_pg_depend_objid, BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(target->objectId));
	depScan = systable_beginscan(depRel, DependDependerIndexId, true, NULL, 2, key);

	while (HeapTupleIsValid(depTup = systable_getnext(depScan)))
	{
		Form_pg_depend pg_depend = (Form_pg_depend) GETSTRUCT(depTup);
		ObjectAddress address = { 0 };
		ObjectAddressSet(address, pg_depend->refclassid, pg_depend->refobjid);

		if (follow == NULL || !follow(context, pg_depend))
		{
			/* skip all pg_depend entries the user didn't want to follow */
			continue;
		}

		/*
		 * recurse depth first, this makes sure we call apply for the deepest dependency
		 * first.
		 */
		recurse_pg_depend(&address, follow, apply, context);

		/* now apply changes for current entry */
		if (apply != NULL)
		{
			apply(context, pg_depend);
		}
	}

	systable_endscan(depScan);
	relation_close(depRel, AccessShareLock);
}


/*
 * follow_get_dependencies_for_object applies filters on pg_depend entries to follow all
 * objects which should be distributed before the root object can safely be created.
 *
 * Objects are only followed if all of the following checks hold true:
 *  - the pg_depend entry is a normal dependency, all other types are created and
 *    maintained by postgres
 *  - the object is not already in the targetList (context), if it is already in there an
 *    other object already caused the creation of this object
 *  - the object is not already marked as distributed in pg_dist_object. Objects in
 *    pg_dist_object are already available on all workers.
 *  - object is not created by an other extension. Objects created by extensions are
 *    assumed to be created on the worker when the extension is created there.
 *  - citus supports object distribution. If citus does not support the distribution of
 *    the object we will not try and distribute it.
 */
static bool
follow_get_dependencies_for_object(void *context, const Form_pg_depend pg_depend)
{
	List **targetList = (List **) context;
	ObjectAddress address = { 0 };
	ObjectAddressSet(address, pg_depend->refclassid, pg_depend->refobjid);

	if (pg_depend->deptype != DEPENDENCY_NORMAL)
	{
		return false;
	}

	if (IsObjectAddressInList(&address, *targetList))
	{
		return false;
	}

	if (isObjectDistributed(&address))
	{
		return false;
	}

	if (IsObjectAddressOwnedByExtension(&address))
	{
		return false;
	}

	if (!SupportedDependencyByCitus(&address))
	{
		return false;
	}

	return true;
}


/*
 * follow_order_object_address applies filters on pg_depend entries to follow the
 * dependency tree of objects in depth first order. The filters are practically the same
 * to follow_get_dependencies_for_object, except it will follow objects that have already
 * been distributed. This is because its primary use is order the objects from
 * pg_dist_object in dependency order.
 *
 * For completeness the list of all edges it will follow;
 *
 * Objects are only followed if all of the following checks hold true:
 *  - the pg_depend entry is a normal dependency, all other types are created and
 *    maintained by postgres
 *  - the object is not already in the targetList (context), if it is already in there an
 *    other object already caused the creation of this object
 *  - object is not created by an other extension. Objects created by extensions are
 *    assumed to be created on the worker when the extension is created there.
 *  - citus supports object distribution. If citus does not support the distribution of
 *    the object we will not try and distribute it.
 */
static bool
follow_order_object_address(void *context, const Form_pg_depend pg_depend)
{
	List **targetList = (List **) context;
	ObjectAddress address = { 0 };
	ObjectAddressSet(address, pg_depend->refclassid, pg_depend->refobjid);

	if (pg_depend->deptype != DEPENDENCY_NORMAL)
	{
		return false;
	}

	if (IsObjectAddressInList(&address, *targetList))
	{
		return false;
	}

	if (IsObjectAddressOwnedByExtension(&address))
	{
		return false;
	}

	if (!SupportedDependencyByCitus(&address))
	{
		return false;
	}

	return true;
}


/*
 * apply_add_to_target_list is an apply function for recurse_pg_depend that will append
 * all the ObjectAddresses for pg_depend entries to the context. The context here is
 * assumed to be a (List **) to the location where all ObjectAddresses will be collected.
 */
static void
apply_add_to_target_list(void *context, const Form_pg_depend pg_depend)
{
	List **targetList = (List **) context;
	ObjectAddress *address = palloc0(sizeof(ObjectAddress));
	ObjectAddressSet(*address, pg_depend->refclassid, pg_depend->refobjid);

	*targetList = lappend(*targetList, address);
}
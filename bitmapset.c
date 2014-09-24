/*************************************
 * Author: Pavel Stehule @2010
 *
 * if you like this sw, then send postcard from your home on address
 *
 *    Pavel Stehule
 *    Skalice 12
 *    256 01 Benesov u Prahy
 *    Czech Republic
 *
 * licenced under BSD licence 
 */
#include "postgres.h"

#include <limits.h>

#include "fmgr.h"

#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "nodes/bitmapset.h"
#include "utils/array.h"
#include "utils/lsyscache.h"

#if PG_VERSION_NUM < 90000
#include "nodes/execnodes.h"
#endif

#define DatumGetBitmapsetP(X)		VarlenaGetBitmapset(PG_DETOAST_DATUM(X))
#define PG_GETARG_BITMAPSET_P(X)	DatumGetBitmapsetP(PG_GETARG_DATUM(X))
#define DatumGetBitmapsetPP(X)		VarlenaGetBitmapset(PG_DETOAST_DATUM_PACKED(X))
#define PG_GETARG_BITMAPSET_PP(X)	DatumGetBitmapsetPP(PG_GETARG_DATUM(X))

#define BitmapsetPGetDatum(X)		PointerGetDatum(BitmapsetGetVarlena(X))
#define PG_RETURN_BITMAPSET_P(X)	return BitmapsetPGetDatum(X)

#define BITMAPSET_SIZE(nwords)	\
	(offsetof(Bitmapset, words) + (nwords) * sizeof(bitmapword))


Datum pst_bms_add_member(PG_FUNCTION_ARGS);
Datum pst_bms_del_member(PG_FUNCTION_ARGS);
Datum pst_bms_add_members(PG_FUNCTION_ARGS);
Datum pst_bms_del_members(PG_FUNCTION_ARGS);
Datum pst_bms_is_member(PG_FUNCTION_ARGS);
Datum pst_bms_in(PG_FUNCTION_ARGS);
Datum pst_bms_out(PG_FUNCTION_ARGS);

Datum pst_bms_to_intarray(PG_FUNCTION_ARGS);
Datum pst_intarray_to_bms(PG_FUNCTION_ARGS);

Datum pst_bms_equal(PG_FUNCTION_ARGS);
Datum pst_bms_union(PG_FUNCTION_ARGS);
Datum pst_bms_intersect(PG_FUNCTION_ARGS);
Datum pst_bms_is_subset(PG_FUNCTION_ARGS);
Datum pst_bms_is_overlap(PG_FUNCTION_ARGS);
Datum pst_bms_num_members(PG_FUNCTION_ARGS);
Datum pst_bms_is_empty(PG_FUNCTION_ARGS);
Datum pst_bms_difference(PG_FUNCTION_ARGS);
Datum pst_bms_nonempty_difference(PG_FUNCTION_ARGS);

Datum pst_bms_collect_transfn(PG_FUNCTION_ARGS);
Datum pst_bms_collect_final(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pst_bms_add_member);
PG_FUNCTION_INFO_V1(pst_bms_del_member);
PG_FUNCTION_INFO_V1(pst_bms_add_members);
PG_FUNCTION_INFO_V1(pst_bms_del_members);
PG_FUNCTION_INFO_V1(pst_bms_is_member);
PG_FUNCTION_INFO_V1(pst_bms_in);
PG_FUNCTION_INFO_V1(pst_bms_out);

PG_FUNCTION_INFO_V1(pst_bms_to_intarray);
PG_FUNCTION_INFO_V1(pst_intarray_to_bms);

PG_FUNCTION_INFO_V1(pst_bms_equal);
PG_FUNCTION_INFO_V1(pst_bms_union);
PG_FUNCTION_INFO_V1(pst_bms_intersect);
PG_FUNCTION_INFO_V1(pst_bms_is_subset);
PG_FUNCTION_INFO_V1(pst_bms_is_overlap);
PG_FUNCTION_INFO_V1(pst_bms_num_members);
PG_FUNCTION_INFO_V1(pst_bms_is_empty);
PG_FUNCTION_INFO_V1(pst_bms_difference);
PG_FUNCTION_INFO_V1(pst_bms_nonempty_difference);

PG_FUNCTION_INFO_V1(pst_bms_collect_transfn);
PG_FUNCTION_INFO_V1(pst_bms_collect_final);

static struct varlena *BitmapsetGetVarlena(Bitmapset * a);
static Bitmapset *VarlenaGetBitmapset(struct varlena *ptr);

/*
 * Convert varlena to bitmapset
 */
static struct varlena *
BitmapsetGetVarlena(Bitmapset * a)
{
	size_t len;
	struct varlena *result;

	if (a == NULL)
	{
		result = palloc(VARHDRSZ);
		SET_VARSIZE(result, VARHDRSZ);
		return result;
	}

	len = a->nwords * sizeof(bitmapword);
	result = palloc(len + VARHDRSZ);
	memcpy(VARDATA(result), a->words, len);
	SET_VARSIZE(result, len + VARHDRSZ);

	return result;
}

/*
 * Anytime this function creates a copy, so we can destroy a 
 * passed data. Original Datum will be unchanged.
 */
static Bitmapset *
VarlenaGetBitmapset(struct varlena *ptr)
{
	size_t size;
	int	nwords;
	Bitmapset *result;

	if (ptr == NULL)
		return NULL;

	size = VARSIZE_ANY_EXHDR(ptr);
	nwords = size / sizeof(bitmapword);
	result = (Bitmapset *) palloc(BITMAPSET_SIZE(nwords));
	memcpy(result->words, VARDATA_ANY(ptr), size);
	result->nwords = nwords;

	return result;
}

/*
 * add member to bitmapset
 */
Datum
pst_bms_add_member(PG_FUNCTION_ARGS)
{
	Bitmapset *a = NULL;
	int	x;

	if (PG_ARGISNULL(1))
		elog(ERROR, "bitmapset can't to store a NULL");
	x = PG_GETARG_INT32(1);

	if (!PG_ARGISNULL(0))
		a = PG_GETARG_BITMAPSET_PP(0);

	a = bms_add_member(a, x);
	PG_RETURN_BITMAPSET_P(a);
}

/*
 * variadic version of previous
 */
Datum 
pst_bms_add_members(PG_FUNCTION_ARGS)
{
	Oid		elmtype;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	ArrayType *v = PG_GETARG_ARRAYTYPE_P(1);
	Datum			*elems;
	bool			*nulls;
	Bitmapset	*a = NULL;
	int	nitems;
	int			i;
    
	if (!PG_ARGISNULL(0))
		a = PG_GETARG_BITMAPSET_PP(0);

	elmtype = ARR_ELEMTYPE(v);
	Assert(elmtype == INT4OID);

	get_typlenbyvalalign(elmtype, &elmlen, &elmbyval, &elmalign);
	deconstruct_array(v, elmtype, elmlen, elmbyval, elmalign,
						&elems, &nulls, &nitems);

	for (i = 0; i < nitems; i++)
		if (nulls[i])
			elog(ERROR, "bitmapset can't to store a NULL");
		else
			a = bms_add_member(a, DatumGetInt32(elems[i]));
	pfree(elems);
	pfree(nulls);

	PG_RETURN_BITMAPSET_P(a);
}

/*
 * delete member from bitmapset
 */
Datum
pst_bms_del_member(PG_FUNCTION_ARGS)
{
	Bitmapset *a = NULL;
	int	x;

	if (PG_ARGISNULL(1))
		elog(ERROR, "bitmapset can't to store a NULL");
	x = PG_GETARG_INT32(1);

	if (!PG_ARGISNULL(0))
		a = PG_GETARG_BITMAPSET_PP(0);

	a = bms_del_member(a, x);
	PG_RETURN_BITMAPSET_P(a);
}

/*
 * variadic form of previous
 */
Datum
pst_bms_del_members(PG_FUNCTION_ARGS)
{
	Oid		elmtype;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	ArrayType *v = PG_GETARG_ARRAYTYPE_P(1);
	Datum			*elems;
	bool			*nulls;
	Bitmapset	*a = NULL;
	int	nitems;
	int			i;

	if (!PG_ARGISNULL(0))
		a = PG_GETARG_BITMAPSET_PP(0);

	elmtype = ARR_ELEMTYPE(v);
	Assert(elmtype == INT4OID);
	
	get_typlenbyvalalign(elmtype, &elmlen, &elmbyval, &elmalign);
	deconstruct_array(v, elmtype, elmlen, elmbyval, elmalign,
						&elems, &nulls, &nitems);

	for (i = 0; i < nitems; i++)
		if (nulls[i])
			elog(ERROR, "bitmapset can't to store a NULL");
		else
			a = bms_del_member(a, DatumGetInt32(elems[i]));

	PG_RETURN_BITMAPSET_P(a);
}

/*
 * returns true when number is member of bitmapset
 */
Datum
pst_bms_is_member(PG_FUNCTION_ARGS)
{
	Bitmapset *a = NULL;
	int	x = 0;		/* be compiler quite */
    
	if (!PG_ARGISNULL(0))
		a = PG_GETARG_BITMAPSET_PP(0);
	if (PG_ARGISNULL(1))
		elog(ERROR, "bitmapset can't to store a NULL");
	else
		x = PG_GETARG_INT32(1);

	PG_RETURN_BOOL(bms_is_member(x, a));
}

enum
{
	INITIAL = 0,
	EXPECTED_NUMBER_OR_PARENTHESIS = 1,
	EXPECTED_NUMBER = 2,
	EXPECTED_COMMA_OR_PARENTHESIS = 3,
	FINAL = 4
};

/*
 * parse cstring - construct bitmapset
 */
Datum
pst_bms_in(PG_FUNCTION_ARGS)
{
	Bitmapset *a = NULL;
	char *initstr = PG_GETARG_CSTRING(0);
	int state = INITIAL;

	while (*initstr)
	{
		if (*initstr == '{' && state == INITIAL)
		{
			initstr++;
			state = EXPECTED_NUMBER_OR_PARENTHESIS;
			continue;
		}
		else if (*initstr == '}' && (state == EXPECTED_NUMBER_OR_PARENTHESIS || 
						state == EXPECTED_COMMA_OR_PARENTHESIS))
		{
			initstr++;
			state = FINAL;		/* end */
			continue;
		}
		else if (*initstr == ' ' || *initstr == '\t' || *initstr == '\n')
		{
			initstr++;
			continue;
		}
		else if (*initstr == ',' && state == EXPECTED_COMMA_OR_PARENTHESIS)
		{
			initstr++;
			state = EXPECTED_NUMBER;
			continue;
		}
		else if (*initstr >= '0' && *initstr <= '9' && (state == EXPECTED_NUMBER ||
						state == EXPECTED_NUMBER_OR_PARENTHESIS))
		{
			long l;
			char *badp;

			l = strtol(initstr, &badp, 10);
			if (errno == ERANGE || l < INT_MIN || l > INT_MAX)
				ereport(ERROR,
						(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
						errmsg("value \"%s\" is out of range for type integer", initstr)));

			a = bms_add_member(a, (int) l);

			if (badp == NULL)
				elog(ERROR, "unexpected end of input");
			initstr = badp;
			state = EXPECTED_COMMA_OR_PARENTHESIS;
			continue;
		}

		elog(ERROR, "unexpected input char '%.*s'", pg_mblen(initstr), initstr);
	}
	
	if (state != FINAL)
		elog(ERROR, "unexpected end of input");

	PG_RETURN_BITMAPSET_P(a);
}

/*
 * print bitmapset
 */
Datum
pst_bms_out(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	StringInfoData  str;
	int x;
	bool	is_first = true;
	
	initStringInfo(&str);
	appendStringInfoChar(&str, '{');
	while ((x = bms_first_member(a)) >= 0)
	{
		if (!is_first)
			appendStringInfo(&str, ",%d", x);
		else
		{
			appendStringInfo(&str, "%d", x);
			is_first = false;
		}
	}
	appendStringInfoChar(&str, '}');
	
	PG_RETURN_CSTRING(str.data);
}

/*
 * convert bitmapset to int array
 */
Datum
pst_bms_to_intarray(PG_FUNCTION_ARGS)
{
	ArrayBuildState *astate = NULL;

	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	int x;
	while ((x = bms_first_member(a)) >= 0)
	{
		astate = accumArrayResult(astate, Int32GetDatum(x), false, INT4OID,
								CurrentMemoryContext);
	}

	PG_RETURN_ARRAYTYPE_P(makeArrayResult(astate, CurrentMemoryContext));
}

/*
 * convert a array of int to bitmapset
 */
Datum
pst_intarray_to_bms(PG_FUNCTION_ARGS)
{
	Oid		elmtype;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	ArrayType *v = PG_GETARG_ARRAYTYPE_P(0);
	Datum			*elems;
	bool			*nulls;
	Bitmapset	*a = NULL;
	int	nitems;
	int			i;

	elmtype = ARR_ELEMTYPE(v);

	get_typlenbyvalalign(elmtype, &elmlen, &elmbyval, &elmalign);
	deconstruct_array(v, elmtype, elmlen, elmbyval, elmalign,
						&elems, &nulls, &nitems);
	for (i = 0; i < nitems; i++)
		if (nulls[i])
			elog(ERROR, "bitmapset can't to store a NULL");
		else
			a = bms_add_member(a, DatumGetInt32(elems[i]));
	pfree(elems);
	pfree(nulls);

	PG_RETURN_BITMAPSET_P(a);
}

/*
 * returns true when both bitmapsets are equal
 */
Datum
pst_bms_equal(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BOOL(bms_equal(a, b));
}

/*
 * join two bitmapsets
 */
Datum
pst_bms_union(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BITMAPSET_P(bms_join(a, b));

}

/*
 * returns intersect of two bitmapsets
 */
Datum
pst_bms_intersect(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BITMAPSET_P(bms_intersect(a, b));
}

/*
 * returns true when bitmapset a is subset of bitmapset b
 */
Datum
pst_bms_is_subset(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BOOL(bms_is_subset(a, b));
}

/*
 * returns true when bitmapset has a nonempty overlap
 */
Datum
pst_bms_is_overlap(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BOOL(bms_overlap(a, b));
}

/*
 * returns a count of members
 */
Datum
pst_bms_num_members(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);

	PG_RETURN_INT32(bms_num_members(a));
}

/*
 * returns true when bitmapset is empty
 */
Datum
pst_bms_is_empty(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);

	PG_RETURN_BOOL(bms_is_empty(a));
}

/*
 * returns a members of bitmapset a without members of 
 * bitmapset b
 */
Datum
pst_bms_difference(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BITMAPSET_P(bms_difference(a, b));
}

/* 
 * returns true when is nonempty difference between 
 * bitmapsets a and b
 */
Datum
pst_bms_nonempty_difference(PG_FUNCTION_ARGS)
{
	Bitmapset *a = PG_GETARG_BITMAPSET_PP(0);
	Bitmapset *b = PG_GETARG_BITMAPSET_PP(1);

	PG_RETURN_BOOL(bms_nonempty_difference(a, b));
}

/*
 * Aggregate function bitmap_collect
 */
Datum
pst_bms_collect_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext aggcontext;
	MemoryContext oldctxt;
	Bitmapset *state = NULL;

#if PG_VERSION_NUM < 90000
	if (fcinfo->context && IsA(fcinfo->context, AggState))
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
		aggcontext = ((WindowAggState *) fcinfo->context)->wincontext;
	else
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "collect_transfn called in non aggregate context");
		aggcontext = NULL;		/* keep compiler quiet */
	}
#else
	if (!AggCheckCallContext(fcinfo, &aggcontext))
		elog(ERROR, "collect_transfn called in non aggregate context"); 

#endif

	state = PG_ARGISNULL(0) ? NULL : (Bitmapset *) PG_GETARG_POINTER(0);

	if (!PG_ARGISNULL(1))
	{
		oldctxt = MemoryContextSwitchTo(aggcontext);
		state = bms_add_member(state, PG_GETARG_INT32(1));
		MemoryContextSwitchTo(oldctxt);
	}

	PG_RETURN_POINTER(state);
}

Datum
pst_bms_collect_final(PG_FUNCTION_ARGS)
{
	Bitmapset *a;

#if PG_VERSION_NUM < 90000
	MemoryContext aggcontext;

	if (fcinfo->context && IsA(fcinfo->context, AggState))
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
		aggcontext = ((WindowAggState *) fcinfo->context)->wincontext;
	else
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "collect_final called in non aggregate context");
		aggcontext = NULL;		/* keep compiler quiet */
	}
#else
	Assert(AggCheckCallContext(fcinfo, NULL));
#endif

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	a = (Bitmapset *) PG_GETARG_POINTER(0);

	PG_RETURN_BITMAPSET_P(a);
}

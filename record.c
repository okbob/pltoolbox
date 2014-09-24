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
#include "funcapi.h"
#include "miscadmin.h"

#if PG_VERSION_NUM >= 90300
#include "access/htup_details.h"
#endif

#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "nodes/makefuncs.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/typcache.h"

Datum	pst_record_expand(PG_FUNCTION_ARGS);
Datum	pst_record_get_field(PG_FUNCTION_ARGS);
Datum	pst_record_get_fields(PG_FUNCTION_ARGS);
Datum	pst_record_set_fields_any(PG_FUNCTION_ARGS);
Datum	pst_record_set_fields_textarray(PG_FUNCTION_ARGS);

Datum	pst_format_op(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pst_record_expand);
PG_FUNCTION_INFO_V1(pst_record_get_field);
PG_FUNCTION_INFO_V1(pst_record_get_fields);
PG_FUNCTION_INFO_V1(pst_record_set_fields_any);
PG_FUNCTION_INFO_V1(pst_record_set_fields_textarray);

PG_FUNCTION_INFO_V1(pst_format_op);

static void deform_record_par0(FunctionCallInfo fcinfo,
					HeapTuple tuple,
					TupleDesc *tupledesc, int *natts,
					Datum	**values, bool **nulls);
static HeapTupleHeader form_result(TupleDesc tupledesc, Datum *values, bool *nulls);

extern Datum pst_sprintf(PG_FUNCTION_ARGS);

/***
 * FUNCTION expand_record(record, 
 * 				    OUT name text, OUT value, OUT typ text)
 * 	RETURNS SETOF record
 ***
 */
Datum 
pst_record_expand(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc rstupdesc; 
	Datum	values[3];
	bool	nulls[3] = {false, false, false};

	HeapTupleHeader		rec = PG_GETARG_HEAPTUPLEHEADER(0);
	TupleDesc		rectupdesc;
	Oid			rectuptyp;
	int32			rectuptypmod;
	HeapTupleData		rectuple;
	int	ncolumns;
	Datum 		*recvalues;
	bool  		*recnulls;

	Tuplestorestate *tupstore;		/* output tuplestore */
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	HeapTuple	tuple;
	int			i;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	/* Extract type info from the tuple itself */
	rectuptyp = HeapTupleHeaderGetTypeId(rec);
	rectuptypmod = HeapTupleHeaderGetTypMod(rec);
	rectupdesc = lookup_rowtype_tupdesc(rectuptyp, rectuptypmod);
	ncolumns = rectupdesc->natts;

	/* Build a temporary HeapTuple control structure */
	rectuple.t_len = HeapTupleHeaderGetDatumLength(rec);
	ItemPointerSetInvalid(&(rectuple.t_self));
	rectuple.t_tableOid = InvalidOid;
	rectuple.t_data = rec;

	recvalues = (Datum *) palloc(ncolumns * sizeof(Datum));
	recnulls = (bool *) palloc(ncolumns * sizeof(bool));
	
	/* Break down the tuple into fields */
	heap_deform_tuple(&rectuple, rectupdesc, recvalues, recnulls);

	/* need to build tuplestore in query context */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);
	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rstupdesc = CreateTupleDescCopy(rsinfo->expectedDesc);

	for (i = 0; i < ncolumns; i++)
	{
		Oid	columntyp = rectupdesc->attrs[i]->atttypid;
		int32	columntypmod = rectupdesc->attrs[i]->atttypmod;


		/* Ignore dropped columns */
		if (rectupdesc->attrs[i]->attisdropped)
			continue;

		/* generate junk in short-term context */
		MemoryContextSwitchTo(oldcontext);
                                                            
		values[0] = CStringGetTextDatum(NameStr(rectupdesc->attrs[i]->attname));
		values[2] = CStringGetTextDatum(format_type_with_typemod(columntyp, columntypmod));

		if (!recnulls[i])
		{
			char *outstr;
			bool		typIsVarlena;
			Oid		typoutput;
			FmgrInfo		proc;
			
			getTypeOutputInfo(columntyp, &typoutput, &typIsVarlena);
			fmgr_info_cxt(typoutput, &proc, oldcontext);
			outstr = OutputFunctionCall(&proc, recvalues[i]);

			values[1] = CStringGetTextDatum(outstr);
			nulls[1] = false;
		}
		else
			nulls[1] = true;

		tuple = heap_form_tuple(rstupdesc, values, nulls);

		MemoryContextSwitchTo(per_query_ctx);
		tuplestore_puttuple(tupstore, tuple);
	}

	ReleaseTupleDesc(rectupdesc);

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
        
	MemoryContextSwitchTo(oldcontext);
                
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = rstupdesc;
        
        pfree(recvalues);
        pfree(recnulls);
        
	return (Datum) 0;
}

/***
 * FUNCTION record_get_field(record, name text) RETURNS text 
 *
 ***
 */
Datum
pst_record_get_field(PG_FUNCTION_ARGS)
{
	HeapTupleHeader		rec = PG_GETARG_HEAPTUPLEHEADER(0);
	TupleDesc		rectupdesc;
	Oid			rectuptyp;
	int32			rectuptypmod;
	HeapTupleData		rectuple;
	int	fno;
	char *outstr;
	bool		typIsVarlena;
	Oid		typoutput;
	FmgrInfo		proc;
	Oid	typeid;
	Datum		value;
	bool		isnull;

	char	*fieldname = text_to_cstring(PG_GETARG_TEXT_P(1));

	/* Extract type info from the tuple itself */
	rectuptyp = HeapTupleHeaderGetTypeId(rec);
	rectuptypmod = HeapTupleHeaderGetTypMod(rec);
	rectupdesc = lookup_rowtype_tupdesc(rectuptyp, rectuptypmod);

	/* Build a temporary HeapTuple control structure */
	rectuple.t_len = HeapTupleHeaderGetDatumLength(rec);
	ItemPointerSetInvalid(&(rectuple.t_self));
	rectuple.t_tableOid = InvalidOid;
	rectuple.t_data = rec;

	fno = SPI_fnumber(rectupdesc, fieldname);
	if (fno == SPI_ERROR_NOATTRIBUTE)
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_COLUMN),
						 errmsg("record has no field \"%s\"",
								fieldname)));

	value = SPI_getbinval(&rectuple, rectupdesc, fno, &isnull);
	if (isnull)
	{
		ReleaseTupleDesc(rectupdesc);
		PG_RETURN_NULL();
	}

	typeid = SPI_gettypeid(rectupdesc, fno);
	getTypeOutputInfo(typeid, &typoutput, &typIsVarlena);
	fmgr_info(typoutput, &proc);
	outstr = OutputFunctionCall(&proc, value);

	ReleaseTupleDesc(rectupdesc);

	PG_RETURN_TEXT_P(cstring_to_text(outstr));
}

/***
 * FUNCTION record_get_fields(record, VARIADIC names text[]) RETURNS text[]
 *
 ***
 */
Datum
pst_record_get_fields(PG_FUNCTION_ARGS)
{
	TupleDesc		rectupdesc;
	HeapTupleData		rectuple;
	int			ncolumns;

	ArrayType	*arr;
	bits8		*arraynullsptr;
	char		*ptr;
	int16	elmlen;
	bool	elmbyval;
	char	elmalign;
	int		numelems;
	int		idx;

	ArrayBuildState		*astate = NULL;

	deform_record_par0(fcinfo, &rectuple, 
					    &rectupdesc, &ncolumns,
					    NULL, NULL);

	/* process a variadic arguments - key and newval */
	arr = PG_GETARG_ARRAYTYPE_P(1);

	 /* Sanity check: does it look like an array at all? */
	if (ARR_NDIM(arr) <= 0 || ARR_NDIM(arr) > MAXDIM)
		elog(ERROR, "broken array parameter");

	numelems = ArrayGetNItems(ARR_NDIM(arr), ARR_DIMS(arr));
	ptr = ARR_DATA_PTR(arr);
	arraynullsptr = ARR_NULLBITMAP(arr);
	get_typlenbyvalalign(ARR_ELEMTYPE(arr),
						&elmlen,
						&elmbyval,
						&elmalign);

	for (idx = 0; idx < numelems; idx++)
	{
		if (arraynullsptr != NULL && !(arraynullsptr[idx / 8] & (1 << (idx % 8))))
		{
			elog(ERROR, "field name is null");
		}
		else
		{
			int	fno;
			char *outstr;
			Datum		value,
						value_text_datum = (Datum) 0;
			bool		isnull;
			char *fieldname;

			fieldname = TextDatumGetCString(fetch_att(ptr, elmbyval, elmlen));
 
			ptr = att_addlength_pointer(ptr, elmlen, ptr);
			ptr = (char *) att_align_nominal(ptr, elmalign);
			
			fno = SPI_fnumber(rectupdesc, fieldname);
			if (fno == SPI_ERROR_NOATTRIBUTE)
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_COLUMN),
							 errmsg("record has no field \"%s\"",
											fieldname)));

			value = SPI_getbinval(&rectuple, rectupdesc, fno, &isnull);
			if (!isnull)
			{
				bool		typIsVarlena;
				Oid		typoutput;
				FmgrInfo		proc;
				Oid	typeid;

				typeid = SPI_gettypeid(rectupdesc, fno);
				getTypeOutputInfo(typeid, &typoutput, &typIsVarlena);
				fmgr_info(typoutput, &proc);
				outstr = OutputFunctionCall(&proc, value);
				value_text_datum = CStringGetTextDatum(outstr);
			}
			else
				outstr = NULL;

			astate = accumArrayResult(astate, value_text_datum, isnull, TEXTOID, CurrentMemoryContext);
		}
	}

	ReleaseTupleDesc(rectupdesc);

	PG_RETURN_ARRAYTYPE_P(makeArrayResult(astate, CurrentMemoryContext));
}

static void
deform_record_par0(FunctionCallInfo fcinfo,
					HeapTuple tuple,
					TupleDesc *tupledesc, int *natts,
					Datum	**values, bool **nulls)
{
	HeapTupleHeader		rec;
	Oid	typ;
	int32	typmod;

	if (PG_ARGISNULL(0))
		elog(ERROR, "input record is null");

	rec = PG_GETARG_HEAPTUPLEHEADER(0);
	typ = get_fn_expr_argtype(fcinfo->flinfo, 0);

	if (typ == RECORDOID)
	{

		/* Extract type info from the tuple itself */
		typ = HeapTupleHeaderGetTypeId(rec);
		typmod = HeapTupleHeaderGetTypMod(rec);
		*tupledesc = lookup_rowtype_tupdesc(typ, typmod);
	}
	else
		*tupledesc = lookup_rowtype_tupdesc(typ, -1);

	*natts = (*tupledesc)->natts;

	/* Build a temporary HeapTuple control structure */
	tuple->t_len = HeapTupleHeaderGetDatumLength(rec);
	ItemPointerSetInvalid(&(tuple->t_self));
	tuple->t_tableOid = InvalidOid;
	tuple->t_data = rec;

	if (values != NULL)
	{
		*values = (Datum *) palloc(*natts * sizeof(Datum));
		*nulls = (bool *) palloc(*natts * sizeof(bool));

		/* Break down the tuple into fields */
		heap_deform_tuple(tuple, *tupledesc, *values, *nulls);
	}

	return;
}

static HeapTupleHeader
form_result(TupleDesc tupledesc, Datum *values, bool *nulls)
{
	HeapTuple tuple;
	HeapTupleHeader		result;
	
	tuple = heap_form_tuple(tupledesc, values, nulls);

	/*
	 * We cannot return tuple->t_data because heap_form_tuple allocates it as
	 * part of a larger chunk, and our caller may expect to be able to pfree
	 * our result.	So must copy the info into a new palloc chunk.
	 */
	result = (HeapTupleHeader) palloc(tuple->t_len);
	memcpy(result, tuple->t_data, tuple->t_len);

	heap_freetuple(tuple);
	pfree(values);
	pfree(nulls);

	ReleaseTupleDesc(tupledesc);

	return result;
}

/***
 * FUNCTION record_set_fields(anyelement, VARIADIC params "any")
 * 									RETURNS anyelement AS 
 ***
 */
Datum
pst_record_set_fields_any(PG_FUNCTION_ARGS)
{
	HeapTupleHeader		result;
	TupleDesc	rectupdesc;
	HeapTupleData		rectuple;
	int	ncolumns;
	Datum 		*recvalues;
	bool  		*recnulls;

	int		i;
	char			*fieldname = NULL;

	deform_record_par0(fcinfo, &rectuple, 
					    &rectupdesc, &ncolumns,
					    &recvalues, &recnulls);

	/* process a variadic arguments - key and newval */

	if (PG_NARGS() % 2 != 1)
		elog(ERROR, "missing a value parameter");

	for (i = 1; i < PG_NARGS(); i++)
	{
		bool	isnull;
		Datum	value;
		char		*cstr;

		if (PG_ARGISNULL(i))
		{
			isnull = true;
			cstr = NULL;
		}
		else
		{
			Oid valtype;
			bool		typIsVarlena;
			Oid		typoutput;
			FmgrInfo		proc;

			isnull = false;
			value = PG_GETARG_DATUM(i);

			valtype = get_fn_expr_argtype(fcinfo->flinfo, i);
			getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
			fmgr_info(typoutput, &proc);
			cstr = OutputFunctionCall(&proc, value);
		}

		/* if parameter is key, store name, else convert value to target type */
		if (i % 2 == 1)
		{
			if (isnull)
				elog(ERROR, "field name is null");

			fieldname = cstr; 
		}
		else
		{
			int fno;
			
			fno = SPI_fnumber(rectupdesc, fieldname);
			if (fno == SPI_ERROR_NOATTRIBUTE)
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_COLUMN),
							 errmsg("record has no field \"%s\"",
											fieldname)));

			if (isnull)
			{
				recnulls[fno - 1] = true;
				pfree(fieldname);
			}
			else
			{
				Oid	typeid;
				Oid		typiofunc;
				Oid		typioparam;
				FmgrInfo	proc;

		    		typeid = SPI_gettypeid(rectupdesc, fno);
				getTypeInputInfo(typeid, &typiofunc, &typioparam);
				fmgr_info(typiofunc, &proc);
				recvalues[fno - 1] = InputFunctionCall(&proc, cstr, typioparam, 
											rectupdesc->attrs[fno - 1]->atttypmod);
				recnulls[fno - 1] = false;

				pfree(fieldname);
				pfree(cstr);
			}
		} 
	}

	result = form_result(rectupdesc, recvalues, recnulls);

	PG_RETURN_HEAPTUPLEHEADER(result);
}

/***
 * FUNCTION record_set_fields(anyelement, VARIADIC params text[])
 * 									RETURNS anyelement AS 
 ***
 */
Datum
pst_record_set_fields_textarray(PG_FUNCTION_ARGS)
{
	HeapTupleHeader		result;
	TupleDesc	rectupdesc;
	HeapTupleData		rectuple;
	int	ncolumns;
	Datum 		*recvalues;
	bool  		*recnulls;

	int		idx;
	char			*fieldname = NULL;

	ArrayType	*arr;
	bits8		*arraynullsptr;
	char		*ptr;
	int16	elmlen;
	bool	elmbyval;
	char	elmalign;
	int		numelems;

	deform_record_par0(fcinfo, &rectuple, 
					    &rectupdesc, &ncolumns,
					    &recvalues, &recnulls);

	/* process a variadic arguments - key and newval */
	arr = PG_GETARG_ARRAYTYPE_P(1);

	 /* Sanity check: does it look like an array at all? */
	if (ARR_NDIM(arr) <= 0 || ARR_NDIM(arr) > MAXDIM)
		elog(ERROR, "broken array parameter");

	numelems = ArrayGetNItems(ARR_NDIM(arr), ARR_DIMS(arr));
	ptr = ARR_DATA_PTR(arr);
	arraynullsptr = ARR_NULLBITMAP(arr);
	get_typlenbyvalalign(ARR_ELEMTYPE(arr),
						&elmlen,
						&elmbyval,
						&elmalign);

	if (numelems % 2 != 0)
		elog(ERROR, "missing a value parameter");

	for (idx = 0; idx < numelems; idx++)
	{
		bool	isnull;
		char		*cstr;

		if (arraynullsptr != NULL && !(arraynullsptr[idx / 8] & (1 << (idx % 8))))
		{
			isnull = true;
			cstr = NULL;
		}
		else
		{
			isnull = false;
			cstr = TextDatumGetCString(fetch_att(ptr, elmbyval, elmlen));
 
			ptr = att_addlength_pointer(ptr, elmlen, ptr);
			ptr = (char *) att_align_nominal(ptr, elmalign);
		}

		/* if parameter is key, store name, else convert value to target type */
		if (idx % 2 == 0)
		{
			if (isnull)
				elog(ERROR, "field name is null");

			fieldname = cstr; 
		}
		else
		{
			int fno;
			
			fno = SPI_fnumber(rectupdesc, fieldname);
			if (fno == SPI_ERROR_NOATTRIBUTE)
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_COLUMN),
							 errmsg("record has no field \"%s\"",
											fieldname)));

			if (isnull)
			{
				recnulls[fno - 1] = true;
				pfree(fieldname);
			}
			else
			{
				Oid	typeid;
				Oid		typiofunc;
				Oid		typioparam;
				FmgrInfo	proc;

		    		typeid = SPI_gettypeid(rectupdesc, fno);
				getTypeInputInfo(typeid, &typiofunc, &typioparam);
				fmgr_info(typiofunc, &proc);
				recvalues[fno - 1] = InputFunctionCall(&proc, cstr, typioparam, 
											rectupdesc->attrs[fno - 1]->atttypmod);
				recnulls[fno - 1] = false;

				pfree(fieldname);
				pfree(cstr);
			}
		} 
	}

	result = form_result(rectupdesc, recvalues, recnulls);

	PG_RETURN_HEAPTUPLEHEADER(result);
}

/*
 * using for string formatting in Python style
 *   'string' % value
 *   'string' % (value, value, ..)
 */
Datum 
pst_format_op(PG_FUNCTION_ARGS)
{
	if (PG_NARGS() > 1)
	{
		Oid typ = get_fn_expr_argtype(fcinfo->flinfo, 1);
		int32	typmod;
		TupleDesc	tupdesc;
		HeapTupleHeader		rec;

		if (typ == RECORDOID)
		{
			rec = PG_GETARG_HEAPTUPLEHEADER(1);

			/* Extract type info from the tuple itself */
			typ = HeapTupleHeaderGetTypeId(rec);
			typmod = HeapTupleHeaderGetTypMod(rec);
			tupdesc = lookup_rowtype_tupdesc(typ, typmod);
		}
		else
			tupdesc = lookup_rowtype_tupdesc_noerror(typ, -1, true);

		/* do unpacking only for composite type */
		if (tupdesc != NULL)
		{
			FunctionCallInfoData	fcinfo2;
			FmgrInfo		fake_flinfo;
			int	natts = tupdesc->natts;
			Datum	*values;
			bool	*nulls;
			int			i,
						    j = 0;
			HeapTupleData		tuple;
			int	ncolums = 0;
			List	*argtypes = NIL;
			FuncExpr *fn_expr = (FuncExpr *) makeNode(FuncExpr);

			rec = PG_GETARG_HEAPTUPLEHEADER(1);

			for(i = 0; i < natts; i++)
				if (!tupdesc->attrs[i]->attisdropped)
					ncolums++;

			fake_flinfo.fn_oid = InvalidOid;
			fake_flinfo.fn_extra = NULL;
			fake_flinfo.fn_mcxt = CurrentMemoryContext;
			fake_flinfo.fn_expr = (Node *) fn_expr;
			fake_flinfo.fn_nargs = ncolums + 1;

#if PG_VERSION_NUM < 90100

			InitFunctionCallInfoData(fcinfo2, &fake_flinfo, ncolums + 1, NULL, NULL);
			fcinfo2.arg[0] = fcinfo->arg[0];
			fcinfo2.argnull[0] = false;
			argtypes = list_make1(makeNullConst(TEXTOID, -1));

#else

			InitFunctionCallInfoData(fcinfo2, &fake_flinfo, ncolums + 1, InvalidOid, NULL, NULL);
			fcinfo2.arg[0] = fcinfo->arg[0];
			fcinfo2.argnull[0] = false;
			argtypes = list_make1(makeNullConst(TEXTOID, -1, InvalidOid));

#endif

			/* Build a temporary HeapTuple control structure */
			tuple.t_len = HeapTupleHeaderGetDatumLength(rec);
			ItemPointerSetInvalid(&(tuple.t_self));
			tuple.t_tableOid = InvalidOid;
			tuple.t_data = rec;

			values = (Datum *) palloc(natts * sizeof(Datum));
			nulls = (bool *) palloc(natts * sizeof(bool));

			/* Break down the tuple into fields */
			heap_deform_tuple(&tuple, tupdesc, values, nulls);

			for (i = 0; i < natts; i++)
			{
				Oid	columntyp = tupdesc->attrs[i]->atttypid;

#if PG_VERSION_NUM < 90100

				Node 	*fakeConstant = (Node *) makeNullConst(columntyp, -1);

#else

				Node 	*fakeConstant = (Node *) makeNullConst(columntyp, -1, InvalidOid);

#endif

				if (tupdesc->attrs[i]->attisdropped)
					continue;
				argtypes = lappend(argtypes, fakeConstant);

				fcinfo2.arg[i+1] = values[j];
				fcinfo2.argnull[i+1] = nulls[j++];
			}

			fn_expr->args = argtypes;

			ReleaseTupleDesc(tupdesc);

			return pst_sprintf(&fcinfo2);
		}
	}

	return pst_sprintf(fcinfo);
}

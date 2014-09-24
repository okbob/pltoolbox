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
#include "utils/lsyscache.h"

PG_FUNCTION_INFO_V1(pst_counter);

Datum pst_counter(PG_FUNCTION_ARGS);

typedef struct
{
	long int	iterations;
	int	freq;
	Oid		typoutput;
} counter_cache;

/*
 * raise notice every n call,
 * returns input without change
 */
Datum
pst_counter(PG_FUNCTION_ARGS)
{
	Datum value = PG_GETARG_DATUM(0);
	counter_cache *ptr = (counter_cache *) fcinfo->flinfo->fn_extra;

	if (ptr == NULL)
	{

		fcinfo->flinfo->fn_extra = MemoryContextAlloc(fcinfo->flinfo->fn_mcxt,
										sizeof(counter_cache));
		ptr = (counter_cache *) fcinfo->flinfo->fn_extra;
		ptr->iterations = 0;
		ptr->typoutput = InvalidOid;

		if (PG_ARGISNULL(1))
			elog(ERROR, "second parameter (output frequency) must not be NULL");

		ptr->freq = PG_GETARG_INT32(1);

		if (!PG_ARGISNULL(2) && PG_GETARG_BOOL(2))
		{
			Oid	valtype;
			Oid	typoutput;
			bool	typIsVarlena;

			valtype = get_fn_expr_argtype(fcinfo->flinfo, 0);
			getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
			ptr->typoutput = typoutput;
		}
	}
	
	if (++ptr->iterations % ptr->freq == 0)
	{
		if (!OidIsValid(ptr->typoutput))
		{
			elog(NOTICE, "processed %ld rows", ptr->iterations);
		}
		else
		{
			/* show a processed row, when it's requested */
			if (PG_ARGISNULL(0))
				elog(NOTICE, "processed %ld rows, current value is null", ptr->iterations);
			else
			{
				elog(NOTICE, "processed %ld rows, current value is '%s'", ptr->iterations,
										    OidOutputFunctionCall(ptr->typoutput, value));
			}
		}
	}

	PG_RETURN_DATUM(value);
}

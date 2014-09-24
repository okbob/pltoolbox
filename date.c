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

#include "utils/date.h"
#include "utils/datetime.h"

/*
 * External (defined in PgSQL datetime.c (timestamp utils))
 */
#if PG_VERSION_NUM >= 90400
#define STRING_PTR_FIELD_TYPE const char *const
#else
#define STRING_PTR_FIELD_TYPE char *
#endif

extern PGDLLIMPORT STRING_PTR_FIELD_TYPE days[];

extern int ora_seq_search(const char *name, STRING_PTR_FIELD_TYPE array[], int max);

static int seq_prefix_search(const char *name, STRING_PTR_FIELD_TYPE array[], int max);


/*
 * date functions
 */
Datum	pst_next_day(PG_FUNCTION_ARGS);
Datum	pst_last_day(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pst_last_day);
PG_FUNCTION_INFO_V1(pst_next_day);

#define CHECK_SEQ_SEARCH(_l, _s) \
do { \
	if ((_l) < 0) { \
		ereport(ERROR, \
				(errcode(ERRCODE_INVALID_DATETIME_FORMAT), \
				 errmsg("invalid value for %s", (_s)))); \
	} \
} while (0)


/*
 * Returns the first weekday that is greater than a date value.
 */
Datum
pst_next_day(PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	text *day_txt = PG_GETARG_TEXT_PP(1);
	const char *str = VARDATA_ANY(day_txt);
	int	len = VARSIZE_ANY_EXHDR(day_txt);
	int off;
	int d = -1;

	/*
	 * Oracle uses only 3 heading characters of the input.
	 * Ignore all trailing characters.
	 */
	if (len >= 3 && (d = seq_prefix_search(str, days, 3)) >= 0)
		goto found;

	CHECK_SEQ_SEARCH(-1, "DAY/Day/day");

found:
	off = d - j2day(day+POSTGRES_EPOCH_JDATE);

	PG_RETURN_DATEADT((off <= 0) ? day+off+7 : day + off);
}

/*
 * Returns last day of the month
 */
Datum
pst_last_day(PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	DateADT result;
	int y, m, d;

	j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
	result = date2j(y, m+1, 1) - POSTGRES_EPOCH_JDATE;

	PG_RETURN_DATEADT(result - 1);
}

static int 
seq_prefix_search(const char *name, STRING_PTR_FIELD_TYPE array[], int max)
{
	int		i;

	if (!*name)
		return -1;

	for (i = 0; array[i]; i++)
	{
		if (pg_strncasecmp(name, array[i], max) == 0)
			return i;
	}
	return -1;	/* not found */
}

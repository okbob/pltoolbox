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
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Datum pst_lc_substring(PG_FUNCTION_ARGS);
Datum pst_diff_string(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pst_lc_substring);
PG_FUNCTION_INFO_V1(pst_diff_string);

static char *
LCSubstr(const char *S, const char *T)
{
	int *LP, *LC;
	int m = strlen(S);
	int n = strlen(T);
	int z = 0;
	int i, j;
	int pos = 0;

	LP = palloc0((n + 1) * sizeof(int));
	LC = palloc0((n + 1) * sizeof(int));

	for (i = 1; i <= m; i++)
	{
		for (j = 1; j <= n; j++)
		{
			if (S[i-1] == T[j-1])
			{
				LC[j] = 1 + LP[j-1];
				if (LC[j] > z)
				{
					z = LC[j];
					pos = i;
				}
			}
			else 
				LC[j] = 0;
		}
		memcpy(LP, LC, (n + 1) * sizeof(int));
	}

	pfree(LP);
	pfree(LC);
	
	if (pg_database_encoding_max_length() > 1)
	{
		char *result;
		char *ptr;
		const char *src;

		/* copy to result only complete multibyte chars */
		result = palloc(z + 1);
		ptr = result;
		src = &S[pos - z];
		
		while (z > 0)
		{
			int len = pg_mblen(src);
			int i;
			
			if (len > z)
				break;
			
			for (i = 0; i < len; i++)
			{
				*ptr++ = *src++;
				z--;
			}
		}

		*ptr = '\0';

		return result;
	}
	else
		return pnstrdup(&S[pos - z], z);
}

Datum
pst_lc_substring(PG_FUNCTION_ARGS)
{
	text *s = PG_GETARG_TEXT_P(0);
	text *t = PG_GETARG_TEXT_P(1);

	PG_RETURN_TEXT_P(cstring_to_text(LCSubstr(text_to_cstring(s), text_to_cstring(t))));
}

#define IS_EMPTY_STR(s)		(*(s) == '\0')

static void 
diff_string(StringInfo str, char *S, char *T)
{
	char *lcs = LCSubstr(S, T);
	char *p1,
		 *p2;
	int	len;
	
	len = pg_mbstrlen(lcs);
	if (len == 0)
	{
		if (!IS_EMPTY_STR(S) && !IS_EMPTY_STR(T))
		{
			appendStringInfo(str, "<del>%s</>", S);
			appendStringInfo(str, "<ins>%s</>", T);
		}
		else if (!IS_EMPTY_STR(S))
			appendStringInfo(str, "<del>%s</>", S);
		else if (!IS_EMPTY_STR(T))
			appendStringInfo(str, "<ins>%s</>", T);

		pfree(lcs);

		return;
	}

	p1 = strstr(S, lcs);
	p2 = strstr(T, lcs);

	Assert(p1 != NULL);
	Assert(p2 != NULL);

	*p1 = '\0';
	*p2 = '\0';

	diff_string(str, S, T);
	appendStringInfoString(str, lcs);
	diff_string(str, p1 + len, p2 + len);

	pfree(lcs);
}

Datum
pst_diff_string(PG_FUNCTION_ARGS)
{
	text *s = PG_GETARG_TEXT_P(0);
	text *t = PG_GETARG_TEXT_P(1);
	text *result;
	StringInfoData str;

	initStringInfo(&str);

	diff_string(&str, text_to_cstring(s), text_to_cstring(t));

	result = cstring_to_text_with_len(str.data, str.len);
	pfree(str.data);

	PG_RETURN_TEXT_P(result);
}

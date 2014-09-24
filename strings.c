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
#include "stdio.h"
#include "wchar.h"

#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"

#if PG_VERSION_NUM < 90000
#define appendStringInfoSpaces(str, n)	appendStringInfo(str, "%*s", n, " ")
#endif

PG_MODULE_MAGIC;

#define CHECK_PAD(symbol, pad_value)	\
do { \
	if (pdesc->flags & pad_value)		\
		ereport(ERROR,  	\
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE), \
				 errmsg("broken sprintf format"),          \
				 errdetail("Format string is '%s'.", TextDatumGetCString(fmt)), 	   \
				 errhint("Symbol '%c' can be used only one time.", symbol))); \
	pdesc->flags |= pad_value; \
} while(0);

/*
 * string functions
 */
Datum	pst_sprintf(PG_FUNCTION_ARGS);
Datum	pst_sprintf_nv(PG_FUNCTION_ARGS);
Datum	pst_format(PG_FUNCTION_ARGS);
Datum	pst_format_nv(PG_FUNCTION_ARGS);
Datum	pst_concat(PG_FUNCTION_ARGS);
Datum	pst_concat_ws(PG_FUNCTION_ARGS);
Datum	pst_concat_js(PG_FUNCTION_ARGS);
Datum	pst_concat_sql(PG_FUNCTION_ARGS);
Datum	pst_left(PG_FUNCTION_ARGS);
Datum	pst_right(PG_FUNCTION_ARGS);
Datum	pst_left(PG_FUNCTION_ARGS);
Datum	pst_rvrs(PG_FUNCTION_ARGS);
Datum	pst_chars_to_array(PG_FUNCTION_ARGS);

/*
 * V1 registrations
 */
PG_FUNCTION_INFO_V1(pst_sprintf);
PG_FUNCTION_INFO_V1(pst_sprintf_nv);
PG_FUNCTION_INFO_V1(pst_format);
PG_FUNCTION_INFO_V1(pst_format_nv);
PG_FUNCTION_INFO_V1(pst_concat);
PG_FUNCTION_INFO_V1(pst_concat_ws);
PG_FUNCTION_INFO_V1(pst_concat_js);
PG_FUNCTION_INFO_V1(pst_concat_sql);
PG_FUNCTION_INFO_V1(pst_rvrs);
PG_FUNCTION_INFO_V1(pst_left);
PG_FUNCTION_INFO_V1(pst_right);
PG_FUNCTION_INFO_V1(pst_chars_to_array);

typedef enum {
	stringfunc_ZERO       =   1,
	stringfunc_SPACE      =   2,
	stringfunc_PLUS       =   4,
	stringfunc_MINUS      =   8,
	stringfunc_STAR_WIDTH =  16,
	stringfunc_SHARP      =  32,
	stringfunc_WIDTH      =  64,
	stringfunc_PRECISION  = 128,
	stringfunc_STAR_PRECISION = 256
} PlaceholderTags;

typedef struct {
	int	flags;
	char		field_type;
	char		lenmod;
	int32		width;
	int32		precision;
} FormatPlaceholderData;

typedef FormatPlaceholderData *PlaceholderDesc;

/*
 * Static functions
 */
static char *json_string(char *str);
static int mb_string_info(text *str, char **sizes, int **positions);

static Datum
castValueTo(Datum value, Oid targetTypeId, Oid inputTypeId)
{
	Oid		funcId;
	CoercionPathType	pathtype;
	FmgrInfo	finfo;
	Datum	   result;

	if (inputTypeId != UNKNOWNOID)
		pathtype = find_coercion_pathway(targetTypeId, inputTypeId, 
									COERCION_EXPLICIT, 
									&funcId);
	else
		pathtype = COERCION_PATH_COERCEVIAIO;

	switch (pathtype)
	{
		case COERCION_PATH_RELABELTYPE:
			result = value;
			break;
		case COERCION_PATH_FUNC:
			{
				Assert(OidIsValid(funcId));
				
				fmgr_info(funcId, &finfo);
				result = FunctionCall1(&finfo, value);
			}
			break;

		case COERCION_PATH_COERCEVIAIO:
			{
				Oid                     typoutput;
				Oid			typinput;
				bool            typIsVarlena;
				Oid		typIOParam;
				char 	*extval;

				getTypeOutputInfo(inputTypeId, &typoutput, &typIsVarlena);
				extval = OidOutputFunctionCall(typoutput, value);
				
				getTypeInputInfo(targetTypeId, &typinput, &typIOParam);
				result = OidInputFunctionCall(typinput, extval, typIOParam, -1);
			}
			break;

		default:
			elog(ERROR, "failed to find conversion function from %s to %s",
					format_type_be(inputTypeId), format_type_be(targetTypeId));
			/* be compiler quiet */
			result = (Datum) 0;
	}

	return result;
}

/*
 * parse and verify sprintf parameter 
 *
 *      %[flags][width][.precision]specifier
 *
 */
static char *
parsePlaceholder(char *src, char *end_ptr, PlaceholderDesc pdesc, text *fmt)
{
	char		c;

	pdesc->field_type = '\0';
	pdesc->lenmod = '\0';
	pdesc->flags = 0;
	pdesc->width = 0;
	pdesc->precision = 0;

	while (src < end_ptr && pdesc->field_type == '\0')
	{
		c = *++src;

		switch (c)
		{
			case '0':
				CHECK_PAD('0', stringfunc_ZERO);
				break;
			case ' ':
				CHECK_PAD(' ', stringfunc_SPACE);
				break;
			case '+':
				CHECK_PAD('+', stringfunc_PLUS);
				break;
			case '-':
				CHECK_PAD('-', stringfunc_MINUS);
				break;
			case '*':
				CHECK_PAD('*', stringfunc_STAR_WIDTH);
				break;
			case '#':
				CHECK_PAD('#', stringfunc_SHARP);
				break;
			case 'o': case 'i': case 'e': case 'E': case 'f': 
			case 'g': case 'd': case 's': case 'x': case 'X': 
				pdesc->field_type = *src;
				break;
			case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				CHECK_PAD('9', stringfunc_WIDTH);
				pdesc->width = c - '0';
				while (src < end_ptr && isdigit(src[1]))
					pdesc->width = pdesc->width * 10 + *++src - '0';
				break;
			case '.':
				if (src < end_ptr)
				{
					if (src[1] == '*')
					{
						CHECK_PAD('.', stringfunc_STAR_PRECISION);
						src++;
					}
					else
					{
						/*
						 * when no one digit is entered, then precision
						 * is zero - digits are optional.
						 */
						CHECK_PAD('.', stringfunc_PRECISION);
						while (src < end_ptr && isdigit(src[1]))
						{
							pdesc->precision = pdesc->precision * 10 + *++src - '0';
						}
					}
				}
				else 
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("broken sprintf format"),
							 errdetail("missing precision value")));
				break;

			default:
				ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("unsupported sprintf format tag '%.*s'", pg_mblen(src), src)));
		}
	}

	if (pdesc->field_type == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("broken sprintf format")));

	return src;
}

static char *
currentFormat(StringInfo str, PlaceholderDesc pdesc)
{
	resetStringInfo(str);
	appendStringInfoChar(str,'%');

	if (pdesc->flags & stringfunc_ZERO)
		appendStringInfoChar(str, '0');

	if (pdesc->flags & stringfunc_MINUS)
		appendStringInfoChar(str, '-');

	if (pdesc->flags & stringfunc_PLUS)
		appendStringInfoChar(str, '+');

	if (pdesc->flags & stringfunc_SPACE)
		appendStringInfoChar(str, ' ');

	if (pdesc->flags & stringfunc_SHARP)
		appendStringInfoChar(str, '#');

	if ((pdesc->flags & stringfunc_WIDTH) || (pdesc->flags & stringfunc_STAR_WIDTH))
		appendStringInfoChar(str, '*');

	if ((pdesc->flags & stringfunc_PRECISION) || (pdesc->flags & stringfunc_STAR_PRECISION))
		appendStringInfoString(str, ".*");

	/* Append l or ll. Decision is based on value of INT64_FORMAT */
	if (pdesc->lenmod == 'l')
	{
		if (strcmp(INT64_FORMAT, "%lld") == 0)
			appendStringInfoString(str, "ll");
		else
			appendStringInfoString(str, "l");
	}
	else if (pdesc->lenmod != '\0')
		appendStringInfoChar(str, pdesc->lenmod);

	appendStringInfoChar(str, pdesc->field_type);

	return str->data;
}

/*
 * simulate %+width.precion%s format of sprintf function 
 */
static void 
append_string(StringInfo str,  PlaceholderDesc pdesc, char *string)
{
	int	nchars = 0;				/* length of substring in chars */
	int	binlen = 0;				/* length of substring in bytes */

	/*
	 * apply precision - it means "show only first n chars", for strings - this flag is 
	 * ignored for proprietary tags %lq and iq, because we can't to show a first n chars 
	 * from possible quoted value. 
	 */
	if (pdesc->flags & stringfunc_PRECISION && pdesc->field_type != 'q')
	{
		char *ptr = string;
		int	  len = pdesc->precision;

		if (pg_database_encoding_max_length() > 1)
		{
			while (*ptr && len > 0)
			{
				ptr += pg_mblen(ptr);
				len--;
				nchars++;
			}
		}
		else
		{
			while (*ptr && len > 0)
			{
				ptr++;
				len--;
				nchars++;
			}
		}

		binlen = ptr - string;
	}
	else
	{
		/* there isn't precion specified, show complete string */
		nchars = pg_mbstrlen(string);
		binlen = strlen(string);
	}

	/* when width is specified, then we have to solve left or right align */
	if (pdesc->flags & stringfunc_WIDTH)
	{
		if (pdesc->width > nchars)
		{
			/* add neccessary spaces to begin or end */
			if (pdesc->flags & stringfunc_MINUS)
			{
				/* allign to left */
				appendBinaryStringInfo(str, string, binlen);
				appendStringInfoSpaces(str, pdesc->width - nchars);
			}
			else
			{
				/* allign to right */
				appendStringInfoSpaces(str, pdesc->width - nchars);
				appendBinaryStringInfo(str, string, binlen);
			}

		}
		else
			/* just copy result to output */
			appendBinaryStringInfo(str, string, binlen);
	}
	else
		/* just copy result to output */
		appendBinaryStringInfo(str, string, binlen);
}

/*
 * Set width and precision when they are defined dynamicaly
 */
static 
int setWidthAndPrecision(PlaceholderDesc pdesc, FunctionCallInfoData *fcinfo, int current)
{

	/* 
	 * don't allow ambiguous definition
	 */
	if ((pdesc->flags & stringfunc_WIDTH) && (pdesc->flags & stringfunc_STAR_WIDTH))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("broken sprintf format"),
				 errdetail("ambiguous width definition")));

	if ((pdesc->flags & stringfunc_PRECISION) && (pdesc->flags & stringfunc_STAR_PRECISION))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("broken sprintf format"),
				 errdetail("ambiguous precision definition")));
	if (pdesc->flags & stringfunc_STAR_WIDTH)
	{
		if (current >= PG_NARGS())
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("too few parameters specified for printf function")));

		if (PG_ARGISNULL(current))
			ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("null value not allowed"),
				 errhint("width (%dth) arguments is NULL", current)));

		pdesc->width = DatumGetInt32(castValueTo(PG_GETARG_DATUM(current), INT4OID, 
									get_fn_expr_argtype(fcinfo->flinfo, current)));
		/* reset flag */
		pdesc->flags ^= stringfunc_STAR_WIDTH;
		pdesc->flags |= stringfunc_WIDTH;
		current += 1;
	}

	if (pdesc->flags & stringfunc_STAR_PRECISION)
	{
		if (current >= PG_NARGS())
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("too few parameters specified for printf function")));

		if (PG_ARGISNULL(current))
			ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("null value not allowed"),
				 errhint("width (%dth) arguments is NULL", current)));

		pdesc->precision = DatumGetInt32(castValueTo(PG_GETARG_DATUM(current), INT4OID, 
									get_fn_expr_argtype(fcinfo->flinfo, current)));
		/* reset flags */
		pdesc->flags ^= stringfunc_STAR_PRECISION;
		pdesc->flags |= stringfunc_PRECISION;
		current += 1;
	}

	return current;
}

/*
 * sprintf function - it is wrapper for libc vprintf function
 *
 *    ensure PostgreSQL -> C casting
 */
Datum
pst_sprintf(PG_FUNCTION_ARGS)
{
	text	   *fmt;
	StringInfoData   str;
	StringInfoData   format_str;
	char		*cp;
	int			i = 1;
	size_t		len;
	char		*start_ptr,
				*end_ptr;
	FormatPlaceholderData		pdesc;
	text *result;

	Oid                     typoutput;
	bool            typIsVarlena;
	Datum 	value;
	Oid valtype;

	/* When format string is null, returns null */
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	fmt = PG_GETARG_TEXT_PP(0);
	len = VARSIZE_ANY_EXHDR(fmt);
	start_ptr = VARDATA_ANY(fmt);
	end_ptr = start_ptr + len - 1;

	initStringInfo(&str);
	initStringInfo(&format_str);

	for (cp = start_ptr; cp <= end_ptr; cp++)
	{
		if (cp[0] == '%')
		{
			/* when cp is not pointer on last char, check %% */
			if (cp < end_ptr && cp[1] == '%')
			{
				appendStringInfoChar(&str, cp[1]);
				cp++;
				continue;
			}

			cp = parsePlaceholder(cp, end_ptr, &pdesc, fmt);
			i = setWidthAndPrecision(&pdesc, fcinfo, i);

			if (i >= PG_NARGS())
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("too few parameters specified for printf function")));

			if (!PG_ARGISNULL(i))
			{
				/* append n-th value */
				value = PG_GETARG_DATUM(i);
				valtype = get_fn_expr_argtype(fcinfo->flinfo, i);

				/* convert value to target type */
				switch (pdesc.field_type)
				{
					case 'o': case 'd': case 'i': case 'x': case 'X':
						{
							int64	target_value;
							const char 		*format;

							pdesc.lenmod = 'l';
							target_value = DatumGetInt64(castValueTo(value, INT8OID, valtype));
							format = currentFormat(&format_str, &pdesc);

							if ((pdesc.flags & stringfunc_WIDTH) && (pdesc.flags & stringfunc_PRECISION))
								appendStringInfo(&str, format, pdesc.width, pdesc.precision, target_value);
							else if (pdesc.flags & stringfunc_WIDTH)
								appendStringInfo(&str, format, pdesc.width, target_value);
							else if (pdesc.flags & stringfunc_PRECISION)
								appendStringInfo(&str, format, pdesc.precision, target_value);
							else
								appendStringInfo(&str, format, target_value);
						}
						break;
					case 'e': case 'f': case 'g': case 'G': case 'E':
						{
							float8	target_value;
							const char 		*format;

							target_value = DatumGetFloat8(castValueTo(value, FLOAT8OID, valtype));
							format = currentFormat(&format_str, &pdesc);

							if ((pdesc.flags & stringfunc_WIDTH) && (pdesc.flags & stringfunc_PRECISION))
								appendStringInfo(&str, format, pdesc.width, pdesc.precision, target_value);
							else if (pdesc.flags & stringfunc_WIDTH)
								appendStringInfo(&str, format, pdesc.width, target_value);
							else if (pdesc.flags & stringfunc_PRECISION)
								appendStringInfo(&str, format, pdesc.precision, target_value);
							else
								appendStringInfo(&str, format, target_value);
						}
						break;
					case 's':
						{
							char		*target_value;

							getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
							target_value = OidOutputFunctionCall(typoutput, value);
							
							append_string(&str, &pdesc, target_value);
							pfree(target_value);
						}
						break;
					default:
						/* don't be happen - formats are checked in parsing stage */
						elog(ERROR, "unknown format: %c", pdesc.field_type);
				}
			}
			else
				/* append a NULL string */
				append_string(&str, &pdesc, "<NULL>");
			i++;
		}
		else
			appendStringInfoChar(&str, cp[0]);
	}

	/* check if all arguments are used */
	if (i != PG_NARGS())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("too many parameters for printf function")));
	result = cstring_to_text_with_len(str.data, str.len);

	pfree(str.data);
	pfree(format_str.data);

	PG_RETURN_TEXT_P(result);
}

/*
 * only wrapper
 */
Datum
pst_sprintf_nv(PG_FUNCTION_ARGS)
{
	return pst_sprintf(fcinfo);
}

typedef struct {
	char	field_type;
	int32	position;
	bool	is_positional;
} placeholder_desc;

#define INVALID_PARAMETER_ERROR(d)	ereport(ERROR, \
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE), \
							 errmsg("invalid formatting string for function format"), \
							 errdetail(d)))

static const char *
parse_format(placeholder_desc *pd, const char *start_ptr, 
							    const char *end_ptr, int nparams)
{
	bool	is_valid = false;

	pd->is_positional = false;

	while (start_ptr <= end_ptr)
	{
		switch (*start_ptr)
		{
			case 's':
				pd->field_type = 's';
				is_valid = true;
				break;

			case 'I':
				pd->field_type = 'I';
				is_valid = true;
				break;

			case 'L':
				pd->field_type = 'L';
				is_valid = true;
				break;

			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				if (pd->is_positional)
					INVALID_PARAMETER_ERROR("positional parameter is defined already");

				pd->position = *start_ptr - '0';
				while (start_ptr < end_ptr && isdigit(start_ptr[1]))
				{
					pd->position = pd->position * 10 + *++start_ptr - '0';
					/* don't allow to integer overflow */
					if (pd->position >= nparams)
						INVALID_PARAMETER_ERROR("placeholder is out of range");
				}

				/* recheck position, or first check for one digit positions */
				if (pd->position >= nparams)
					INVALID_PARAMETER_ERROR("placeholder is out of range");

				/* next character must be a $ */
				if (start_ptr == end_ptr && isdigit(*start_ptr))
					INVALID_PARAMETER_ERROR("unexpected end, missing '$'");

				if (*++start_ptr != '$')
					INVALID_PARAMETER_ERROR("expected '$'");
				start_ptr++;

				pd->is_positional = true;
				break;

			default:
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid formatting string for function format"),
						 errdetail("unsupported tag \"%%%.*s\"", pg_mblen(start_ptr), start_ptr)));
		}

		if (is_valid)
			break;
	}

	if (!is_valid)
		INVALID_PARAMETER_ERROR("missing field type");

	return start_ptr;
}

/*
 * Returns a formated string
 */
Datum
pst_format(PG_FUNCTION_ARGS)
{
	text	   *fmt;
	StringInfoData		str;
	const char		*cp;
	int			i = 1;
	size_t		len;
	const char		*start_ptr;
	const char 			*end_ptr;
	text	*result;

	/* When format string is null, returns null */
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	fmt = PG_GETARG_TEXT_PP(0);
	len = VARSIZE_ANY_EXHDR(fmt);
	start_ptr = VARDATA_ANY(fmt);
	end_ptr = start_ptr + len - 1;

	initStringInfo(&str);
	for (cp = start_ptr; cp <= end_ptr; cp++)
	{
		if (cp[0] == '%')
		{
			placeholder_desc pd = { ' ', 0, false };	/* be compiler quiet */
			Oid	valtype;
			Datum	value;
			bool		isNull;
			int		arg_num;
			Oid	typoutput;
			bool		typIsVarlena;

			/* initial check */
			if (cp == end_ptr)
				INVALID_PARAMETER_ERROR("missing field type");

			/* replace escaped symbol */
			if (cp[1] == '%')
			{
				appendStringInfoChar(&str, '%');
				cp++;
				continue;
			}

			cp = parse_format(&pd, ++cp, end_ptr, PG_NARGS());

			if (pd.is_positional)
			{
				arg_num = pd.position;
			}
			else
			{
				/* ordered arguments are not checked yet */
				if (i >= PG_NARGS())
					INVALID_PARAMETER_ERROR("too few parameters");
				arg_num = i++;
			}

			value = PG_GETARG_DATUM(arg_num);
			isNull = PG_ARGISNULL(arg_num);
			valtype = get_fn_expr_argtype(fcinfo->flinfo, arg_num);
			getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);

			switch (pd.field_type)
			{
				case 's':
					{
						if (!isNull)
						{
							char *outstr = OidOutputFunctionCall(typoutput, value);

							appendStringInfoString(&str, outstr);
							pfree(outstr);
						}

						break;
					}

				case 'I':
					{
						char *target_value;

						if (isNull)
							ereport(ERROR, 
									(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
									 errmsg("SQL identifier is undefined"),
									 errhint("You try to use NULL for %%I format")));

						/* show it as sql identifier */
						target_value = OidOutputFunctionCall(typoutput, value);
						appendStringInfoString(&str, quote_identifier(target_value));
						pfree(target_value);

						break;
					}

				case 'L':
					{
						text *txt;
						text *quoted_txt;

						if (!isNull)
						{
							/* get text value and quotize */
							txt = cstring_to_text(OidOutputFunctionCall(typoutput, value));
							quoted_txt = DatumGetTextP(DirectFunctionCall1(quote_literal,
															    PointerGetDatum(txt)));
							appendStringInfoString(&str, text_to_cstring(quoted_txt));

							pfree(quoted_txt);
							pfree(txt);
						}
						else
							appendStringInfoString(&str, "NULL");

						break;
					}

				default:
						/* not possible */
						elog(ERROR, "unexpected field type");
			}
		}
		else
			appendStringInfoChar(&str, cp[0]);
	}

	result = cstring_to_text_with_len(str.data, str.len);
	pfree(str.data);

        PG_RETURN_TEXT_P(result);
}

/*
 * pst_format_nv - nonvariadic wrapper for text_format function.
 * 
 * note: this wrapper is necessary to be sanity_checks test ok
 */
Datum
pst_format_nv(PG_FUNCTION_ARGS)
{
	return pst_format(fcinfo);
}

/*
 * Concat values to comma separated list. This function
 * is NULL safe. NULL values are skipped.
 */
Datum
pst_concat(PG_FUNCTION_ARGS)
{
	StringInfo	str;
	int	i;

	/* return NULL, if there are not any parameter */
	if (PG_NARGS() == 0)
		PG_RETURN_NULL();

	str = makeStringInfo();
	for(i = 0; i < PG_NARGS(); i++)
	{
		if (i > 0)
			appendStringInfoChar(str, ',');

		if (!PG_ARGISNULL(i))
		{
			Oid	valtype;
			Datum	value;
			Oid                     typoutput;
			bool            typIsVarlena;

			/* append n-th value */
			value = PG_GETARG_DATUM(i);
			valtype = get_fn_expr_argtype(fcinfo->flinfo, i);

			getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
			appendStringInfoString(str, OidOutputFunctionCall(typoutput, value));
		}
	}

	PG_RETURN_TEXT_P(CStringGetTextDatum(str->data));
}

/*
 * Concat values to comma separated list. This function
 * is NULL safe. NULL values are skipped.
 */
Datum
pst_concat_ws(PG_FUNCTION_ARGS)
{
	StringInfo	str;
	int	i;
	char	*sepstr;
	
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	/* return NULL, if there are not any parameter */
	if (PG_NARGS() == 1)
		PG_RETURN_NULL();

	sepstr = TextDatumGetCString(PG_GETARG_TEXT_P(0));

	str = makeStringInfo();
	for(i = 1; i < PG_NARGS(); i++)
	{
		if (i > 1)
			appendStringInfoString(str, sepstr);

		if (!PG_ARGISNULL(i))
		{
			Oid	valtype;
			Datum	value;
			Oid                     typoutput;
			bool            typIsVarlena;

			/* append n-th value */
			value = PG_GETARG_DATUM(i);
			valtype = get_fn_expr_argtype(fcinfo->flinfo, i);

			getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
			appendStringInfoString(str, OidOutputFunctionCall(typoutput, value));
		}
	}

	PG_RETURN_TEXT_P(CStringGetTextDatum(str->data));
}


/*
 * Concat string with respect to SQL format. This is NULL safe.
 * NULLs values are transformated to "NULL" string.
 */
Datum
pst_concat_sql(PG_FUNCTION_ARGS)
{
	StringInfo	str;
	int	i;

	/* return NULL, if there are not any parameter */
	if (PG_NARGS() == 0)
		PG_RETURN_NULL();

	str = makeStringInfo();
	for(i = 0; i < PG_NARGS(); i++)
	{
		if (i > 0)
			appendStringInfoChar(str, ',');

		if (!PG_ARGISNULL(i))
		{
			Oid	valtype;
			Datum	value;
			Oid                     typoutput;
			bool            typIsVarlena;
			TYPCATEGORY	typcat;

			/* append n-th value */
			value = PG_GETARG_DATUM(i);
			valtype = get_fn_expr_argtype(fcinfo->flinfo, i);
			typcat = TypeCategory(valtype);

			if (typcat == 'N' || typcat == 'B')
			{
				getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
				appendStringInfoString(str, OidOutputFunctionCall(typoutput, value));
			}
			else
			{
				text	*txt;
				text	*quoted_txt;
			
				getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
				
				/* get text value and quotize */
				txt = cstring_to_text(OidOutputFunctionCall(typoutput, value));
				quoted_txt = DatumGetTextP(DirectFunctionCall1(quote_literal,
											    PointerGetDatum(txt)));
				appendStringInfoString(str, text_to_cstring(quoted_txt));
			}
		}
		else
			appendStringInfoString(str, "NULL");
	}

	PG_RETURN_TEXT_P(CStringGetTextDatum(str->data));
}


/*
 * Concat string with respect to JSON format. This is NULL safe.
 * NULLs values are transformated to "null" string.
 * JSON uses lowercase characters for constants - see www.json.org
 */
Datum
pst_concat_js(PG_FUNCTION_ARGS)
{
	StringInfo	str;
	int	i;

	/* return NULL, if there are not any parameter */
	if (PG_NARGS() == 0)
		PG_RETURN_NULL();

	str = makeStringInfo();
	for(i = 0; i < PG_NARGS(); i++)
	{
		if (i > 0)
			appendStringInfoChar(str, ',');

		if (!PG_ARGISNULL(i))
		{
			Oid	valtype;
			Datum	value;
			Oid                     typoutput;
			bool            typIsVarlena;
			TYPCATEGORY	typcat;

			/* append n-th value */
			value = PG_GETARG_DATUM(i);
			valtype = get_fn_expr_argtype(fcinfo->flinfo, i);
			typcat = TypeCategory(valtype);

			if (typcat == 'N')
			{
				getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
				appendStringInfoString(str, OidOutputFunctionCall(typoutput, value));
			} else if (typcat == 'B')
			{
				bool	bvalue = PG_GETARG_BOOL(i);
				
				appendStringInfoString(str, bvalue ? "true" : "false");
			}
			else
			{
				getTypeOutputInfo(valtype, &typoutput, &typIsVarlena);
				appendStringInfo(str, "\"%s\"", json_string(OidOutputFunctionCall(typoutput, value)));			
			}
		}
		else
			appendStringInfoString(str, "null");
	}

	PG_RETURN_TEXT_P(CStringGetTextDatum(str->data));
}

/*
 * Returns first n chars. When n is negative, then
 * it returns chars from n+1 position.
 */
Datum
pst_left(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_PP(0);
	int	len = VARSIZE_ANY_EXHDR(str);
	char	*p = VARDATA_ANY(str);
	text   *result;
	int		n = PG_GETARG_INT32(1);
	
	if (len == 0 || n == 0)
		PG_RETURN_TEXT_P(CStringGetTextDatum(""));
	
	if (pg_database_encoding_max_length() > 1)
	{
		char	*sizes;
		int	*positions;
		
		len = mb_string_info(str, &sizes, &positions);
		
		if (n > 0)
		{
			n = n > len ? len : n;
			result = cstring_to_text_with_len(p, positions[n - 1] + sizes[n - 1]);
		}
		else
		{
			if (-n >= len)
				result = cstring_to_text("");
			else
			{
				n = -n > len ? len : -n;
				result = cstring_to_text_with_len(p,  positions[len - n - 1] + sizes[len - n - 1]);
			}
		}

		pfree(positions);
		pfree(sizes);
	}
	else
	{
		if (n > 0)
		{
			n = n > len ? len : n;
			result = cstring_to_text_with_len(p, n);
		}
		else
		{
			n = -n > len ? len :  - n;
			result = cstring_to_text_with_len(p, len - n);
		}
	}

	PG_RETURN_TEXT_P(result);
}

/*
 * Returns last n chars from string. When n is negative,
 * then returns string without last n chars.
 */
Datum
pst_right(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_PP(0);
	int	len = VARSIZE_ANY_EXHDR(str);
	char	*p = VARDATA_ANY(str);
	text   *result;
	int		n = PG_GETARG_INT32(1);
	
	if (len == 0 || n == 0)
		PG_RETURN_TEXT_P(CStringGetTextDatum(""));
	
	if (pg_database_encoding_max_length() > 1)
	{
		char	*sizes;
		int	*positions;
		
		len = mb_string_info(str, &sizes, &positions);
		
		if (n > 0)
		{
			n = n > len ? len : n;
			result = cstring_to_text_with_len(p + positions[len - n],
								positions[len - 1] + sizes[len - 1] - positions[len - n]);
		}
		else
		{
			if (-n >= len)
				result = cstring_to_text("");
			else
			{
				n = -n > len ? len : -n;
				result = cstring_to_text_with_len(p + positions[n - 1] + sizes[n - 1], positions[len - 1] + sizes[len - 1] - positions[n - 1] - sizes[n - 1]);
			}
		}

		pfree(positions);
		pfree(sizes);
	}
	else
	{
		if (n > 0)
		{
			n = n > len ? len : n;
			result = cstring_to_text_with_len(p + len - n, n);
		}
		else
		{
			n = -n > len ? len : -n;
			result = cstring_to_text_with_len(p + n, len - n);
		}
	}

	PG_RETURN_TEXT_P(result);
}

/*
 * Returns reversed string
 */
Datum
pst_rvrs(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_PP(0);
	text	*result;
	char	*p = VARDATA_ANY(str);
	int  len = VARSIZE_ANY_EXHDR(str);
	char	*data;
	int		i;

	result = palloc(len + VARHDRSZ);
	data = (char*) VARDATA(result);
	SET_VARSIZE(result, len + VARHDRSZ);
	
	if (pg_database_encoding_max_length() > 1)
	{
		char	*sizes;
		int	*positions;
		
		/* multibyte version */
		len = mb_string_info(str, &sizes, &positions);
		for (i = len - 1; i >= 0; i--)
		{
			memcpy(data, p + positions[i], sizes[i]);
			data += sizes[i];
		}
		
		pfree(positions);
		pfree(sizes);
	}
	else
	{
		/* single byte version */
		for (i = len - 1; i >= 0; i--)
			*data++ = p[i];
	}
	
	PG_RETURN_TEXT_P(result);
}

Datum
pst_chars_to_array(PG_FUNCTION_ARGS)
{
	text	*chars = PG_GETARG_TEXT_PP(0);
	ArrayBuildState *astate = NULL;
	int	*positions;
	char	*sizes;
	bool		is_mb;
	int	len;
	int		i;
	char		*p = VARDATA_ANY(chars);
	
	is_mb = pg_database_encoding_max_length() > 1;
	
	if (is_mb)
		len = mb_string_info(chars, &sizes, &positions);
	else
		len = VARSIZE_ANY_EXHDR(chars);

	if (len > 0)
	{
		for (i = 0; i < len; i++)
		{
			text	*chr;
		
			if (is_mb)
				chr = cstring_to_text_with_len(p + positions[i], 
										sizes[i]);
			else
				chr = cstring_to_text_with_len(p + i, 1);

			astate = accumArrayResult(astate,
							PointerGetDatum(chr),
							false,
							TEXTOID,
							CurrentMemoryContext);	
						
			pfree(chr);
		}
	
		if (is_mb)
		{
			pfree(positions);
			pfree(sizes);
		}
		
		PG_RETURN_ARRAYTYPE_P(makeArrayResult(astate, 
								CurrentMemoryContext));
	}
	else
		PG_RETURN_ARRAYTYPE_P(construct_empty_array(TEXTOID));
}

/*
 * Convert C string to JSON string
 */
static char *
json_string(char *str)
{
	char	len = strlen(str);
	char		*result, *wc;
	
	wc = result = palloc(len * 2 + 1);
	while (*str != '\0')
	{
		char	c = *str++;
		
		switch (c)
		{
			case '\t':
				*wc++ = '\\';
				*wc++ = 't';
				break;
			case '\b':
				*wc++ = '\\';
				*wc++ = 'b';
				break;
			case '\n':
				*wc++ = '\\';
				*wc++ = 'n';
				break;
			case '\r':
				*wc++ = '\\';
				*wc++ = 'r';
				break;
			case '\\':
				*wc++ = '\\';
				*wc++ = '\\';
				break;
			case '"':
				*wc++ = '\\';
				*wc++ = '"';
				break;
			default:
				*wc++ = c;
		}
	}
	*wc = '\0';
	return result;
}

/*
 * Returns length of string, size and position of every
 * multibyte char in string.
 */
static int
mb_string_info(text *str, char **sizes, int **positions)
{
	int r_len;
	int cur_size = 0;
	int sz;
	char *p;
	int cur = 0;

	p = VARDATA_ANY(str);
	r_len = VARSIZE_ANY_EXHDR(str);

	if (NULL != sizes)
		*sizes = palloc(r_len * sizeof(char));
	if (NULL != positions)
		*positions = palloc(r_len * sizeof(int));

	while (cur < r_len)
	{
		sz = pg_mblen(p);
		if (sizes)
			(*sizes)[cur_size] = sz;
		if (positions)
			(*positions)[cur_size] = cur;
		cur += sz;
		p += sz;
		cur_size += 1;
	}

	return cur_size;
}

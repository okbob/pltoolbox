CREATE SCHEMA pst;

CREATE OR REPLACE FUNCTION pst.sprintf(fmt text, VARIADIC args "any")
RETURNS text 
AS 'MODULE_PATHNAME','pst_sprintf'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.sprintf(fmt text)
RETURNS text 
AS 'MODULE_PATHNAME','pst_sprintf_nv'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.format(fmt text, VARIADIC args "any")
RETURNS text 
AS 'MODULE_PATHNAME','pst_format'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.format(fmt text)
RETURNS text 
AS 'MODULE_PATHNAME','pst_format_nv'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.concat(VARIADIC args "any")
RETURNS text 
AS 'MODULE_PATHNAME','pst_concat'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.concat_ws(separator text, VARIADIC args "any")
RETURNS text 
AS 'MODULE_PATHNAME','pst_concat_ws'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.concat_js(VARIADIC args "any")
RETURNS text 
AS 'MODULE_PATHNAME','pst_concat_js'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.concat_sql(VARIADIC args "any")
RETURNS text 
AS 'MODULE_PATHNAME','pst_concat_sql'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.rvrs(str text)
RETURNS text 
AS 'MODULE_PATHNAME','pst_rvrs'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.reverse(str text)
RETURNS text 
AS 'MODULE_PATHNAME','pst_rvrs'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.left(str text, n int)
RETURNS text 
AS 'MODULE_PATHNAME','pst_left'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.right(str text, n int)
RETURNS text 
AS 'MODULE_PATHNAME','pst_right'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.chars_to_array(str text)
RETURNS text[] 
AS 'MODULE_PATHNAME','pst_chars_to_array'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.next_day(day date, dow text)
RETURNS date
AS 'MODULE_PATHNAME','pst_next_day'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.last_day(day date)
RETURNS date
AS 'MODULE_PATHNAME','pst_last_day'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pst.counter(anyelement, int, bool)
RETURNS anyelement
AS 'MODULE_PATHNAME','pst_counter' LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION pst.diff_string(S text, T text)
RETURNS text
AS 'MODULE_PATHNAME','pst_diff_string' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.lc_substring(S text, T text)
RETURNS text
AS 'MODULE_PATHNAME','pst_lc_substring' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.record_expand(record, OUT name text, OUT value text, OUT typ text)
RETURNS SETOF record
AS 'MODULE_PATHNAME','pst_record_expand' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.record_get_field(record, name text)
RETURNS text
AS 'MODULE_PATHNAME','pst_record_get_field' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.record_get_fields(record, VARIADIC names text[])
RETURNS text[]
AS 'MODULE_PATHNAME','pst_record_get_fields' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.record_set_fields(anyelement, VARIADIC params "any")
RETURNS anyelement
AS 'MODULE_PATHNAME','pst_record_set_fields_any' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.record_set_fields(anyelement, VARIADIC params text[])
RETURNS anyelement
AS 'MODULE_PATHNAME','pst_record_set_fields_textarray' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.format_op(text, "any")
RETURNS text AS 'MODULE_PATHNAME', 'pst_format_op' LANGUAGE C;

CREATE OPERATOR public.%% (
  procedure = pst.format_op,
  LEFTARG = text,
  RIGHTARG = "any"
);

CREATE OPERATOR pst.%% (
  procedure = pst.format_op,
  LEFTARG = text,
  RIGHTARG = "any"
);

CREATE OR REPLACE FUNCTION pst.bitmapset_in(cstring)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_in' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_out(pst.bitmapset)
RETURNS cstring AS 'MODULE_PATHNAME','pst_bms_out' LANGUAGE C STRICT;

CREATE TYPE pst.bitmapset (
  internallength = variable,
  input          = pst.bitmapset_in,
  output         = pst.bitmapset_out,
  like      = bytea
);
      
CREATE OR REPLACE FUNCTION pst.add_member(pst.bitmapset, int)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_add_member' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.add_members(pst.bitmapset, VARIADIC int[])
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_add_members' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.del_member(pst.bitmapset, int)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_del_member' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.del_members(pst.bitmapset, VARIADIC int[])
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_del_members' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.is_member(pst.bitmapset, int)
RETURNS bool AS 'MODULE_PATHNAME','pst_bms_is_member' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.bms_to_intarray(pst.bitmapset)
RETURNS int[] AS 'MODULE_PATHNAME','pst_bms_to_intarray' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.intarray_to_bms(int[])
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_intarray_to_bms' LANGUAGE C STRICT;

CREATE CAST (pst.bitmapset AS int[]) WITH FUNCTION pst.bms_to_intarray(pst.bitmapset);
CREATE CAST (int[] AS pst.bitmapset) WITH FUNCTION pst.intarray_to_bms(int[]);

CREATE OR REPLACE FUNCTION pst.bitmapset_equal(pst.bitmapset, pst.bitmapset)
RETURNS bool AS 'MODULE_PATHNAME','pst_bms_equal' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_union(pst.bitmapset, pst.bitmapset)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_union' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_intersect(pst.bitmapset, pst.bitmapset)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_intersect' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_is_subset(pst.bitmapset, pst.bitmapset)
RETURNS bool AS 'MODULE_PATHNAME','pst_bms_is_subset' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_overlap(pst.bitmapset, pst.bitmapset)
RETURNS bool AS 'MODULE_PATHNAME','pst_bms_is_overlap' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_num_members(pst.bitmapset)
RETURNS int AS 'MODULE_PATHNAME','pst_bms_num_members' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_is_empty(pst.bitmapset)
RETURNS bool AS 'MODULE_PATHNAME','pst_bms_is_empty' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_difference(pst.bitmapset, pst.bitmapset)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_difference' LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pst.bitmapset_nonempty_difference(pst.bitmapset, pst.bitmapset)
RETURNS bool AS 'MODULE_PATHNAME','pst_bms_nonempty_difference' LANGUAGE C STRICT;


CREATE OPERATOR || (
  PROCEDURE = pst.bitmapset_union,
  LEFTARG = pst.bitmapset,
  RIGHTARG = pst.bitmapset
);

CREATE OPERATOR || (
  PROCEDURE = pst.add_member,
  LEFTARG = pst.bitmapset,
  RIGHTARG = int
);

CREATE OPERATOR = (
  PROCEDURE = pst.bitmapset_equal,
  LEFTARG = pst.bitmapset,
  RIGHTARG = pst.bitmapset
);

CREATE OR REPLACE FUNCTION pst.bitmapset_collect_transfn(internal, int)
RETURNS internal AS 'MODULE_PATHNAME','pst_bms_collect_transfn' LANGUAGE C;

CREATE OR REPLACE FUNCTION pst.bitmapset_collect_final(internal)
RETURNS pst.bitmapset AS 'MODULE_PATHNAME','pst_bms_collect_final' LANGUAGE C;

CREATE AGGREGATE pst.bitmapset_collect (int) (
  sfunc = pst.bitmapset_collect_transfn,
  stype = internal,
  finalfunc = pst.bitmapset_collect_final
);

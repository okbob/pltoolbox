SET client_min_messages = warning;
\set ECHO none
\i pltoolbox.sql
RESET client_min_messages;
\pset null '<NULL>'
\set ECHO all

SET search_path = pst;

select sprintf('Hello');
select sprintf('Hello %s', 'World');
select sprintf('Hello %10s', 'World');
select sprintf('>>%10.4d<<', 100.22);
select sprintf('>>%-10.4d<<', 10.20);
select sprintf('>>%10.4f<<', 100.22);
select sprintf('>>%-10.4f<<', 10.20);

select pst.format('Hello %s', 'World');
select pst.format('INSERT INTO %I VALUES(%L,%L,%L)', 'my tab',NULL, 10, 'Paul''s Smith');
-- should to fail
select pst.format('INSERT INTO %I VALUES(%L,%L,%L)', NULL,NULL, 10, 'Paul''s Smith');
select pst.concat('a','b',10,true);
select pst.concat_ws('*','a','b',10,true);
select pst.concat_js('a','b',10,to_date('2010-12-28','YYYY-MM-DD'), true, NULL);
select pst.concat_sql('a','b',10,to_date('2010-12-28','YYYY-MM-DD'), true, NULL);
select pst.rvrs('Ahoj');
select pst.reverse('Ahoj');
select 7 - i, pst.left('Hello', 7 - i) from generate_series(1,14) g(i);
select 7 - i, pst.right('Hello', 7 - i) from generate_series(1,14) g(i);

select next_day(to_date('2010-12-28','YYYY-MM-DD'), 'mon');
select last_day(to_date('2010-12-28','YYYY-MM-DD'));

create table omega(a int, b int);
insert into omega select i, i+10 from generate_series(1,1000) g(i);
create table omega2(a int, b int);

insert into omega2 select (x.xx).* from (select counter(omega,200, true) xx from omega) x;

drop table omega2;

select diff_string('yellow horse', 'yellow cat');
select lc_substring('aaahhhhxxxx','hhhuuuaaaammm');

select record_get_field(omega,'a') from omega limit 10;
select (record_get_fields(omega, variadic array['a','b']))[2] from omega limit 10;
select * from record_expand(record_set_fields(row(10,20), 'f1',23));

create or replace function omega_trg_fce()
returns trigger as $$
declare r record;
begin
  for r in select * from record_expand(new)
  loop
    raise notice 'name := "%", value := "%"', r.name, r.value;
  end loop;
  return new;
end;
$$ language plpgsql;

create trigger xxx before insert on omega for each row execute procedure omega_trg_fce();

insert into omega values(-1,-1000);

select upper('Hello %s %s %s' %% ('World',1999, to_date('2010-12-23','YYYY-MM-DD')));

select '{1,3,4}'::bitmapset;
select '{1,3,4}'::bitmapset || 8;
select '{1,3,4}'::bitmapset || '{3,4,6,9}'::bitmapset;
select '{1,2}'::bitmapset = '{1,2}'::bitmapset;
select '{1,2}'::bitmapset = '{1,2,3}'::bitmapset;
select add_member('{4,2}',3);
select add_member(NULL, 3);
select add_members('{}', 2,3,4);
select del_member('{3,2,1}',3);
select del_members('{3,2,1}',2,3);
select is_member('{1,2}', 3);
select is_member('{1,2}', 2);

select unnest((bitmapset '{1,2,3,4}')::int[]);
select ARRAY[1,2,3,4,2,1]::bitmapset;

select bitmapset_is_subset('{1,2}','{1,2,3}');
select bitmapset_is_subset('{0}','{1,2}');
select bitmapset_is_subset('{1,2}','{1,3}');

select bitmapset_overlap('{1,2}','{1,2,3}');
select bitmapset_overlap('{0}','{1,2}');
select bitmapset_overlap('{1,2}','{1,3}');

select bitmapset_difference('{1,2}','{1,2,3}');
select bitmapset_difference('{0}','{1,2}');
select bitmapset_difference('{1,2}','{1,3}');

select bitmapset_is_empty(bitmapset_difference('{1,2}','{1,2,3}'));
select bitmapset_is_empty(bitmapset_difference('{0}','{1,2}'));

drop table omega;
create table omega(a int);
insert into omega values(1),(2),(10),(NULL),(2),(1),(33);

select bitmapset_collect(a) from omega;

drop table omega;

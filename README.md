pltoolbox
=========
set of functions for usage from PostgreSQL stored procedures

In order to compile and install this module you need to run the following
command from a (root) Unix-like shell

    USE_PGXS=1 make install

In order to enable the module with its SQL interface you need to run the
following command from your (non-mandatory root) shell (You may need to replace contrib 
with extension on some version)

    psql <connection string> -f $(pg_config --sharedir)/contrib/pltoolbox.sql <database>

Remember that this command needs to be run again if you DROP and reCREATE a
database, unless you run the above command into the template1 database.
To uninstall and disable the module you need to run the following:

    psql <connection string> -f $(pg_config --sharedir)/contrib/uninstall_pstcoll.sql

All functions will be available into the pst schema and need thus to be called
like this:

    SELECT * FROM pst.format( 'Hello %s! Now it is %s','world',NOW() );

The remainder of the documentation need to be written yet.
Any volunteers?

Attention
---------
From 2010-12-28 was significantly changed behave of `left` and `right` functions.
Reason - conformance with implemented functions in 9.1. The change is related
to behave when functions are called with negative value parameters.

Some functions from this extensions are in upstream now (Sep 2014) with not necessary
full equal implementation. I don't plan to propagate changes to this extension now. I don't
would to create a some new compatibility issues.

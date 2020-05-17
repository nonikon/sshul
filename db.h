#ifndef _DB_H_
#define _DB_H_

#include <stdint.h>

#include "xhash.h"

typedef xhash_t db_t;

/* load db file */
db_t* db_open(const char* file);
/* check whether 'fname' need to update */
int db_check(db_t* db, const char* fname, uint64_t t);
/* check and update */
int db_update(db_t* db, const char* fname, uint64_t t);
/* save changes to db file and release memory */
void db_close(const char* file, db_t* db);

#endif // _DB_H_
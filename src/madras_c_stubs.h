extern "C" {

#include <string.h>
#include <assert.h>
#include <stdio.h>

#if !defined(SQLITEINT_H)
#include "sqlite3ext.h"
#endif
SQLITE_EXTENSION_INIT1

SQLITE_API void sqlite3_free_stub(void *p) {
  sqlite3_free(p);
}

SQLITE_API void *sqlite3_malloc_stub(int n) {
  return sqlite3_malloc(n);
}

SQLITE_API int sqlite3_declare_vtab_stub(sqlite3 *db, const char *zCreateTable) {
  return sqlite3_declare_vtab(db, zCreateTable);
}

}

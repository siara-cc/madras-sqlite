/*
** 2018-04-19
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file implements a template virtual-table.
** Developers can make a copy of this file as a baseline for writing
** new virtual tables and/or table-valued functions.
**
** Steps for writing a new virtual table implementation:
**
**     (1)  Make a copy of this file.  Perhaps call it "mynewvtab.c"
**
**     (2)  Replace this header comment with something appropriate for
**          the new virtual table
**
**     (3)  Change every occurrence of "madras" to some other string
**          appropriate for the new virtual table.  Ideally, the new string
**          should be the basename of the source file: "mynewvtab".  Also
**          globally change "MADRAS" to "MYNEWVTAB".
**
**     (4)  Run a test compilation to make sure the unmodified virtual
**          table works.
**
**     (5)  Begin making incremental changes, testing as you go, to evolve
**          the new virtual table to do what you want it to do.
**
** This template is minimal, in the sense that it uses only the required
** methods on the sqlite3_module object.  As a result, madras is
** a read-only and eponymous-only table.  Those limitation can be removed
** by adding new methods.
**
** This template implements an eponymous-only virtual table with a rowid and
** two columns named "a" and "b".  The table as 10 rows with fixed integer
** values. Usage example:
**
**     SELECT rowid, a, b FROM madras;
*/
#if !defined(SQLITEINT_H)
#include "sqlite3ext.h"
#endif
SQLITE_EXTENSION_INIT1
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "../../madras-trie/src/madras_dv1.h"

/* madras_vtab is a subclass of sqlite3_vtab which is
** underlying representation of the virtual table
*/
typedef struct madras_vtab madras_vtab;
struct madras_vtab {
  sqlite3_vtab base;  /* Base class - must be first */
  madras_dv1::static_dict dict;
};

/* madras_cursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct madras_cursor madras_cursor;
struct madras_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  madras_dv1::dict_iter_ctx ctx;
  uint8_t *key_buf;
  uint8_t *val_buf;
  int key_len;
  int val_len;
  sqlite3_int64 iRowid;      /* The rowid */
};

/*
** The madrasConnect() method is invoked to create a new
** template virtual table.
**
** Think of this routine as the constructor for madras_vtab objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the madras_vtab object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against the virtual table will look like.
*/
static int madrasConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  madras_vtab *pNew;
  int rc;

  rc = sqlite3_declare_vtab(db,
           "CREATE TABLE x(a,b)"
       );
  /* For convenience, define symbolic names for the index to each column. */
#define MADRAS_A  0
#define MADRAS_B  1
  if( rc==SQLITE_OK ){
    pNew = (madras_vtab *) sqlite3_malloc( sizeof(*pNew) );
    *ppVtab = (sqlite3_vtab*)pNew;
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    pNew->dict.load(argv[3]);
  }
  return rc;
}

/*
** The xConnect and xCreate methods do the same thing, but they must be
** different so that the virtual table is not an eponymous virtual table.
*/
static int madrasCreate(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
 return madrasConnect(db, pAux, argc, argv, ppVtab, pzErr);
}

/*
** This method is the destructor for madras_vtab objects.
*/
static int madrasDisconnect(sqlite3_vtab *pVtab){
  madras_vtab *p = (madras_vtab*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Advance a madras_cursor to its next row of output.
*/
static int madrasNext(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*)cur;
  madras_vtab *vtab = (madras_vtab *) pCur->base.pVtab;
  madras_dv1::static_dict *dict = &vtab->dict;
  pCur->key_len = dict->next(pCur->ctx, pCur->key_buf, pCur->val_buf, &pCur->val_len);
  if (pCur->key_len != 0)
    pCur->iRowid = pCur->ctx.node_path[pCur->ctx.cur_idx];
  else
    pCur->iRowid = -1;
  return SQLITE_OK;
}

/*
** Constructor for a new madras_cursor object.
*/
static int madrasOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  madras_cursor *pCur;
  pCur = (madras_cursor *) sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  pCur->ctx.init();
  *ppCursor = &pCur->base;
  madras_vtab *vtab = (madras_vtab *) p;
  madras_dv1::static_dict *dict = &vtab->dict;
  pCur->key_buf = (uint8_t *) sqlite3_malloc(dict->get_max_key_len());
  pCur->val_buf = (uint8_t *) sqlite3_malloc(dict->get_max_val_len());
  pCur->val_len = 0;
  pCur->key_len = 0;
  return SQLITE_OK;
}

/*
** Destructor for a madras_cursor.
*/
static int madrasClose(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}


static uint32_t read_uint32(const uint8_t *ptr) {
    uint32_t ret;
    ret = ((uint32_t)*ptr++) << 24;
    ret += ((uint32_t)*ptr++) << 16;
    ret += ((uint32_t)*ptr++) << 8;
    ret += *ptr;
    return ret;
}

/*
** Return values of columns for the row at which the madras_cursor
** is currently pointing.
*/
static int madrasColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  madras_cursor *pCur = (madras_cursor*)cur;
  switch( i ){
    case MADRAS_A:
      sqlite3_result_text(ctx, (const char *) pCur->key_buf, pCur->key_len, NULL);
      break;
    default:
      assert( i==MADRAS_B );
      sqlite3_result_int(ctx, read_uint32(pCur->val_buf));
      break;
  }
  return SQLITE_OK;
}

/*
** Return the rowid for the current row.  In this implementation, the
** rowid is the same as the output value.
*/
static int madrasRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  madras_cursor *pCur = (madras_cursor*)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int madrasEof(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*)cur;
  return pCur->iRowid == -1;
}

/*
** This method is called to "rewind" the madras_cursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to madrasColumn() or madrasRowid() or 
** madrasEof().
*/
static int madrasFilter(
  sqlite3_vtab_cursor *pVtabCursor, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  madras_cursor *pCur = (madras_cursor *)pVtabCursor;
  pCur->ctx.cur_idx = 0;
  pCur->ctx.key_pos = 0;
  pCur->ctx.key.resize(0);
  pCur->ctx.child_count.resize(0);
  pCur->ctx.node_path.resize(0);
  pCur->ctx.last_tail_len.resize(0);
  pCur->ctx.init();
  return madrasNext(pVtabCursor);
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
*/
static int madrasBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the 
** virtual table.
*/
static sqlite3_module madrasModule = {
  /* iVersion    */ 0,
  /* xCreate     */ madrasCreate,
  /* xConnect    */ madrasConnect,
  /* xBestIndex  */ madrasBestIndex,
  /* xDisconnect */ madrasDisconnect,
  /* xDestroy    */ madrasDisconnect,
  /* xOpen       */ madrasOpen,
  /* xClose      */ madrasClose,
  /* xFilter     */ madrasFilter,
  /* xNext       */ madrasNext,
  /* xEof        */ madrasEof,
  /* xColumn     */ madrasColumn,
  /* xRowid      */ madrasRowid,
  /* xUpdate     */ 0,
  /* xBegin      */ 0,
  /* xSync       */ 0,
  /* xCommit     */ 0,
  /* xRollback   */ 0,
  /* xFindMethod */ 0,
  /* xRename     */ 0,
  /* xSavepoint  */ 0,
  /* xRelease    */ 0,
  /* xRollbackTo */ 0,
  /* xShadowName */ 0//,
  ///* xIntegrity  */ 0
};

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_madras_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  sqlite3_initialize();
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  rc = sqlite3_create_module(db, "madras", &madrasModule, 0);
  return rc;
}

}

// 1 - int8
// 2 - int16
// 4 - int32
// 8 - int64
// t - text
// d - f64
// f - f32
// b - binary
// v - varint64
// s - sortable varint

// d - distinct
// s - suffix coding
// p - prefix coding
// t - trie
// r - record
// s - store

// 0 - no compression
// s - snappy
// 4 - lz4
// 2 - unishox2
// 3 - unishox3
// t - zstd
// b - bz2
// z - zip
// g - gzip
// 7 - 7z
// a - lzma
// r - rar

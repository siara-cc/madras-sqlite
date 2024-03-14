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

#include "madras_c_stubs.h"
#include "../../madras-trie/src/madras_dv1.hpp"

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
  bool is_point_lookup;
  bool is_val_scan;
  bool is_eof;
  madras_dv1::ctx_vars cv;
  uint8_t *given_val;
  int given_val_len;
  sqlite3_int64 iRowid;      /* The rowid */
  void init() {
    key_buf = val_buf = given_val = NULL;
    key_len = val_len = given_val_len = 0;
    is_point_lookup = is_val_scan = is_eof = false;
  }
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

  rc = sqlite3_declare_vtab_stub(db,
           "CREATE TABLE x (key, val)"
       );
  if( rc==SQLITE_OK ){
    pNew = (madras_vtab *) sqlite3_malloc_stub( sizeof(*pNew) );
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
  sqlite3_free_stub(p);
  return SQLITE_OK;
}

/*
** Advance a madras_cursor to its next row of output.
*/
static int madrasNext(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*)cur;
  if (pCur->is_point_lookup && !pCur->is_val_scan && pCur->key_len != 0) {
    pCur->is_eof = true;
    return SQLITE_OK;
  }
  madras_vtab *vtab = (madras_vtab *) pCur->base.pVtab;
  madras_dv1::static_dict *dict = &vtab->dict;
  if (pCur->is_val_scan && pCur->is_point_lookup) {
    //printf("Given val: %d, [%.*s]\n", pCur->given_val_len, pCur->given_val_len, pCur->given_val);
    while (dict->val_map->next_val(pCur->cv, &pCur->val_len, pCur->val_buf)) {
      if (madras_dv1::cmn::compare(pCur->val_buf, pCur->val_len, pCur->given_val, pCur->given_val_len) == 0) {
        //printf("Val: %d, [%.*s]\n", pCur->val_len, pCur->val_len, pCur->val_buf);
        dict->reverse_lookup_from_node_id(pCur->cv.node_id, &pCur->key_len, pCur->key_buf);
        pCur->iRowid = pCur->cv.node_id;
        pCur->cv.node_id++;
        return SQLITE_OK;
      }
      pCur->cv.node_id++;
    }
    pCur->is_eof = true;
    return SQLITE_OK;
  } else
    pCur->key_len = dict->next(pCur->ctx, pCur->key_buf, pCur->val_buf, &pCur->val_len);
  pCur->iRowid = pCur->ctx.node_path[pCur->ctx.cur_idx];
  pCur->is_eof = false;
  if (pCur->key_len == 0)
    pCur->is_eof = true;
  return SQLITE_OK;
}

/*
** Constructor for a new madras_cursor object.
*/
static int madrasOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  madras_cursor *pCur;
  pCur = (madras_cursor *) sqlite3_malloc_stub( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  madras_vtab *vtab = (madras_vtab *) p;
  madras_dv1::static_dict *dict = &vtab->dict;
  printf("Max Key Len: %u, val len: %u, max lvl: %u\n", dict->get_max_key_len(), dict->get_max_val_len(), dict->get_max_level());
  pCur->ctx.init(dict->get_max_key_len(), dict->get_max_level());
  *ppCursor = &pCur->base;
  pCur->init();
  pCur->key_buf = (uint8_t *) sqlite3_malloc_stub(dict->get_max_key_len());
  pCur->val_buf = (uint8_t *) sqlite3_malloc_stub(dict->get_max_val_len());
  pCur->given_val = (uint8_t *) sqlite3_malloc_stub(dict->get_max_val_len());
  return SQLITE_OK;
}

/*
** Destructor for a madras_cursor.
*/
static int madrasClose(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*)cur;
  sqlite3_free_stub(pCur);
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
    case 0:
      sqlite3_result_text(ctx, (const char *) pCur->key_buf, pCur->key_len, NULL);
      break;
    default:
      sqlite3_result_text(ctx, (const char *) pCur->val_buf, pCur->val_len, NULL);
      //sqlite3_result_int(ctx, read_uint32(pCur->val_buf));
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
  return pCur->is_eof;
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
  madras_vtab *vtab = (madras_vtab *) pCur->base.pVtab;
  madras_dv1::static_dict *dict = &vtab->dict;
  pCur->key_len = 0;
  pCur->ctx.init(dict->get_max_key_len(), dict->get_max_level());
  printf("idxNum: %d, argc: %d, idxStr: %s\n", idxNum, argc, idxStr);
  for (int i = 0; i < argc; i++) {
    printf("arg %d: %s\n", i, sqlite3_value_text(argv[i]));
  }
  if (argc == 1 && idxNum == 1) {
    const uint8_t *key = sqlite3_value_text(argv[0]);
    pCur->ctx = dict->find_first(key, strlen((const char *) key));
    pCur->ctx.to_skip_first_leaf = false;
    pCur->is_point_lookup = true;
    pCur->key_len = 0;
  }
  if (argc == 1 && idxNum == 2) {
    pCur->is_val_scan = true;
    pCur->is_point_lookup = true;
    const uint8_t *val = sqlite3_value_text(argv[0]);
    pCur->given_val_len = strlen((const char *) val);
    memcpy(pCur->given_val, val, pCur->given_val_len);
    pCur->cv.init_cv_nid0(dict->get_trie_loc());
  }
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
  printf("nConstraint: %d\n", pIdxInfo->nConstraint);
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    printf("c%d: iColumn: %d, op: %d, usable: %d\n", i, pIdxInfo->aConstraint[i].iColumn, pIdxInfo->aConstraint[i].op, pIdxInfo->aConstraint[i].usable);
    if (pIdxInfo->aConstraint[i].usable) {
      pIdxInfo->aConstraintUsage[i].argvIndex = i + 1;
      pIdxInfo->idxNum = pIdxInfo->aConstraint[i].iColumn + 1;
    }
  }
  // if (pIdxInfo->nConstraint == 1 && pIdxInfo->aConstraint[0].iColumn == 0) {
  //   pIdxInfo->aConstraintUsage[0].argvIndex = 1;
  // }
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

#ifdef _WIN32
#ifdef MY_LIBRARY_IMPORTS
#define MY_LIBRARY_API __declspec(dllimport)
#else
#define MY_LIBRARY_API __declspec(dllexport)
#endif
#else
#define MY_LIBRARY_API __attribute__((visibility("default"))) 
#endif

extern "C" {

/*
** This following structure defines all the methods for the 
** virtual table.
*/
MY_LIBRARY_API sqlite3_module madrasModule = {
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

MY_LIBRARY_API int sqlite3_madras_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
  int rc = SQLITE_OK;
  sqlite3_initialize();
  SQLITE_EXTENSION_INIT2(pApi);
  rc = sqlite3_create_module(db, "madras", &madrasModule, 0);
  return rc;
}

}

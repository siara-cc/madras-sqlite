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

#include <string>
#include "madras_c_stubs.h"
#include "../../madras-trie/src/madras_dv1.hpp"

/* madras_vtab is a subclass of sqlite3_vtab which is
** underlying representation of the virtual table
*/
typedef struct madras_vtab madras_vtab;
struct madras_vtab {
  sqlite3_vtab base;  /* Base class - must be first */
  madras_dv1::static_trie_map *dict;
};

/* madras_cursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct madras_cursor madras_cursor;
struct madras_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  madras_dv1::iter_ctx *ctx;
  uint8_t *key_buf;
  uint32_t *ptr_bit_count;
  uint8_t *val_buf;
  size_t key_len;
  size_t val_len;
  bool is_point_lookup;
  bool is_val_scan;
  bool is_eof;
  madras_dv1::input_ctx cv;
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
  madras_vtab *pNew = (madras_vtab *) sqlite3_malloc_stub( sizeof(*pNew) );
  *ppVtab = (sqlite3_vtab*)pNew;
  if( pNew==0 ) return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));
  pNew->dict = new madras_dv1::static_trie_map();
  //pNew->dict->map_file_to_mem(argv[3]);
  pNew->dict->load(argv[3]);
  std::string vtct = "CREATE TABLE ";
  const char *table_name = pNew->dict->get_table_name();
  vtct.append(strncmp(table_name, "vtab", 4) == 0 ? argv[2] : table_name);
  vtct.append(" (");
  int col_count = pNew->dict->get_column_count();
  for (int i = 0; i < col_count; i++) {
    if (i > 0)
      vtct.append(", ");
    vtct.append(pNew->dict->get_column_name(i));
    const char type_char = pNew->dict->get_column_type(i);
    switch (type_char) {
      case 't':
        vtct.append(" text");
        break;
      case '*':
        vtct.append(" varchar");
        break;
      case '0': case 'i':
        vtct.append(" integer");
        break;
      case '1': case '2': case '3': case '4': case '5':
      case '6': case '7': case '8': case '9':
      case 'j': case 'k': case 'l': case 'm': case 'n':
      case 'o': case 'p': case 'q': case 'r':
      case 'x': case 'X': case 'y': case 'Y':
        vtct.append(" double");
        break;
      default:
        vtct.append(" text");
    }
  }
  vtct.append(")");
  printf("vtct: %s\n", vtct.c_str());
  int rc = sqlite3_declare_vtab_stub(db, vtct.c_str());
  if (rc != SQLITE_OK)
    sqlite3_free_stub(pNew);
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
  delete p->dict;
  sqlite3_free_stub(p);
  return SQLITE_OK;
}

/*
** Advance a madras_cursor to its next row of output.
*/
static int madrasNext(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*)cur;
  if (pCur->is_point_lookup && !pCur->is_val_scan && pCur->key_len != -2) {
    pCur->is_eof = true;
    return SQLITE_OK;
  }
  madras_vtab *vtab = (madras_vtab *) pCur->base.pVtab;
  madras_dv1::static_trie_map *dict = vtab->dict;
  if (pCur->is_val_scan && pCur->is_point_lookup) {
    //printf("Given val: %d, [%.*s]\n", pCur->given_val_len, pCur->given_val_len, pCur->given_val);
    // while (dict->val_map->next_val(pCur->cv, &pCur->val_len, pCur->val_buf)) {
    //   if (gen::compare(pCur->val_buf, pCur->val_len, pCur->given_val, pCur->given_val_len) == 0) {
    //     //printf("Val: %d, [%.*s]\n", pCur->val_len, pCur->val_len, pCur->val_buf);
    //     if (dict->key_count > 0)
    //       dict->reverse_lookup_from_node_id(pCur->cv.node_id, &pCur->key_len, pCur->key_buf);
    //     pCur->iRowid = pCur->cv.node_id;
    //     pCur->cv.node_id++;
    //     return SQLITE_OK;
    //   }
    //   pCur->cv.node_id++;
    // }
    pCur->is_eof = true;
    return SQLITE_OK;
  } else {
    if (dict->get_key_count() > 0)
      pCur->key_len = dict->next(*pCur->ctx, pCur->key_buf);
    //printf("Key: [%.*s], %d\n", pCur->key_len, pCur->key_buf, pCur->key_len);
  }
  pCur->is_eof = false;
  if (dict->get_key_count() > 0) {
    pCur->iRowid = pCur->ctx->node_path[pCur->ctx->cur_idx];
    if (pCur->key_len == -2)
      pCur->is_eof = true;
  } else {
    pCur->iRowid = pCur->cv.node_id;
    pCur->cv.node_id++;
    if (pCur->iRowid >= dict->get_node_count())
      pCur->is_eof = true;
  }
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
  madras_dv1::static_trie_map *dict = vtab->dict;
  printf("Max Key Len: %u, val len: %u, max lvl: %u, column_count: %u\n", dict->get_max_key_len(), dict->get_max_val_len(), dict->get_max_level(), dict->get_column_count());
  pCur->ctx = new madras_dv1::iter_ctx();
  memset(pCur->ctx, '\0', sizeof(madras_dv1::iter_ctx));
  pCur->ctx->init(dict->get_max_key_len(), dict->get_max_level());
  *ppCursor = &pCur->base;
  pCur->init();
  pCur->key_buf = new uint8_t[dict->get_max_key_len()];
  pCur->val_buf = new uint8_t[dict->get_max_val_len()];
  pCur->given_val = new uint8_t[dict->get_max_val_len()];
  pCur->ptr_bit_count = new uint32_t[dict->get_column_count()];
  return SQLITE_OK;
}

/*
** Destructor for a madras_cursor.
*/
static int madrasClose(sqlite3_vtab_cursor *cur){
  madras_cursor *pCur = (madras_cursor*) cur;
  pCur->ctx->close();
  delete [] pCur->key_buf;
  delete [] pCur->val_buf;
  delete [] pCur->given_val;
  delete [] pCur->ptr_bit_count;
  delete pCur->ctx;
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
  madras_vtab *vtab = (madras_vtab *) pCur->base.pVtab;
  madras_dv1::iter_ctx *iter_ctx = pCur->ctx;
  char data_type = vtab->dict->get_column_type(i);
  uint8_t *out_buf;
  int out_buf_len;
  if (vtab->dict->get_max_val_len(i) == 0 && vtab->dict->get_key_count() > 0) {
    out_buf = pCur->key_buf;
    out_buf_len = pCur->key_len;
  } else {
    uint32_t cur_node_id = pCur->iRowid;
    if (vtab->dict->get_key_count() > 0)
      cur_node_id = iter_ctx->node_path[iter_ctx->cur_idx];
    vtab->dict->get_col_val(cur_node_id, i, &pCur->val_len, pCur->val_buf, &pCur->ptr_bit_count[i]);
    out_buf = pCur->val_buf;
    out_buf_len = pCur->val_len;
  }
  if ((data_type >= '0' && data_type <= '9' && out_buf[0] == 0)
        || ((data_type == 't' || data_type == '*') 
        && memcmp(out_buf, madras_dv1::NULL_VALUE, madras_dv1::NULL_VALUE_LEN) == 0))
    sqlite3_result_null(ctx);
  else {
    switch (data_type) {
      case 't':
        sqlite3_result_text(ctx, (const char *) out_buf, out_buf_len, SQLITE_TRANSIENT);
        break;
      case '*':
        sqlite3_result_blob(ctx, out_buf, out_buf_len, SQLITE_TRANSIENT);
        break;
      case '0':
      case 'i':
        sqlite3_result_int(ctx, *((int64_t *)out_buf));
        break;
      case '1'... '9':
      case 'j'... 'r':
      case 'x': case 'X': case 'y': case 'Y':
        sqlite3_result_double(ctx, *((double*)out_buf));
        break;
    }
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
  madras_dv1::static_trie_map *dict = vtab->dict;
  pCur->key_len = 0;
  pCur->ctx->init(dict->get_max_key_len(), dict->get_max_level());
  printf("idxNum: %d, argc: %d, idxStr: %s\n", idxNum, argc, idxStr);
  for (int i = 0; i < dict->get_column_count(); i++)
    pCur->ptr_bit_count[i] = UINT32_MAX;
  for (int i = 0; i < argc; i++) {
    printf("arg %d: %s\n", i, sqlite3_value_text(argv[i]));
  }
  if (dict->get_key_count() > 0 && idxNum == 2 && (argc == 1 || (argc == 2 && argv[1] == NULL))) {
    const uint8_t *key = sqlite3_value_text(argv[0]);
    int key_len = 0;
    if (key != NULL)
      key_len = strlen((const char *) key);
    bool is_success = dict->find_first(key, key_len, *pCur->ctx, true);
    pCur->ctx->to_skip_first_leaf = false;
    pCur->is_point_lookup = true;
    pCur->key_len = -2;
  }
  if (argc == 1 && idxNum == 1) {
    pCur->is_val_scan = true;
    pCur->is_point_lookup = true;
    const uint8_t *val = sqlite3_value_text(argv[0]);
    pCur->given_val_len = 0;
    if (val != NULL)
      pCur->given_val_len = strlen((const char *) val);
    memcpy(pCur->given_val, val, pCur->given_val_len);
  }
  if (dict->get_key_count() == 0) {
    pCur->cv.node_id = 0;
    pCur->iRowid = 0;
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
      if (pIdxInfo->aConstraint[i].op != 73 && pIdxInfo->aConstraint[i].op != 74) {
        pIdxInfo->aConstraintUsage[i].argvIndex = i + 1;
        pIdxInfo->idxNum = pIdxInfo->aConstraint[i].iColumn + 1;
      }
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

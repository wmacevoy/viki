#ifndef VIKI_INDEX_H
#define VIKI_INDEX_H

#include <sqlite3.h>

/* `viki index <dir>`: walk dir recursively, chunk + hash text files,
** incrementally maintain viki_chunk + chunk_fts in db. model_id is fixed
** to "none" for now (rung 0 / BM25-only -- see FINDINGS.md: no ONNX
** embedding pipeline yet). Returns 0 on success. */
int viki_cmd_index(sqlite3 *db, const char *zDir);

#endif

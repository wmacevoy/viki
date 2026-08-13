/*
** viki.c -- CLI entry point and subcommand dispatch.
**
** Milestone 1 (KICKOFF.md). Both retrieval rungs are real and working
** end to end: FTS5 BM25 ("rung 0") and ONNX sentence embeddings +
** sqlite-ndvss cosine ("rung 2"), rank-fused by `viki ask`. When no
** model is present the rung-2 leg drops out and `viki ask` is honest
** about it (prints a degraded-mode notice) rather than faking hybrid
** retrieval -- VIKI_DESIGN.md's required standalone path, not a stub.
*/
#include "viki_db.h"
#include "viki_index.h"
#include "viki_ask.h"
#include "viki_cache.h"
#include "viki_serve.h"
#include "embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define VIKI_VERSION "0.1.0-m1"

static void usage(void){
    fprintf(stderr,
        "usage: viki <subcommand> [args...]\n\n"
        "  index [dir]              Walk dir (default: .) and (re)index into the local cache\n"
        "  ask \"<query>\" [--k N]    Hybrid BM25+vector search (default N=5); BM25-only if no model found\n"
        "  cache push [db-path]     Publish the local cache db as a fossil uv blob (default: .viki/cache.db)\n"
        "  cache pull [db-path]     Fetch the cache db from a fossil uv blob\n"
        "  serve [--host H] [--port N]\n"
        "                           Local HTTP server: human search page at / plus a JSON API for\n"
        "                           agents/scripts (GET /api/ask?q=&k=, /api/chunk?hash=&ix=,\n"
        "                           /api/health). Loopback-only by default (127.0.0.1:8080), no auth\n"
        "                           -- do not expose this port to a network.\n"
        "  version                  Print version\n"
        "  help                     Show this message\n"
        "\n"
        "All subcommands except 'version'/'help' open (creating if needed) the local\n"
        "cache db at .viki/cache.db, relative to the current directory. Run from within\n"
        "a fossil-see checkout.\n"
        "\n"
        "Embedding model directory (model.onnx + vocab.txt + viki-manifest.json):\n"
        "$VIKI_MODEL_DIR if set, else the build's own build/dist/model. Missing/absent\n"
        "is not an error -- degrades to BM25-only per VIKI_DESIGN.md.\n");
}

/* Resolves the model directory and opens an embedder, or returns NULL if
** none is configured/loadable -- this is the expected, handled path when
** no model is present (VIKI_DESIGN.md's required degraded mode), not an
** error callers should treat as fatal. */
static viki_embedder *open_embedder_if_available(void){
    const char *dir = getenv("VIKI_MODEL_DIR");
    struct stat st;
    if( !dir || !dir[0] ) dir = "build/dist/model";
    if( stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) ) return NULL;
    return viki_embedder_open(dir);
}

static int ensure_viki_dir(void){
    struct stat st;
    if( stat(".viki", &st) == 0 ) return S_ISDIR(st.st_mode) ? 0 : 1;
    if( mkdir(".viki", 0755) != 0 ){
        perror("viki: mkdir .viki");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv){
    const char *sub;
    int rc = 0;

    if( argc < 2 ){ usage(); return 1; }
    sub = argv[1];

    if( strcmp(sub, "version") == 0 ){
        printf("viki %s\n", VIKI_VERSION);
        return 0;
    }
    if( strcmp(sub, "help") == 0 || strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 ){
        usage();
        return 0;
    }

    viki_db_register_ndvss();

    if( strcmp(sub, "index") == 0 ){
        sqlite3 *db;
        viki_embedder *emb;
        const char *dir = argc > 2 ? argv[2] : ".";
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        emb = open_embedder_if_available();
        if( !emb ) fprintf(stderr, "viki index: no embedding model found -- indexing BM25-only (rung 0)\n");
        rc = viki_cmd_index(db, dir, emb);
        if( emb ) viki_embedder_close(emb);
        sqlite3_close(db);
        return rc;
    }

    if( strcmp(sub, "ask") == 0 ){
        sqlite3 *db;
        viki_embedder *emb;
        int k = 5;
        int i;
        if( argc < 3 ){ fprintf(stderr, "usage: viki ask \"<query>\" [--k N]\n"); return 1; }
        for( i = 3; i < argc - 1; i++ ){
            if( strcmp(argv[i], "--k") == 0 ) k = atoi(argv[i+1]);
        }
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        emb = open_embedder_if_available();
        rc = viki_cmd_ask(db, argv[2], k, emb);
        if( emb ) viki_embedder_close(emb);
        sqlite3_close(db);
        return rc;
    }

    if( strcmp(sub, "serve") == 0 ){
        sqlite3 *db;
        viki_embedder *emb;
        const char *host = "127.0.0.1";
        int port = 8080;
        int i;
        for( i = 2; i < argc; i++ ){
            if( strcmp(argv[i], "--port") == 0 && i + 1 < argc ) port = atoi(argv[++i]);
            else if( strcmp(argv[i], "--host") == 0 && i + 1 < argc ) host = argv[++i];
        }
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        emb = open_embedder_if_available();
        if( !emb ) fprintf(stderr, "viki serve: no embedding model found -- serving BM25-only (rung 0)\n");
        rc = viki_cmd_serve(db, emb, host, port, VIKI_VERSION);
        if( emb ) viki_embedder_close(emb);
        sqlite3_close(db);
        return rc;
    }

    if( strcmp(sub, "cache") == 0 ){
        const char *dbPath = VIKI_DEFAULT_CACHE_DB;
        if( argc < 3 ){ fprintf(stderr, "usage: viki cache push|pull [db-path]\n"); return 1; }
        if( argc > 3 ) dbPath = argv[3];
        if( ensure_viki_dir() ) return 1;
        if( strcmp(argv[2], "push") == 0 ) return viki_cmd_cache_push(dbPath);
        if( strcmp(argv[2], "pull") == 0 ) return viki_cmd_cache_pull(dbPath);
        fprintf(stderr, "viki cache: unknown action '%s' (want push|pull)\n", argv[2]);
        return 1;
    }

    if( strcmp(sub, "ndvss-selftest") == 0 ){
        /* Debug/regression command, not in usage(): proves sqlite-ndvss is
        ** really statically linked and functional, independent of whether
        ** any real vector data exists yet. See FINDINGS.md. */
        sqlite3 *db;
        sqlite3_stmt *st;
        if( sqlite3_open(":memory:", &db) != SQLITE_OK ) return 1;
        if( sqlite3_prepare_v2(db, "SELECT ndvss_instruction_set()", -1, &st, NULL) != SQLITE_OK ){
            fprintf(stderr, "ndvss-selftest: ndvss_instruction_set() not registered: %s\n",
                    sqlite3_errmsg(db));
            sqlite3_close(db);
            return 1;
        }
        if( sqlite3_step(st) == SQLITE_ROW ){
            printf("ndvss instruction set: %s\n", sqlite3_column_text(st, 0));
        }
        sqlite3_finalize(st);
        sqlite3_close(db);
        return 0;
    }

    if( strcmp(sub, "embed-selftest") == 0 ){
        /* Debug/regression command, not in usage(): proves the ONNX
        ** embedding pipeline is real -- loads the model, tokenizes, runs
        ** inference, and checks a semantic property (similar sentences
        ** cosine-closer than dissimilar ones) rather than just "did it
        ** not crash". argv[2] is the model dir (default build/dist/model). */
        const char *modelDir = argc > 2 ? argv[2] : "build/dist/model";
        viki_embedder *emb;
        float vA[1024], vB[1024], vC[1024];
        int dim, i;
        double simAB = 0, simAC = 0;

        emb = viki_embedder_open(modelDir);
        if( !emb ){ fprintf(stderr, "embed-selftest: FAIL (could not open model at '%s')\n", modelDir); return 1; }
        dim = viki_embedder_dim(emb);
        printf("embed-selftest: model_id=%s dim=%d\n", viki_embedder_model_id(emb), dim);

        if( viki_embed(emb, "six horses were grazing near the water trough", vA) ||
            viki_embed(emb, "a group of horses stood by the watering hole", vB) ||
            viki_embed(emb, "the quarterly tax filing deadline is in April", vC) ){
            fprintf(stderr, "embed-selftest: FAIL (viki_embed returned an error)\n");
            viki_embedder_close(emb);
            return 1;
        }

        for( i = 0; i < dim; i++ ){ simAB += vA[i]*vB[i]; simAC += vA[i]*vC[i]; }
        printf("cosine(horses/trough, horses/watering-hole) = %.4f\n", simAB);
        printf("cosine(horses/trough, tax-filing-deadline)   = %.4f\n", simAC);
        viki_embedder_close(emb);

        if( simAB > simAC && simAB > 0.5 ){
            printf("embed-selftest: PASS\n");
            return 0;
        }
        fprintf(stderr, "embed-selftest: FAIL (similar-sentence pair not clearly closer than the unrelated one)\n");
        return 1;
    }

    fprintf(stderr, "viki: unknown subcommand '%s'\n\n", sub);
    usage();
    return 1;
}

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
#include "viki_muse.h"
#include "viki_grep.h"
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
        "  ask \"<query>\" [--k N]    Hybrid BM25+vector search (default N=5); BM25-only if no model found.\n"
        "                           Each hit prints one header line, then the snippet indented 4 spaces:\n"
        "                             [<rank>] rrf=<score>  <content_hash>#<chunk_ix>  <source>\n"
        "                           content_hash is the citable identity (same value /api/ask returns as\n"
        "                           \"hash\"); <source> is a best-effort path hint and may be unknown.\n"
        "  cache push [db-path] [--no-model]\n"
        "                           Publish the local cache db (default: .viki/cache.db) AND the\n"
        "                           pinned model directory as fossil uv blobs -- viki-cache.db plus\n"
        "                           viki-model/{model.onnx,vocab.txt,viki-manifest.json}, per D-12.\n"
        "                           An unchanged model epoch is not re-pushed; no model present is\n"
        "                           not an error (peers stay BM25-only). --no-model: cache only.\n"
        "  cache pull [db-path] [--no-model]\n"
        "                           Fetch both back. The model is written to the same directory\n"
        "                           'ask' reads (below) and checked against the manifest's sha256.\n"
        "                           A hub with no model published is not an error.\n"
        "  muse [--k N] [--seed N] [--from <hash>#<ix>] [--chars N] [--bias none|old]\n"
        "       [--same-source] [--model-id ID]\n"
        "                           Undirected recall: NO query. Picks a random seed chunk and\n"
        "                           returns chunks from the calibrated MIDDLE of that seed's\n"
        "                           similarity band -- related enough to matter, far enough out\n"
        "                           that `ask` would not surface them. Vector-only: no model, no\n"
        "                           muse -- there is no BM25 analogue of \"the middle of the\n"
        "                           distribution\", so an unembedded cache is refused, not faked.\n"
        "                           Needs vectors but NOT the model file: a cache pulled from a\n"
        "                           peer (D-11/D-12) muses fine with no model.onnx on disk.\n"
        "                           --seed makes a run replayable; the seed used is always\n"
        "                           printed. Run header on stderr, hits on stdout, in `ask` form:\n"
        "                             [<n>] cos=<c> rank=<r>  <content_hash>#<chunk_ix>  <source>\n"
        "  grep \"<regex>\" [--k N] [-i] [--source <like>] [--chars N]\n"
        "                           POSIX regex over EVERY indexed chunk -- including the\n"
        "                           artifacts that are not files and so cannot be grepped:\n"
        "                           check-in comments, wiki, tickets, forum posts, tech\n"
        "                           notes, ticket changes, attachments, unversioned files.\n"
        "                           Exact and UNRANKED, unlike 'ask'. Syntax is POSIX ERE,\n"
        "                           NOT PCRE: use [[:digit:]] not \\d, and there is no\n"
        "                           lookaround. -i is case-insensitive; --source filters by\n"
        "                           SQL LIKE on the source path (e.g. --source 'ckin:%').\n"
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
    /* viki_model_dir() rather than a local copy of the
    ** $VIKI_MODEL_DIR-else-build/dist/model rule: `viki cache pull` writes the
    ** model it fetched to that same function's answer, so if the two ever
    ** disagreed a fresh clone would pull a model and then silently ignore it,
    ** degrading to BM25-only with no diagnostic. One definition, in the module
    ** that publishes the directory. */
    const char *dir = viki_model_dir();
    struct stat st;
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

    if( strcmp(sub, "grep") == 0 ){
        /* Regex over the indexed corpus. No embedder is opened: this path
        ** never embeds anything, and loading the ONNX model costs more than
        ** the whole scan on a personal-scale cache. */
        sqlite3 *db;
        int k = 0, icase = 0, nChars = 160, i;
        const char *zSrc = NULL;
        if( argc < 3 ){ fprintf(stderr, "usage: viki grep \"<regex>\" [--k N] [-i] [--source <like>] [--chars N]\n"); return 1; }
        for( i = 3; i < argc; i++ ){
            if( strcmp(argv[i], "--k") == 0 && i + 1 < argc ) k = atoi(argv[++i]);
            else if( strcmp(argv[i], "-i") == 0 ) icase = 1;
            else if( strcmp(argv[i], "--source") == 0 && i + 1 < argc ) zSrc = argv[++i];
            else if( strcmp(argv[i], "--chars") == 0 && i + 1 < argc ) nChars = atoi(argv[++i]);
            else { fprintf(stderr, "viki grep: unknown option '%s'\n", argv[i]); return 1; }
        }
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        if( viki_grep_register(db) != SQLITE_OK ){
            fprintf(stderr, "viki grep: could not register regexp()\n");
            sqlite3_close(db);
            return 1;
        }
        rc = viki_cmd_grep(db, argv[2], k, icase, zSrc, nChars);
        sqlite3_close(db);
        return rc;
    }

    if( strcmp(sub, "muse") == 0 ){
        /* Undirected recall: no query, a random seed chunk, and the chunks
        ** that sit in the calibrated middle of that seed's similarity
        ** distribution -- see src/viki_muse.h. Note NO embedder is opened:
        ** musing never calls viki_embed(), because the "query" is a chunk
        ** whose vector is already in the cache, and measured, loading the
        ** ONNX model costs ~72 ms against ~41 ms for the whole rest of the
        ** command. --model-id is the escape hatch for a multi-epoch cache
        ** that the embedder used to provide. */
        sqlite3 *db;
        viki_muse_opts opts;
        int i;
        viki_muse_defaults(&opts);
        for( i = 2; i < argc; i++ ){
            if( strcmp(argv[i], "--k") == 0 && i + 1 < argc ) opts.nResults = atoi(argv[++i]);
            else if( strcmp(argv[i], "--seed") == 0 && i + 1 < argc )
                opts.seed = strtoull(argv[++i], NULL, 10);
            else if( strcmp(argv[i], "--chars") == 0 && i + 1 < argc ) opts.nChars = atoi(argv[++i]);
            else if( strcmp(argv[i], "--model-id") == 0 && i + 1 < argc ) opts.zModelId = argv[++i];
            else if( strcmp(argv[i], "--bias") == 0 && i + 1 < argc ){
                const char *b = argv[++i];
                if( strcmp(b, "old") == 0 ) opts.bias = VIKI_MUSE_BIAS_OLD;
                else if( strcmp(b, "none") != 0 ){
                    fprintf(stderr, "viki muse: unknown --bias '%s' (want none|old)\n", b);
                    return 1;
                }
            }
            else if( strcmp(argv[i], "--same-source") == 0 ) opts.allowSameSource = 1;
            else if( strcmp(argv[i], "--from") == 0 && i + 1 < argc ){
                /* --from <64hex>#<ix>: muse FROM a named chunk instead of a
                ** random one, so an agent can follow a chain of association
                ** out of a previous muse hit or a `viki ask` result. The
                ** hash is matched in full, never by prefix -- a prefix that
                ** silently matched the wrong chunk would be worse than a
                ** clean miss, and viki_muse.c says so when it misses. */
                static char hbuf[65];
                const char *a = argv[++i];
                const char *hash = strchr(a, '#');
                size_t nh = hash ? (size_t)(hash - a) : strlen(a);
                if( nh > 64 ) nh = 64;
                memcpy(hbuf, a, nh); hbuf[nh] = 0;
                opts.zSeedHash = hbuf;
                opts.seedIx = hash ? atoi(hash + 1) : 0;
            }
            else {
                fprintf(stderr, "viki muse: unknown option '%s'\n", argv[i]);
                return 1;
            }
        }
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        rc = viki_cmd_muse(db, &opts, NULL);
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
        /* Flag-vs-path is decided by exact match on "--no-model" rather than
        ** by position: before the model leg existed argv[3] was unconditionally
        ** a db path, so `viki cache push --no-model` would have silently pushed
        ** a cache db named "--no-model" (i.e. failed to open it) instead of
        ** honouring the opt-out. Last non-flag argument wins as the path. */
        const char *dbPath = VIKI_DEFAULT_CACHE_DB;
        unsigned mFlags = 0;
        int i;
        if( argc < 3 ){ fprintf(stderr, "usage: viki cache push|pull [db-path] [--no-model]\n"); return 1; }
        for( i = 3; i < argc; i++ ){
            if( strcmp(argv[i], "--no-model") == 0 ) mFlags |= VIKI_CACHE_NO_MODEL;
            else dbPath = argv[i];
        }
        if( ensure_viki_dir() ) return 1;
        if( strcmp(argv[2], "push") == 0 ) return viki_cmd_cache_push_opts(dbPath, mFlags);
        if( strcmp(argv[2], "pull") == 0 ) return viki_cmd_cache_pull_opts(dbPath, mFlags);
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
        ** not crash". argv[2] is the model dir; with none given, the same
        ** directory `ask`/`index`/`cache pull` use (viki_model_dir()), so a
        ** selftest without arguments tests the model viki would actually run. */
        const char *modelDir = argc > 2 ? argv[2] : viki_model_dir();
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

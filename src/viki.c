/*
** viki.c -- CLI entry point and subcommand dispatch.
**
** Milestone 1 skeleton (KICKOFF.md). Real, working end to end for the
** BM25-only ("rung 0") path; the ONNX embedding pipeline (rung 1/2) is
** not implemented -- see FINDINGS.md. `viki ask` is honest about this
** (prints a degraded-mode notice) rather than faking hybrid retrieval.
*/
#include "viki_db.h"
#include "viki_index.h"
#include "viki_ask.h"
#include "viki_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define VIKI_VERSION "0.1.0-m1-skeleton"

static void usage(void){
    fprintf(stderr,
        "usage: viki <subcommand> [args...]\n\n"
        "  index [dir]              Walk dir (default: .) and (re)index into the local cache\n"
        "  ask \"<query>\" [--k N]    BM25 top-N search (default N=5); degraded mode, see FINDINGS.md\n"
        "  cache push [db-path]     Publish the local cache db as a fossil uv blob (default: .viki/cache.db)\n"
        "  cache pull [db-path]     Fetch the cache db from a fossil uv blob\n"
        "  version                  Print version\n"
        "  help                     Show this message\n"
        "\n"
        "All subcommands except 'version'/'help' open (creating if needed) the local\n"
        "cache db at .viki/cache.db, relative to the current directory. Run from within\n"
        "a fossil-see checkout.\n");
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
        const char *dir = argc > 2 ? argv[2] : ".";
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        rc = viki_cmd_index(db, dir);
        sqlite3_close(db);
        return rc;
    }

    if( strcmp(sub, "ask") == 0 ){
        sqlite3 *db;
        int k = 5;
        int i;
        if( argc < 3 ){ fprintf(stderr, "usage: viki ask \"<query>\" [--k N]\n"); return 1; }
        for( i = 3; i < argc - 1; i++ ){
            if( strcmp(argv[i], "--k") == 0 ) k = atoi(argv[i+1]);
        }
        if( ensure_viki_dir() ) return 1;
        if( viki_db_open(VIKI_DEFAULT_CACHE_DB, &db) != SQLITE_OK ) return 1;
        rc = viki_cmd_ask(db, argv[2], k);
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

    fprintf(stderr, "viki: unknown subcommand '%s'\n\n", sub);
    usage();
    return 1;
}

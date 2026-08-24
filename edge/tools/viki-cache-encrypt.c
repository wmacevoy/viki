/* viki-cache-encrypt -- convert a plaintext .viki/cache.db into a SQLCipher
** database, or back.
**
** This is a PUSHING PEER's job, not the edge's. The edge opens its cache
** READ-ONLY and never holds a writable handle, so it cannot and must not
** perform this conversion; a laptop that has the corpus encrypts before
** publishing, and the phone only ever decrypts to read. Keeping the
** capability on this side is what makes SQLITE_OPEN_READONLY on the edge a
** structural guarantee rather than a convention.
**
** Uses SQLCipher's own sqlcipher_export(), which copies schema and contents
** through the codec rather than copying bytes -- the only correct way, since
** the page format differs.
**
** Build (native, against the SQLCipher amalgamation + LibreSSL):
**   cc -O2 -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
**      -DSQLITE_EXTRA_INIT=sqlcipher_extra_init \
**      -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown \
**      -DSQLITE_ENABLE_FTS5 -DSQLITE_THREADSAFE=1 \
**      -I<sqlcipher> -I<libressl>/include \
**      viki-cache-encrypt.c <sqlcipher>/sqlite3.c <libressl>/lib/libcrypto.a \
**      -o viki-cache-encrypt
**
** A raw x'<64 hex>' key is used by SQLCipher directly and skips PBKDF2
** entirely; a passphrase costs ~346ms per open against ~6ms (FINDINGS.md).
** For a machine-to-machine tribe key, use the raw form. */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

static int run(sqlite3 *db, const char *zSql){
    char *err = NULL;
    if( sqlite3_exec(db, zSql, NULL, NULL, &err) != SQLITE_OK ){
        fprintf(stderr, "viki-cache-encrypt: %s\n", err ? err : "(no message)");
        sqlite3_free(err);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv){
    sqlite3 *db = NULL;
    char *zSql = NULL;
    int rc = 1, decrypt = 0;
    const char *zSrc, *zDst, *zKey;

    if( argc == 5 && strcmp(argv[1], "--decrypt") == 0 ){
        decrypt = 1; zSrc = argv[2]; zDst = argv[3]; zKey = argv[4];
    }else if( argc == 4 ){
        zSrc = argv[1]; zDst = argv[2]; zKey = argv[3];
    }else{
        fprintf(stderr,
          "usage: viki-cache-encrypt [--decrypt] <src.db> <dst.db> <key>\n"
          "   key: x'<64 hex>' for a machine key (no PBKDF2), or a passphrase\n");
        return 2;
    }

    /* READWRITE, not READONLY, and it is not a mistake: ATTACH inherits the
    ** main connection's access mode, so a read-only handle cannot CREATE the
    ** destination and the conversion fails with "unable to open database".
    ** Nothing is ever written to the source -- sqlcipher_export() reads it and
    ** writes only to `out`. */
    /* CREATE is required too: ATTACH can only create the destination file if
    ** the MAIN connection was opened with it. The existence check above keeps
    ** that from silently conjuring an empty source out of a typo'd path. */
    {   /* refuse a missing source rather than creating one */
        FILE *f = fopen(zSrc, "rb");
        if( !f ){ fprintf(stderr, "viki-cache-encrypt: no such source: %s\n", zSrc); return 1; }
        fclose(f);
    }
    if( sqlite3_open_v2(zSrc, &db, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki-cache-encrypt: cannot open %s\n", zSrc);
        goto done;
    }
    /* When DECRYPTING, the source is the encrypted one and needs the key
    ** before any statement runs; when encrypting, the source is plaintext
    ** and the DESTINATION carries the key. */
    if( decrypt ){
        zSql = sqlite3_mprintf("PRAGMA key = %Q;", zKey);
        if( run(db, zSql) ) goto done;
        sqlite3_free(zSql);
        zSql = sqlite3_mprintf("ATTACH DATABASE %Q AS out KEY '';", zDst);
    }else{
        zSql = sqlite3_mprintf("ATTACH DATABASE %Q AS out KEY %Q;", zDst, zKey);
    }
    if( run(db, zSql) ) goto done;
    sqlite3_free(zSql); zSql = NULL;

    if( run(db, "SELECT sqlcipher_export('out');") ) goto done;
    if( run(db, "DETACH DATABASE out;") ) goto done;
    printf("viki-cache-encrypt: %s -> %s (%s)\n", zSrc, zDst,
           decrypt ? "decrypted" : "encrypted");
    rc = 0;

done:
    sqlite3_free(zSql);
    if( db ) sqlite3_close(db);
    return rc;
}

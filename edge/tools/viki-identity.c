/* viki-identity -- the device's identity store.
**
** identity.db is a SQLCipher database under a PUBLICLY KNOWN key ("1"). That
** is deliberate and it is not security theatre: SQLCipher applies a per-page
** HMAC even under a known key, so the container gets TAMPER DETECTION for
** free, and every database this project touches stays one code path. The
** secrecy lives entirely in the per-identity wrapping below. Anyone tempted
** to "simplify" this into a plain SQLite file should know they would be
** trading away the integrity check for nothing (QUEUE 49).
**
** EACH PRIVATE KEY IS WRAPPED SEPARATELY, so one device can hold several
** identities (a human, an agent, a recovery key) with different passphrases
** and no shared unlock.
**
** THE FACTOR STRUCTURE IS THE POINT, and it is built to take a second factor
** even though today it ships with one:
**
**     unlock_key = HKDF( PBKDF2(passphrase, salt, iters) || device_secret,
**                        info = "viki-identity-v1" )
**
** `device_secret` is empty today and the `factors` column records that. When
** a platform secret is available -- Secure Enclave / Keychain, Android
** Keystore, TPM, or WebAuthn PRF in a browser -- it mixes in here and the
** column says so, with NO format change and no re-wrap of anything else.
** That matters because the single-factor version is attackable OFFLINE AT
** UNLIMITED SPEED once identity.db is copied: LibreSSL exposes PBKDF2 only,
** and PBKDF2 has no memory hardness, so GPUs parallelise it cheaply. Adding
** the device factor turns "copy the file and grind" into "possess the
** device", which is a change of kind. See QUEUE 49.
**
** Usage:
**   viki-identity init                      [--db identity.db]
**   viki-identity add <name>                [--db ...]   passphrase on stdin
**   viki-identity list                      [--db ...]
**   viki-identity pub <name>                [--db ...]
**   viki-identity unwrap <name> -i f.age [-o out]        passphrase on stdin
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include "sqlite3.h"

#define VIKI_AGE_NO_MAIN 1
#include "viki-key-wrap.c"

#define ID_DB_KEY   "1"
#define KDF_ITERS   600000       /* OWASP's PBKDF2-HMAC-SHA256 floor, 2023 */
#define SALT_LEN    16
#define WRAP_LEN    (X25519_LEN + TAG_LEN)

static const char *SCHEMA =
    "CREATE TABLE IF NOT EXISTS identity("
    "  name     TEXT PRIMARY KEY,"
    "  pubkey   TEXT NOT NULL,"      /* age1... -- public, stored in the clear */
    "  wrapped  BLOB NOT NULL,"      /* the 32-byte secret, AEAD-wrapped */
    "  salt     BLOB NOT NULL,"
    "  iters    INTEGER NOT NULL,"
    /* Which factors the unlock key was derived from. "passphrase" today;
    ** "passphrase+device" when a platform secret is mixed in. Recorded so an
    ** unlocking client knows what to ASK FOR rather than guessing, and so a
    ** future migration can find the single-factor rows. */
    "  factors  TEXT NOT NULL DEFAULT 'passphrase',"
    "  created  TEXT NOT NULL"
    ");"
    /* THE TRIBE REGISTRY. What the vikiverse is, from this device's point of
    ** view: which tribes exist, where to pull each from, and which identity
    ** can open it.
    **
    ** `wrapped` is the tribe's SQLCipher key, age-wrapped to one or more of
    ** the identities above -- so identity.db NEVER HOLDS A TRIBE KEY IN THE
    ** CLEAR. Reading one costs the owning identity's passphrase, and a device
    ** that holds two tribes cannot open the second by virtue of holding the
    ** first. That is the property that makes "my projects" and "a tribe I was
    ** given access to" safe on the same phone, and it is why the registry
    ** lives HERE rather than in a config file next to the caches.
    **
    ** `caching` is VIKIVERSE.md's tier: none | optional | required. It is
    ** advisory -- it says what this device INTENDS to keep, which is what a
    ** puller needs to know before it spends a phone's storage. */
    "CREATE TABLE IF NOT EXISTS tribe("
    "  name     TEXT PRIMARY KEY,"
    "  url      TEXT,"            /* hub base URL; /uv/ under it serves the cache */
    "  cache    TEXT,"            /* local cache file name */
    "  wrapped  BLOB NOT NULL,"   /* the tribe key, age-wrapped to `identity` */
    "  identity TEXT NOT NULL,"   /* which identity unwraps it */
    "  caching  TEXT NOT NULL DEFAULT 'optional',"
    "  added    TEXT NOT NULL"
    ");";

static sqlite3 *open_db(const char *zPath, int bCreate){
    sqlite3 *db = NULL;
    int flags = SQLITE_OPEN_READWRITE | (bCreate ? SQLITE_OPEN_CREATE : 0);
    if( sqlite3_open_v2(zPath, &db, flags, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki-identity: cannot open %s\n", zPath);
        exit(1);
    }
    /* The key must be applied before any statement runs. */
    if( sqlite3_key(db, ID_DB_KEY, (int)strlen(ID_DB_KEY)) != SQLITE_OK ) die("keying failed");
    if( bCreate && sqlite3_exec(db, SCHEMA, NULL, NULL, NULL) != SQLITE_OK )
        die("schema init failed");
    if( sqlite3_exec(db, "SELECT count(*) FROM identity", NULL, NULL, NULL) != SQLITE_OK )
        die("not a viki identity.db (or it is damaged)");
    /* An identity.db predating the registry has no `tribe` table; creating it
    ** on open is the migration, and it is safe because the table is additive
    ** and empty. */
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS tribe("
                     "  name TEXT PRIMARY KEY, url TEXT, cache TEXT,"
                     "  wrapped BLOB NOT NULL, identity TEXT NOT NULL,"
                     "  caching TEXT NOT NULL DEFAULT 'optional', added TEXT NOT NULL)",
                 NULL, NULL, NULL);
    return db;
}

/* Reads a passphrase from stdin with echo off when stdin is a terminal, and
** plainly when it is a pipe -- so scripts and tests work without a tty. */
static void read_pass(const char *zPrompt, char *out, size_t cap){
    struct termios old, noecho;
    int tty = isatty(STDIN_FILENO);
    if( tty ){
        fprintf(stderr, "%s", zPrompt);
        tcgetattr(STDIN_FILENO, &old);
        noecho = old; noecho.c_lflag &= ~(unsigned)ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &noecho);
    }
    if( !fgets(out, (int)cap, stdin) ) out[0] = 0;
    if( tty ){ tcsetattr(STDIN_FILENO, TCSAFLUSH, &old); fprintf(stderr, "\n"); }
    { size_t n = strlen(out); while( n && (out[n-1]=='\n' || out[n-1]=='\r') ) out[--n] = 0; }
    if( !out[0] ) die("empty passphrase");
}

static void unlock_key(const char *zPass, const unsigned char *salt, int iters,
                       const unsigned char *devSecret, int devLen, unsigned char *out32){
    unsigned char pbk[32], ikm[64];
    if( PKCS5_PBKDF2_HMAC(zPass, (int)strlen(zPass), salt, SALT_LEN, iters,
                          EVP_sha256(), 32, pbk) != 1 ) die("PBKDF2 failed");
    memcpy(ikm, pbk, 32);
    if( devLen > 0 ) memcpy(ikm + 32, devSecret, (size_t)devLen);
    hkdf(salt, SALT_LEN, ikm, 32 + devLen, "viki-identity-v1", out32, 32);
}

static int cmd_add(const char *zDb, const char *zName){
    sqlite3 *db = open_db(zDb, 1);
    unsigned char sk[X25519_LEN], pk[X25519_LEN], salt[SALT_LEN], uk[32];
    unsigned char wrapped[WRAP_LEN], nonce[12];
    char pass[512], pub[128], iso[32];
    sqlite3_stmt *st;
    time_t now = time(NULL);

    if( RAND_bytes(sk, X25519_LEN) != 1 || RAND_bytes(salt, SALT_LEN) != 1 )
        die("RAND_bytes failed");
    if( !x25519_pub(sk, pk) ) die("x25519 failed");
    bech_encode("age", pk, X25519_LEN, pub);

    read_pass("passphrase: ", pass, sizeof(pass));
    unlock_key(pass, salt, KDF_ITERS, NULL, 0, uk);
    memset(pass, 0, sizeof(pass));
    memset(nonce, 0, sizeof(nonce));      /* salt is unique per identity */
    if( aead(1, uk, nonce, sk, X25519_LEN, wrapped) != WRAP_LEN ) die("wrap failed");
    memset(sk, 0, sizeof(sk)); memset(uk, 0, sizeof(uk));

    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    if( sqlite3_prepare_v2(db,
        "INSERT INTO identity(name,pubkey,wrapped,salt,iters,factors,created)"
        " VALUES(?1,?2,?3,?4,?5,'passphrase',?6)", -1, &st, NULL) != SQLITE_OK )
        die("prepare failed");
    sqlite3_bind_text(st, 1, zName, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, pub, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 3, wrapped, WRAP_LEN, SQLITE_STATIC);
    sqlite3_bind_blob(st, 4, salt, SALT_LEN, SQLITE_STATIC);
    sqlite3_bind_int(st, 5, KDF_ITERS);
    sqlite3_bind_text(st, 6, iso, -1, SQLITE_STATIC);
    if( sqlite3_step(st) != SQLITE_DONE )
        fprintf(stderr, "viki-identity: %s\n", sqlite3_errmsg(db)), exit(1);
    sqlite3_finalize(st);
    sqlite3_close(db);
    printf("%s\n", pub);
    return 0;
}

static int load_secret(const char *zDb, const char *zName, unsigned char *sk){
    sqlite3 *db = open_db(zDb, 0);
    sqlite3_stmt *st;
    char pass[512];
    int ok = 0;
    if( sqlite3_prepare_v2(db, "SELECT wrapped,salt,iters FROM identity WHERE name=?1",
                           -1, &st, NULL) != SQLITE_OK ) die("prepare failed");
    sqlite3_bind_text(st, 1, zName, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ){
        const unsigned char *w = sqlite3_column_blob(st, 0);
        const unsigned char *s = sqlite3_column_blob(st, 1);
        int iters = sqlite3_column_int(st, 2);
        unsigned char uk[32], nonce[12];
        read_pass("passphrase: ", pass, sizeof(pass));
        unlock_key(pass, s, iters, NULL, 0, uk);
        memset(pass, 0, sizeof(pass));
        memset(nonce, 0, sizeof(nonce));
        ok = aead(0, uk, nonce, w, WRAP_LEN, sk) == X25519_LEN;
        memset(uk, 0, sizeof(uk));
    }else{
        fprintf(stderr, "viki-identity: no such identity: %s\n", zName);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    /* A wrong passphrase fails the Poly1305 tag -- it is not a guess. */
    if( !ok ) die("wrong passphrase, or the record is damaged");
    return 0;
}

/* Looks up an identity's public key. */
static int identity_pub(sqlite3 *db, const char *zName, char *out, size_t cap){
    sqlite3_stmt *st; int ok = 0;
    if( sqlite3_prepare_v2(db, "SELECT pubkey FROM identity WHERE name=?1", -1, &st, NULL) != SQLITE_OK )
        return 0;
    sqlite3_bind_text(st, 1, zName, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ){
        snprintf(out, cap, "%s", (const char*)sqlite3_column_text(st, 0));
        ok = 1;
    }
    sqlite3_finalize(st);
    return ok;
}

static int cmd_tribe_add(const char *zDb, const char *zName, const char *zIdent,
                          const char *zKeyFile, const char *zUrl, const char *zCache,
                          const char *zCaching){
    sqlite3 *db = open_db(zDb, 1);
    char pub[128], iso[32];
    unsigned char keyBuf[512], wrapped[8192];
    int keyLen = 0, wlen = 0;
    char *recips[1];
    sqlite3_stmt *st;
    time_t now = time(NULL);
    FILE *f;

    if( !identity_pub(db, zIdent, pub, sizeof(pub)) ) die("no such identity");
    f = zKeyFile ? fopen(zKeyFile, "rb") : stdin;
    if( !f ) die("cannot read the tribe key");
    keyLen = (int)fread(keyBuf, 1, sizeof(keyBuf) - 1, f);
    if( zKeyFile ) fclose(f);
    while( keyLen > 0 && (keyBuf[keyLen-1] == '\n' || keyBuf[keyLen-1] == '\r') ) keyLen--;
    if( keyLen <= 0 ) die("empty tribe key");

    recips[0] = pub;
    if( !age_wrap_mem(recips, 1, keyBuf, keyLen, wrapped, &wlen, (int)sizeof(wrapped)) )
        die("wrapping the tribe key failed");
    memset(keyBuf, 0, sizeof(keyBuf));

    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    if( sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO tribe(name,url,cache,wrapped,identity,caching,added)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7)", -1, &st, NULL) != SQLITE_OK ) die("prepare failed");
    sqlite3_bind_text(st, 1, zName, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, zUrl, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, zCache ? zCache : zName, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 4, wrapped, wlen, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, zIdent, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, zCaching ? zCaching : "optional", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 7, iso, -1, SQLITE_STATIC);
    if( sqlite3_step(st) != SQLITE_DONE ) die(sqlite3_errmsg(db));
    sqlite3_finalize(st);
    sqlite3_close(db);
    printf("%s wrapped to %s (%s)\n", zName, zIdent, pub);
    return 0;
}

/* Prints the tribe key. Costs the owning identity's passphrase every time --
** there is no cached plaintext anywhere, which is the whole point. */
static int cmd_tribe_key(const char *zDb, const char *zName){
    sqlite3 *db = open_db(zDb, 0);
    sqlite3_stmt *st;
    char ident[128] = "";
    unsigned char wrapped[8192], sk[X25519_LEN], out[512];
    int wlen = 0, olen = 0, found = 0;

    if( sqlite3_prepare_v2(db, "SELECT wrapped,identity FROM tribe WHERE name=?1",
                           -1, &st, NULL) != SQLITE_OK ) die("prepare failed");
    sqlite3_bind_text(st, 1, zName, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ){
        const unsigned char *w = sqlite3_column_blob(st, 0);
        wlen = sqlite3_column_bytes(st, 0);
        if( wlen > 0 && wlen <= (int)sizeof(wrapped) ){
            memcpy(wrapped, w, (size_t)wlen);
            snprintf(ident, sizeof(ident), "%s", (const char*)sqlite3_column_text(st, 1));
            found = 1;
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    if( !found ) die("no such tribe");

    load_secret(zDb, ident, sk);
    if( !age_unwrap_mem(sk, wrapped, wlen, out, &olen, (int)sizeof(out)) ){
        memset(sk, 0, sizeof(sk));
        die("the wrapped tribe key did not open with that identity");
    }
    memset(sk, 0, sizeof(sk));
    fwrite(out, 1, (size_t)olen, stdout);
    fputc('\n', stdout);
    memset(out, 0, sizeof(out));
    return 0;
}

static int cmd_tribe_list(const char *zDb){
    sqlite3 *db = open_db(zDb, 0);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT name,caching,identity,coalesce(url,''),coalesce(cache,'') "
        "FROM tribe ORDER BY name", -1, &st, NULL);
    while( sqlite3_step(st) == SQLITE_ROW ){
        printf("%-20s %-9s via %-14s %s%s%s\n",
               sqlite3_column_text(st,0), sqlite3_column_text(st,1),
               sqlite3_column_text(st,2), sqlite3_column_text(st,3),
               sqlite3_column_text(st,4)[0] ? "  cache=" : "",
               sqlite3_column_text(st,4));
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return 0;
}

int main(int argc, char **argv){
    const char *zDb = "identity.db", *zName = NULL, *zIn = NULL, *zOut = NULL;
    int i;
    if( argc < 2 ){
        fprintf(stderr,
          "usage: viki-identity init|list [--db f]\n"
          "       viki-identity add|pub <name> [--db f]\n"
          "       viki-identity unwrap <name> -i file.age [-o out] [--db f]\n"
          "       viki-identity tribe add <tribe> -r <identity> [--key-file f]\n"
          "                           [--url U] [--cache C] [--caching none|optional|required]\n"
          "       viki-identity tribe key <tribe>        (prints it; costs a passphrase)\n"
          "       viki-identity tribe list\n"
          "Public keys are age recipients; files are age v1 (`age -d` reads them).\n");
        return 2;
    }
    /* `tribe` parses its OWN flags (-r, --key-file, --url, --caching), which
    ** the generic loop below would reject as unknown, so it dispatches first. */
    if( !strcmp(argv[1], "tribe") ){
        const char *sub = argc > 2 ? argv[2] : "";
        const char *zIdent = NULL, *zKeyFile = NULL, *zUrl = NULL, *zCache = NULL, *zCaching = NULL;
        const char *zTribe = NULL;
        int j;
        for( j = 3; j < argc; j++ ){
            if( !strcmp(argv[j], "--db") && j+1 < argc ) zDb = argv[++j];
            else if( !strcmp(argv[j], "-r") && j+1 < argc ) zIdent = argv[++j];
            else if( !strcmp(argv[j], "--key-file") && j+1 < argc ) zKeyFile = argv[++j];
            else if( !strcmp(argv[j], "--url") && j+1 < argc ) zUrl = argv[++j];
            else if( !strcmp(argv[j], "--cache") && j+1 < argc ) zCache = argv[++j];
            else if( !strcmp(argv[j], "--caching") && j+1 < argc ) zCaching = argv[++j];
            else if( argv[j][0] != '-' && !zTribe ) zTribe = argv[j];
        }
        if( !strcmp(sub, "list") ) return cmd_tribe_list(zDb);
        if( !zTribe ) die("tribe add/key needs a <name>");
        if( !strcmp(sub, "add") ){
            if( !zIdent ) die("tribe add needs -r <identity>");
            return cmd_tribe_add(zDb, zTribe, zIdent, zKeyFile, zUrl, zCache, zCaching);
        }
        if( !strcmp(sub, "key") ) return cmd_tribe_key(zDb, zTribe);
        die("tribe: expected add | key | list");
    }
    for( i = 2; i < argc; i++ ){
        if( !strcmp(argv[i], "--db") && i+1 < argc ) zDb = argv[++i];
        else if( !strcmp(argv[i], "-i") && i+1 < argc ) zIn = argv[++i];
        else if( !strcmp(argv[i], "-o") && i+1 < argc ) zOut = argv[++i];
        else if( argv[i][0] != '-' && !zName ) zName = argv[i];
        else die("unknown option");
    }
    if( !strcmp(argv[1], "init") ){ sqlite3_close(open_db(zDb, 1)); printf("%s\n", zDb); return 0; }
    if( !strcmp(argv[1], "list") ){
        sqlite3 *db = open_db(zDb, 0); sqlite3_stmt *st;
        sqlite3_prepare_v2(db, "SELECT name,pubkey,factors,created FROM identity ORDER BY name",
                           -1, &st, NULL);
        while( sqlite3_step(st) == SQLITE_ROW )
            printf("%-16s %s  [%s]  %s\n", sqlite3_column_text(st,0), sqlite3_column_text(st,1),
                   sqlite3_column_text(st,2), sqlite3_column_text(st,3));
        sqlite3_finalize(st); sqlite3_close(db); return 0;
    }
    if( !zName ) die("that command needs a <name>");
    if( !strcmp(argv[1], "add") ) return cmd_add(zDb, zName);
    if( !strcmp(argv[1], "pub") ){
        sqlite3 *db = open_db(zDb, 0); sqlite3_stmt *st; int found = 0;
        sqlite3_prepare_v2(db, "SELECT pubkey FROM identity WHERE name=?1", -1, &st, NULL);
        sqlite3_bind_text(st, 1, zName, -1, SQLITE_STATIC);
        if( sqlite3_step(st) == SQLITE_ROW ){ printf("%s\n", sqlite3_column_text(st,0)); found = 1; }
        sqlite3_finalize(st); sqlite3_close(db);
        if( !found ) die("no such identity");
        return 0;
    }
    if( !strcmp(argv[1], "unwrap") ){
        unsigned char sk[X25519_LEN]; char sec[128]; char *p;
        if( !zIn ) die("unwrap needs -i");
        load_secret(zDb, zName, sk);
        bech_encode("AGE-SECRET-KEY-", sk, X25519_LEN, sec);
        for( p = sec; *p; p++ ) if( *p >= 'a' && *p <= 'z' ) *p = (char)(*p - 'a' + 'A');
        memset(sk, 0, sizeof(sk));
        { int rc = cmd_unwrap(sec, zIn, zOut); memset(sec, 0, sizeof(sec)); return rc; }
    }
    die("unknown command");
    return 2;
}

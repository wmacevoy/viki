/* strdup is POSIX. Under -std=c11 glibc hides it, gcc assumes int, and the
** returned pointer is truncated to 32 bits -- a segfault that macOS never
** shows because its headers expose strdup regardless. The host is allowed
** POSIX; it just has to ask. */
#define _POSIX_C_SOURCE 200809L
/*
** viki_cli.c -- the CLI face. A BINDING, not an implementation.
**
** THE CLI IS THE HOST, and that is the point of it existing. It does the
** filesystem: resolves a path, opens the sqlite3*, supplies the key. core
** never learns any of that. Every verb below is a thin call into viki-core,
** so the CLI and any other face provably cannot disagree about an answer.
**
** ---------------------------------------------------------------------
** `viki run CMD` -- ONE WARM CONTEXT FOR A WHOLE COMMAND
**
** Measured, because the obvious justification is wrong: opening the store and
** answering a query costs 0.93 ms, and a bare fork+exec costs 1.28 ms. For a
** plaintext store with no model, a warm context SAVES NOTHING -- the process
** spawn dominates it.
**
** What it actually holds is the two things that are expensive and that a
** child must never have to redo:
**
**     SQLCipher with a PASSPHRASE   345.93 ms   (PBKDF2; FINDINGS.md)
**     SQLCipher with a raw key        5.99 ms
**     an ONNX model load            100-500 ms  (typical)
**
** So `viki run` is not "fast fork". It is "pay for the key and the model
** once, and never hand either to a child".
**
** THE TRANSPORT IS A UNIX SOCKET, NOT A LOOPBACK PORT, and this follows from
** what the parent holds. After `viki run`, the parent has the DECRYPTED store
** and the loaded model; anything that can reach the socket can read the whole
** memory without the passphrase. A loopback TCP port -- v6 or v4 -- is
** reachable by EVERY process of EVERY user on the machine, so the guard would
** have to be a token in the URL, and a token in an environment variable is
** readable from /proc on Linux and leaks into any child's `env` output.
**
** A unix socket in a 0700 directory is guarded by the filesystem, needs no
** token, does not appear in netstat, and does not traverse the network stack
** at all. It is both stricter and faster. The socket is unlinked on exit.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include "viki_core.h"
#include "viki_task.h"
#include "viki_trace.h"
#include "viki_vcs.h"
#include "viki_cal.h"
#include "sha256.h"
#include <dlfcn.h>
#include <fcntl.h>

#define VIKI_CTX_ENV "VIKI_CONTEXT"

static const char *zProg = "viki";

static int usage(void){
    fprintf(stderr,
"usage: %s [--store PATH] <command>\n"
"\n"
"  note TEXT             remember something.  --supersedes ID retires a\n"
"                        task: an arrival is a RECORD, not a new debt\n"
"  task TEXT             ...that someone owes, by a date.\n"
"                        --who W --due YYYY-MM-DD --place P --state S\n"
"                        --channel C --supersedes ID\n"
"  ledger [--me NAME]    what is owed, to whom, by when. LIVE only:\n"
"                        anything superseded has left.  --json\n"
"  coverage              which channels fed this diary, and when last\n"
"  claim TEXT            what a trace believes, and HOW SURE.  --status k0..k4\n"
"                        --falsified-by W (k0 only) --by WHO (ALWAYS required:\n"
"                        the compaction envelope stores summaries unattributed)\n"
"                        --supersedes ID --because WHY  retires an older claim\n"
"  why ID                the chain BOTH ways: what replaced this, and what\n"
"                        this replaced.  Newest first, so a reader meets the\n"
"                        correction before the thing corrected\n"
"  pending               captures never structured into a task\n"
"  redact ID --why W --by WHO   destroy an assertion HERE and on every peer\n"
"                        that merges. IRREVERSIBLE -- the id can never be\n"
"                        re-added. Unlike forget, which the next merge undoes.\n"
"                        The tombstone carries the id only, never the content.\n"
"  file PATH             a VERSION of a document. akey=path, so this\n"
"                        supersedes the current version of PATH; storing\n"
"                        unchanged content is a no-op.  --content TEXT |\n"
"                        --content-from FILE | stdin.  --who --ts\n"
"  checkin --comment C [--who W] [--branch B] [--parent ID] ID...\n"
"                        GROUP file versions atomically. Supersedes its\n"
"                        PARENT check-in, so `why` walks commit history.\n"
"                        Two children of one parent is a branch.\n"
"  clone DEST            checkpointed snapshot to a NEW file (VACUUM INTO).\n"
"                        A plain cp is WRONG -- journal_mode=wal means a copy\n"
"                        taken while anything holds the store drops committed\n"
"                        rows still in -wal. The clone is HISTORICAL on\n"
"                        arrival; it bounds the first merge, never replaces it.\n"
"  observe               every assertion id here, sorted -- a MANIFEST.\n"
"                        --lacking FILE   ids here that FILE does not list.\n"
"                        --after N        ids that reached HERE after seq N\n"
"                        --seq            this diary's own arrival clock\n"
"                        Anti-entropy, not a log tail: the store\n"
"                        keeps no arrival order and a peer's rows carry the\n"
"                        peer's clock, so set difference is the only correct\n"
"                        question. Ids are content hashes; no clock needed.\n"
"  ask QUERY [-k N]      hybrid retrieval\n"
"  forget ID             withdraw an assertion (local; peers keep theirs)\n"
"  pull PATH             union a peer's store into this one\n"
"  push PATH             union this one into a peer's. REFUSES to create it:\n"
"                        first contact is `clone`. For a dumb hub the round\n"
"                        trip is PULL then PUSH -- push alone drops whatever\n"
"                        another peer left there since.\n"
"                        --source-keyfile PATH  if the peer uses another key\n"
"  merge PATH            same as pull, kept for the older spelling\n"
"  reindex               (re)project ranges; repeat with --lines/--overlap\n"
"                        to add a SECOND chunking over the same assertions\n"
"  cal ingest FILE       jsCalendar (RFC 8984) in\n"
"  model import DIR      ingest model.onnx, vocab.txt and the pinned dim\n"
"                        as blobs. Files are read HERE and never again.\n"
"  cal events [FROM TO]  the resolved tier\n"
"  count WHAT            assert|current|range|vector|chunking|model\n"
"  sql SELECT ...        the raw rung\n"
"  run CMD [ARGS...]     hold ONE warm context for CMD; every viki inside it\n"
"                        reuses it via $%s\n"
"\n"
"  --lines N --overlap N   chunking policy for reindex (default 40/10)\n"
"  --keyfile PATH          unlock an encrypted diary (mode 600). Else\n"
"                          $VIKI_KEY, else a prompt on a terminal.\n"
"                          64 hex chars is used as a RAW key (no KDF).\n"
"  --signer PATH           Ed25519 key file (mode 600): name, then 64 hex\n"
"  --embedder PATH.so      a library exporting the embedder ABI. It reads the\n"
"                          model from THIS DIARY -- import it once first.\n"
"  --source-keyfile PATH   key for `merge`'s source, if it differs\n"
"  --plaintext             open an UNENCRYPTED diary, deliberately\n"
"  --store PATH            the PRIVATE diary; writes land here.\n"
"                          default $VIKI_STORE, else ./viki.db\n"
"  --core PATH             a shared diary, opened READ-ONLY. Where a model\n"
"                          belongs -- it is the tribe's, not yours.\n"
"\n"
"  `run` retains BOTH for the whole command, so children get signed writes\n"
"  and hybrid retrieval while never holding a key or a model.\n",
        zProg, VIKI_CTX_ENV);
    return 2;
}

/* ---- the host's job: paths, keys and connections ---------------------
**
** ALL FOUR OF THESE ARE THE HOST'S, and that is why core never sees them:
** where the diary lives, which VFS opens it, what key unlocks it, and how
** that key was obtained. core takes an open sqlite3* and asks no questions.
**
** WHERE THE KEY MAY COME FROM, worst to best:
**
**   --key VALUE     REFUSED. argv is world-readable in `ps` on every system
**                   this runs on, and shell history keeps it afterwards.
**                   Refusing is not pedantry: a flag that exists WILL be used
**                   in a cron line.
**   $VIKI_KEY       accepted, with a warning. Readable from /proc on Linux
**                   and inherited by every child's environment -- but it is
**                   what CI needs, and refusing it only pushes people to a
**                   key file with worse permissions.
**   --keyfile PATH  the default answer. Permissions are checked the way ssh
**                   checks a private key, because a 0644 key file is the
**                   failure this option exists to prevent.
**   a prompt        when stdin is a terminal and nothing else was given.
**   the platform    Keychain, Secure Enclave, TPM. Not wired here; this is
**                   the seam where it goes, and it needs no core change.
**
** RAW KEY vs PASSPHRASE IS DETECTED, NOT CONFIGURED. Measured on this
** project: a raw x'<64 hex>' key opens an encrypted diary in 5.99 ms and a
** passphrase in 345.93 ms, because SQLCipher runs PBKDF2 for one and not the
** other. 64 hex characters is therefore treated as a raw key -- the caller
** who generated one should not have to say so, and should not silently pay
** 58x for it. */
static char *key_from_file(const char *zPath){
    struct stat st;
    FILE *f;
    char *z;
    size_t n;
    if( stat(zPath,&st)!=0 ){
        fprintf(stderr,"%s: cannot read %s: %s\n",zProg,zPath,strerror(errno));
        return 0;
    }
    if( st.st_mode & (S_IRWXG|S_IRWXO) ){
        fprintf(stderr,"%s: %s is group/world accessible (mode %04o) -- refusing.\n"
                       "%s: chmod 600 %s\n",
                zProg, zPath, (unsigned)(st.st_mode & 07777), zProg, zPath);
        return 0;
    }
    f = fopen(zPath,"rb");
    if( !f ) return 0;
    z = (char*)malloc(1024);
    if( !z ){ fclose(f); return 0; }
    n = fread(z,1,1023,f);
    fclose(f);
    z[n] = 0;
    while( n && (z[n-1]=='\n' || z[n-1]=='\r') ) z[--n] = 0;
    return z;
}

static char *key_prompt(void){
    struct termios old, quiet;
    char *z;
    size_t n;
    if( !isatty(STDIN_FILENO) ) return 0;
    fprintf(stderr,"passphrase for the diary: ");
    if( tcgetattr(STDIN_FILENO,&old)!=0 ) return 0;
    quiet = old; quiet.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&quiet);
    z = (char*)malloc(1024);
    if( z && !fgets(z,1024,stdin) ){ free(z); z = 0; }
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&old);
    fprintf(stderr,"\n");
    if( !z ) return 0;
    n = strlen(z);
    while( n && (z[n-1]=='\n' || z[n-1]=='\r') ) z[--n] = 0;
    if( !n ){ free(z); return 0; }
    return z;
}

static int is_raw_key(const char *z){
    int i;
    if( strlen(z)!=64 ) return 0;
    for(i=0;i<64;i++){
        char c = z[i];
        if( !((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) ) return 0;
    }
    return 1;
}

/* ENCRYPTED IS THE DEFAULT AND PLAINTEXT IS AN EXPLICIT ACT.
**
** The build already refuses stock SQLite, but that alone does not get you an
** encrypted diary -- it gets you a binary CAPABLE of one. If opening without
** a key just worked, every diary would be plaintext, because that is the
** path of least effort and nobody chooses it on purpose. So the default is
** to refuse, and --plaintext is how someone says they meant it. */
static int bPlaintext = 0;

/* Applies the key BEFORE anything else touches the connection: SQLCipher
** decrypts the header on the first read, so a PRAGMA key that arrives after
** any other statement is already too late. */
static const char *zSrcKeyFile = 0;

static int apply_key(sqlite3 *db, const char *zPath, const char *zKeyFile){
    char *zKey = 0;
    char *zSql;
    int rc, bWarn = 0;
    if( zKeyFile ) zKey = key_from_file(zKeyFile);
    else if( getenv("VIKI_KEY") ){ zKey = strdup(getenv("VIKI_KEY")); bWarn = 1; }
    else zKey = key_prompt();
    if( !zKey ){
        if( zKeyFile ) return -1;              /* named a key file, got nothing */
        if( bPlaintext ) return 0;             /* said so out loud */
        fprintf(stderr,
          "%s: refusing to open %s without a key.\n"
          "%s:   --keyfile PATH   unlock (or create) an encrypted diary\n"
          "%s:   $VIKI_KEY        same, for CI\n"
          "%s:   --plaintext      say you meant an UNENCRYPTED diary\n"
          "%s: a diary holds what someone told it in confidence.\n",
          zProg, zPath, zProg, zProg, zProg, zProg);
        return -1;
    }
    if( bWarn )
        fprintf(stderr,"%s: using $VIKI_KEY -- readable from /proc and inherited "
                       "by children; prefer --keyfile\n", zProg);
    zSql = is_raw_key(zKey)
         ? sqlite3_mprintf("PRAGMA key = \"x'%q'\"", zKey)   /* no KDF: 5.99 ms */
         : sqlite3_mprintf("PRAGMA key = %Q", zKey);          /* PBKDF2: ~346 ms */
    rc = zSql ? sqlite3_exec(db, zSql, 0, 0, 0) : SQLITE_NOMEM;
    /* wipe before free: the process may live a long time under `viki run` */
    memset(zKey, 0, strlen(zKey));
    free(zKey);
    if( zSql ){ memset(zSql, 0, strlen(zSql)); sqlite3_free(zSql); }
    if( rc!=SQLITE_OK ){
        fprintf(stderr,"%s: cannot key %s: %s\n",zProg,zPath,sqlite3_errmsg(db));
        return -1;
    }
    return 1;
}

static const char *zKeyFile  = 0;
static const char *zSignerFile = 0;
static const char *zEmbedLib = 0;

/* ---- the embedder seam ----------------------------------------------
**
** A MODEL IS A FILE AND FILES ARE THE HOST'S, so core takes a callback and
** the CLI dlopen()s whatever provides it. Three symbols, and the ABI is
** deliberately tiny because everything interesting -- ONNX Runtime, CoreML,
** a remote service -- fits behind it:
**
**   int  viki_embedder_open (const void *pModel, size_t n, int *pnDim, void **pp);
**   int  viki_embedder_embed(void *p, const char *zText, float *aOut, int nDim);
**   void viki_embedder_close(void *p);
**
** pModel is the model BLOB out of the diary, or NULL when the library knows
** where its own weights are. That is the shape that lets one model database
** serve several diaries without any of them carrying a copy. */
/* THE HOST RESOLVES NAMES TO BYTES. Where from is the host's business and
** that is the entire point: a diary blob works in wasm and inside a sandbox,
** a file works on a laptop, and the embedder is told neither. An earlier
** version passed $VIKI_MODEL_DIR, which is a filesystem assumption wearing a
** plugin's clothes -- there is no directory in two of the three places this
** must run. */
static char *slurp(const char *zPath, size_t *pn);   /* defined below */
typedef const void *(*viki_blob_fn)(void*, const char*, size_t*);
typedef int  (*fnEmbOpen )(viki_blob_fn, void*, int*, void**);
typedef int  (*fnEmbEmbed)(void*, const char*, float*, int);
typedef void (*fnEmbClose)(void*);
typedef struct {
    void *hLib; void *pApp;
    fnEmbEmbed xEmbed; fnEmbClose xClose;
} Embedder;

static int embed_thunk(void *pApp, const char *zText, float *aOut, int nDim){
    Embedder *e = (Embedder*)pApp;
    return e->xEmbed(e->pApp, zText, aOut, nDim);
}

/* THE DIARY IS THE ONLY SOURCE. There is no file fallback and that is the
** correction: reading model.onnx off disk at runtime is an INGEST step
** wearing a resolver's clothes. Files exist once, at `viki model import`, and
** after that a diary is self-contained -- which is what makes the wasm case
** and the sandboxed-robot case identical to the laptop case instead of a
** degraded version of it.
**
** Names are matched against a blob assertion's description prefix, so
** "model.onnx", "vocab.txt" and "dim" are ordinary blobs that merge, sync and
** are signed like anything else in the diary. */
static char *g_aHeld[4];
static int   g_nHeld = 0;

/* ACROSS EVERY OPEN DIARY, via core. The model belongs in `core` and the
** notes in `private`, so a resolver that knew one connection could only ever
** find a model sitting beside the notes -- which is not where it goes. */
static const void *cli_blob(void *pApp, const char *zName, size_t *pn){
    const void *p = 0; sqlite3_int64 n = 0; const char *zWhich = 0;
    (void)pApp;
    *pn = 0;
    if( viki_blob_find(zName, &p, &n, &zWhich)!=VIKI_OK || !p || n<=0 ){
        fprintf(stderr,"%s: no '%s' in any open diary -- viki model import DIR\n",
                zProg, zName);
        return 0;
    }
    /* COPIED: the pointer dies with core's held statement, and ORT keeps the
    ** graph for the life of the session. */
    if( g_nHeld >= 4 ) return 0;
    { char *z = (char*)malloc((size_t)n+1);
      if( !z ) return 0;
      memcpy(z, p, (size_t)n); z[n] = 0;
      g_aHeld[g_nHeld++] = z; *pn = (size_t)n;
      return z; }
}

static int embedder_open(const char *zPath, Embedder *e, VikiEmbed *pOut,
                         const char *zModelId){
    fnEmbOpen xOpen;
    memset(e, 0, sizeof *e);
    e->hLib = dlopen(zPath, RTLD_NOW);
    if( !e->hLib ){ fprintf(stderr,"%s: %s\n", zProg, dlerror()); return -1; }
    xOpen     = (fnEmbOpen )dlsym(e->hLib, "viki_embedder_open");
    e->xEmbed = (fnEmbEmbed)dlsym(e->hLib, "viki_embedder_embed");
    e->xClose = (fnEmbClose)dlsym(e->hLib, "viki_embedder_close");
    if( !xOpen || !e->xEmbed ){
        fprintf(stderr,"%s: %s does not export the embedder ABI\n", zProg, zPath);
        dlclose(e->hLib); e->hLib = 0; return -1;
    }
    pOut->nDim = 0;
    if( xOpen(cli_blob, 0, &pOut->nDim, &e->pApp)!=0 || pOut->nDim<=0 ){
        fprintf(stderr,"%s: %s failed to open its model\n", zProg, zPath);
        dlclose(e->hLib); e->hLib = 0; return -1;
    }
    pOut->xEmbed = embed_thunk;
    pOut->pApp   = e;
    pOut->zModel = zModelId;
    return 0;
}
static void embedder_close(Embedder *e){
    int i;
    if( e->hLib ){ if( e->xClose ) e->xClose(e->pApp); dlclose(e->hLib); e->hLib = 0; }
    /* held only as long as the session that borrowed them */
    for(i=0;i<g_nHeld;i++) free(g_aHeld[i]);
    g_nHeld = 0;
}

static sqlite3 *open_store_named(const char *zPath);
static sqlite3 *open_store(const char *zPath){
    sqlite3 *db = 0;
    if( sqlite3_open(zPath, &db)!=SQLITE_OK ){
        fprintf(stderr, "%s: cannot open %s: %s\n", zProg, zPath, sqlite3_errmsg(db));
        return 0;
    }
    if( apply_key(db, zPath, zKeyFile) < 0 ){ sqlite3_close(db); return 0; }
    /* THE FIRST REAL READ. A wrong key fails here, not at PRAGMA key -- which
    ** succeeds regardless, because it only sets the cipher context. Reporting
    ** it plainly beats letting core's schema creation fail with something
    ** that reads like corruption. */
    if( sqlite3_exec(db, "SELECT count(*) FROM sqlite_master", 0, 0, 0)!=SQLITE_OK ){
        fprintf(stderr,"%s: %s is not readable -- wrong key, or not a diary\n",
                zProg, zPath);
        sqlite3_close(db);
        return 0;
    }
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", 0, 0, 0);
    sqlite3_busy_timeout(db, 5000);
    if( viki_attach(db)!=VIKI_OK || viki_cal_attach(db)!=VIKI_OK ){
        fprintf(stderr, "%s: %s\n", zProg, viki_errmsg());
        sqlite3_close(db);
        return 0;
    }
    return db;
}

/* ---- printing -------------------------------------------------------- */
static int prHit(FILE *out, VikiHits *h){
    int i;
    if( !h ) return 0;
    if( h->bDegraded )
        fprintf(stderr, "%s: no embedder -- keyword and literal only\n", zProg);
    for(i=0;i<h->n;i++){
        /* THE CITABLE EXTENT IS THE RANGE. A caller can fetch exactly these
        ** bytes again; an ordinal would only be meaningful under the chunking
        ** that produced it. */
        fprintf(out, "[%d] %.4f  %.12s#%d-%d  (%s)\n", i+1, h->a[i].score,
               h->a[i].zId, h->a[i].lo, h->a[i].hi, h->a[i].zChunking);
        fprintf(out, "    %s\n", h->a[i].zText ? h->a[i].zText : "");
    }
    return h->n;
}
static int prSql(void *pApp, int nCol, const char *const *az){
    FILE *out = (FILE*)pApp; int i;
    for(i=0;i<nCol;i++) fprintf(out, "%s%s", i?"|":"", az[i]?az[i]:"");
    fprintf(out, "\n");
    return 0;
}
static int prCal(void *pApp, const VikiEvent *e){
    FILE *out = (FILE*)pApp;
    fprintf(out, "%-20s %-10s %s%s%s  %s\n",
           e->zDtstart ? e->zDtstart : "",
           e->zDtstartForm ? e->zDtstartForm : "",
           (e->zDtstartTzid && e->zDtstartTzid[0]) ? "[" : "",
           (e->zDtstartTzid && e->zDtstartTzid[0]) ? e->zDtstartTzid : "",
           (e->zDtstartTzid && e->zDtstartTzid[0]) ? "]" : "",
           e->zSummary ? e->zSummary : "");
    return 0;
}
static int prEach(void *pApp, const char *zId, const char *zKind,
                  const char *zKey, const char *zTs, const char *zBody, int bCur){
    FILE *out = (FILE*)pApp; (void)zKey;
    fprintf(out, "%.12s %-6s %-20s %s %s\n", zId, zKind, zTs ? zTs : "",
           bCur ? " " : "~", zBody ? zBody : "");
    return 0;
}

static char *slurp(const char *zPath, size_t *pn){
    FILE *f = fopen(zPath, "rb");
    char *z; long n;
    if( !f ) return 0;
    fseek(f,0,SEEK_END); n = ftell(f); fseek(f,0,SEEK_SET);
    if( n<0 ){ fclose(f); return 0; }
    z = (char*)malloc((size_t)n+1);
    if( z && fread(z,1,(size_t)n,f)!=(size_t)n ){ free(z); z=0; }
    if( z ) z[n]=0;
    fclose(f);
    if( pn ) *pn = (size_t)n;
    return z;
}


/* ---- the HOST owns the clock ------------------------------------------
** core calls time() exactly zero times, and that is deliberate: it has no
** filesystem, no network and no clock, so everything it stores is something
** it was TOLD. That makes a store reproducible from its inputs, and it makes
** the caller responsible for saying when. Nothing else in this CLI stamped
** anything, which is why every note written so far carries an empty ts. */
static const char *isoNow(void){
    static char z[32];
    time_t t = time(0);
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    strftime(z, sizeof(z), "%Y-%m-%dT%H:%M:%SZ", &g);
    return z;
}
static const char *isoToday(void){
    static char z[16];
    memcpy(z, isoNow(), 10); z[10] = 0;
    return z;
}

/* ---- printing the ledger ---------------------------------------------
** Facts only. Whether an overdue promise is worth waking someone about is
** the assistant's call, not this program's -- so the row says the date and
** the marker, and stops there. */
struct ledPr { FILE *out; int nMine, nTheirs, nOver, nToday, nUndated; };
static int ledRow(void *pApp, const VikiTaskRow *r){
    struct ledPr *c = (struct ledPr*)pApp;
    const char *zT = isoToday();
    const char *zMark = "";
    if( !r->zDue[0] )                       { c->nUndated++; }
    else if( strcmp(r->zDue, zT) < 0 )      { zMark = "OVERDUE"; c->nOver++; }
    else if( strncmp(r->zDue, zT, 10)==0 )  { zMark = "TODAY";   c->nToday++; }
    if( r->bMine ) c->nMine++; else c->nTheirs++;
    fprintf(c->out, "%-8s %-12s %-12s %s\n",
            zMark,
            r->zDue[0] ? r->zDue : "(no due)",
            r->bMine ? "mine" : r->zWho,
            r->zText);
    fprintf(c->out, "         %.12s%s%s\n", r->zId,
            r->zPlace[0] ? "  @" : "", r->zPlace);
    return 0;
}


/* ---- JSON out -------------------------------------------------------
** ONE escaper, used by every JSON surface here. The predecessor grew a second
** private copy and the two diverged: an unescaped quote in a note made
** `coverage --json` invalid, which made the morning brief report "nothing at
** all -- no captures, no channels" while three live channels sat in the
** store. A total coverage blackout, exit 0, no warning. */
static void jsonStr(FILE *out, const char *z){
    fputc('"', out);
    for(; z && *z; z++){
        unsigned char c = (unsigned char)*z;
        switch(c){
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:
                if( c < 0x20 ) fprintf(out, "\\u%04x", c);
                else           fputc(c, out);
        }
    }
    fputc('"', out);
}

/* THE BRIEF MUST NOT PARSE A DISPLAY FORMAT. It did, by column offset, and the
** format moved under it twice in one day: a note carrying @place made the
** continuation line two tokens instead of one, and every one of them was
** filed under "an agent is carrying these". This is why --json exists. */
struct ledJson { FILE *out; int n; };
static int ledJsonRow(void *pApp, const VikiTaskRow *r){
    struct ledJson *c = (struct ledJson*)pApp;
    fprintf(c->out, "%s\n  {", c->n ? "," : "");
    fputs("\"id\":", c->out);    jsonStr(c->out, r->zId);
    fputs(",\"due\":", c->out);  jsonStr(c->out, r->zDue);
    fputs(",\"who\":", c->out);  jsonStr(c->out, r->zWho);
    fputs(",\"place\":", c->out);jsonStr(c->out, r->zPlace);
    fputs(",\"state\":", c->out);jsonStr(c->out, r->zState);
    fputs(",\"ts\":", c->out);   jsonStr(c->out, r->zTs);
    fputs(",\"text\":", c->out); jsonStr(c->out, r->zText);
    fprintf(c->out, ",\"mine\":%s}", r->bMine ? "true" : "false");
    c->n++;
    return 0;
}

/* ---- coverage: which channels fed this store, and when last -----------
** A QUERY WITH NO JUDGMENT IN IT. No staleness threshold, no advice: the
** moment it needs a number like "12 hours" it is policy, and policy lives in
** assistant/. SCOPES 3. */
struct covJson { FILE *out; int n; };
static int covRow(void *pApp, int nCol, const char *const *az){
    struct covJson *c = (struct covJson*)pApp;
    if( nCol < 3 ) return 0;
    fprintf(c->out, "%s\n  {", c->n ? "," : "");
    fputs("\"channel\":", c->out); jsonStr(c->out, az[0] ? az[0] : "");
    fputs(",\"last_seen\":", c->out); jsonStr(c->out, az[1] ? az[1] : "");
    fprintf(c->out, ",\"n\":%s}", az[2] ? az[2] : "0");
    c->n++;
    return 0;
}


/* ---- printing a supersession chain -----------------------------------
** FORWARD FIRST, and that ordering is the point rather than a preference: a
** trace arriving at a retired claim must meet the retirement BEFORE the
** claim, or it will act on it exactly the way the trace before it did. */
struct whyPr { FILE *out; int nFwd, nBack; };
static int whyRow(void *pApp, const VikiChainRow *r){
    struct whyPr *c = (struct whyPr*)pApp;
    const char *zArrow;
    if( r->nDepth==0 )      zArrow = "  >>";
    else if( r->bForward ){ zArrow = "  ^ "; c->nFwd++; }
    else                  { zArrow = "  v "; c->nBack++; }
    fprintf(c->out, "%s %.12s  %-6s %s%s%s\n", zArrow, r->zId, r->zKind,
            r->zStatus && r->zStatus[0] ? "[" : "",
            r->zStatus ? r->zStatus : "",
            r->zStatus && r->zStatus[0] ? "] " : "");
    fprintf(c->out, "       %s\n", r->zText ? r->zText : "");
    if( r->zBecause && r->zBecause[0] )
        fprintf(c->out, "       because: %s\n", r->zBecause);
    if( r->zBy && r->zBy[0] )
        fprintf(c->out, "       by: %s   %s\n", r->zBy, r->zTs ? r->zTs : "");
    return 0;
}


/* one column, one line -- for manifests, where anything else is a parse risk */
static int prRaw(void *pApp, int nCol, const char *const *az){
    if( nCol>0 && az[0] ) fprintf((FILE*)pApp, "%s\n", az[0]);
    return 0;
}

/* ---- the verbs, executed against a retained store --------------------- */
static int do_verb(FILE *out, int argc, char **argv, int nLines, int nOverlap){
    int n = 0;
    if( argc<1 ) return usage();

    if( !strcmp(argv[0],"note") && argc>=2 ){
        /* A NOTE IS HOW A TASK CLOSES, which is why --supersedes belongs here
        ** and not only on `task`. When Sue's report finally arrives it is a
        ** RECORD, not a new obligation: filing it as a task would retire the
        ** old row and immediately add a fresh one to the ledger, so the list
        ** never shrinks and stops meaning anything. As a note it removes the
        ** task and adds nothing, because the ledger reads kind='task' only.
        ** This is what the predecessor spelled `--closes`. */
        VikiNote nt; char zId[VIKI_ID_HEX+1]; int i;
        memset(&nt, 0, sizeof(nt));
        nt.vftbl = &vikiNoteVftbl;
        nt.zText = argv[1];
        nt.zTs   = isoNow();
        for(i=2;i+1<argc;i++){
            if(!strcmp(argv[i],"--supersedes")) nt.zSupersedes = argv[++i];
            else if(!strcmp(argv[i],"--key"))   nt.zKey        = argv[++i];
        }
        if( viki_put((VikiAssert*)&nt)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        memcpy(zId, nt.zId, sizeof(zId));
        fprintf(out, "%s\n", zId);
        return 0;
    }
    if( !strcmp(argv[0],"task") && argc>=2 ){
        VikiTask t; char zId[VIKI_ID_HEX+1]; int i;
        memset(&t, 0, sizeof(t));
        t.zText = argv[1];
        t.zTs   = isoNow();
        for(i=2;i+1<argc;i++){
            if(!strcmp(argv[i],"--who"))        t.zWho=argv[++i];
            else if(!strcmp(argv[i],"--due"))   t.zDue=argv[++i];
            else if(!strcmp(argv[i],"--place")) t.zPlace=argv[++i];
            else if(!strcmp(argv[i],"--state")) t.zState=argv[++i];
            else if(!strcmp(argv[i],"--channel")) t.zChannel=argv[++i];
            else if(!strcmp(argv[i],"--supersedes")) t.zSupersedes=argv[++i];
        }
        if( viki_task(&t, zId)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg());
            fprintf(stderr,"%s: a --due must be YYYY-MM-DD or YYYY-MM-DDThh:mm:ssZ\n",zProg);
            return 1; }
        fprintf(out, "%s\n", zId);
        return 0;
    }
    if( !strcmp(argv[0],"ledger") ){
        struct ledPr c; const char *zMe = 0; int i, bJson = 0;
        for(i=1;i<argc;i++){
            if(!strcmp(argv[i],"--json")) bJson = 1;
            else if(!strcmp(argv[i],"--me") && i+1<argc) zMe = argv[++i];
        }
        if( bJson ){
            struct ledJson j; memset(&j,0,sizeof j); j.out = out;
            fputs("[", out);
            if( viki_ledger(zMe, ledJsonRow, &j)!=VIKI_OK ){
                fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            fputs(j.n ? "\n]\n" : "]\n", out);
            return 0;
        }
        memset(&c, 0, sizeof(c)); c.out = out;
        fprintf(out, "%-8s %-12s %-12s %s\n", "RISK", "DUE", "WHO", "WHAT");
        if( viki_ledger(zMe, ledRow, &c)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        /* THE LEDGER STATES ITS OWN COVERAGE, every run. A ledger that prints
        ** nothing and says nothing about why is indistinguishable from one
        ** that could not read its store. */
        fprintf(out, "\n%d overdue, %d due today, %d undated"
                     "  (%d mine, %d held by someone else)\n",
                c.nOver, c.nToday, c.nUndated, c.nMine, c.nTheirs);
        fprintf(out, "       live tasks only -- anything superseded has left.\n");
        return 0;
    }
    if( !strcmp(argv[0],"coverage") ){
        struct covJson j; memset(&j,0,sizeof j); j.out = out;
        fputs("[", out);
        if( viki_sql(
              "SELECT coalesce(nullif(json_extract(body,'$.channel'),''),'(none)'),"
              "       max(ts), count(*)"
              "  FROM viki_assert WHERE kind='task'"
              " GROUP BY 1 ORDER BY 1", covRow, &j)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        fputs(j.n ? "\n]\n" : "]\n", out);
        return 0;
    }
    if( !strcmp(argv[0],"pending") ){
        /* A RAW CAPTURE NEVER STRUCTURED: a note nothing supersedes, that
        ** supersedes nothing itself. The second half is what excludes an
        ** ARRIVAL -- those supersede a task and are already accounted for. */
        struct covJson j; memset(&j,0,sizeof j); j.out = out;
        fputs("[", out);
        if( viki_sql(
              "SELECT a.id, a.ts, 1 FROM viki_assert a"
              " WHERE a.kind='note' AND a.supersedes IS NULL"
              "   AND NOT EXISTS (SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"
              " ORDER BY a.ts", covRow, &j)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        fputs(j.n ? "\n]\n" : "]\n", out);
        return 0;
    }
    if( !strcmp(argv[0],"claim") && argc>=2 ){
        VikiClaim c; char zId[VIKI_ID_HEX+1]; int i;
        memset(&c, 0, sizeof(c));
        c.zText = argv[1];
        c.zTs   = isoNow();
        for(i=2;i+1<argc;i++){
            if(!strcmp(argv[i],"--status"))            c.zStatus=argv[++i];
            else if(!strcmp(argv[i],"--falsified-by")) c.zFalsifier=argv[++i];
            else if(!strcmp(argv[i],"--because"))      c.zBecause=argv[++i];
            else if(!strcmp(argv[i],"--by"))           c.zBy=argv[++i];
            else if(!strcmp(argv[i],"--supersedes"))   c.zSupersedes=argv[++i];
        }
        if( viki_claim(&c, zId)!=VIKI_OK ){
            fprintf(stderr,"%s: refused.\n",zProg);
            fprintf(stderr,"%s:   --status must be one of k0 k1 k2 k3 k4\n",zProg);
            fprintf(stderr,"%s:   a k0 also needs --falsified-by: \"it was checked\"\n",zProg);
            fprintf(stderr,"%s:   without \"and here is what would have shown it wrong\"\n",zProg);
            fprintf(stderr,"%s:   is the exact shape of a confident error.\n",zProg);
            return 1; }
        fprintf(out, "%s\n", zId);
        return 0;
    }
    if( !strcmp(argv[0],"why") && argc>=2 ){
        struct whyPr c; VikiStatus rc;
        memset(&c, 0, sizeof(c)); c.out = out;
        rc = viki_why(argv[1], whyRow, &c);
        if( rc==VIKI_ENOTFOUND ){
            /* NOT FOUND AND NO-CHAIN MUST NOT PRINT THE SAME. */
            fprintf(stderr,"%s: no assertion %s in this diary.\n",zProg,argv[1]);
            fprintf(stderr,"%s: that is NOT the same as \"nothing superseded it\".\n",zProg);
            return 1; }
        if( rc!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        fprintf(out, "\n  %d newer (^ what replaced it), %d older (v what it replaced)\n",
                c.nFwd, c.nBack);
        if( c.nFwd==0 )
            fprintf(out, "  nothing has superseded this. It still stands.\n");
        return 0;
    }
    if( !strcmp(argv[0],"redact") && argc>=2 ){
        VikiRedact r; char zId[VIKI_ID_HEX+1]; int i;
        memset(&r,0,sizeof r);
        r.zTarget = argv[1]; r.zTs = isoNow();
        for(i=2;i+1<argc;i++){
            if(!strcmp(argv[i],"--why")) r.zWhy=argv[++i];
            else if(!strcmp(argv[i],"--by")) r.zBy=argv[++i];
        }
        if( viki_redact(&r, zId)!=VIKI_OK ){
            fprintf(stderr,"%s: refused. --why and --by are both required.\n",zProg);
            fprintf(stderr,"%s: a redaction is IRREVERSIBLE and it PROPAGATES:\n",zProg);
            fprintf(stderr,"%s:   the content dies here and on every peer that merges,\n",zProg);
            fprintf(stderr,"%s:   the id can never be re-added, and nothing recovers it.\n",zProg);
            return 1; }
        fprintf(out, "%s\n", zId);
        return 0;
    }
    if( !strcmp(argv[0],"clone") && argc>=2 ){
        /* CLONE IS NOT SYNC AND CANNOT REPLACE IT.
        **
        ** Warren, 2026-08-30: "even if the desktop got the current exact copy
        ** of the phone viki, they will not be the same... checkpoint copy and
        ** transport is clone, once the cloned (historical) repo is nearby you
        ** still must sync."
        **
        ** A clone is HISTORICAL the instant it lands. It bounds how much the
        ** first merge has to move; it never removes the merge. There is no
        ** state in which two live stores are identical -- convergence is a
        ** property of the OPERATION, not a condition either side can be in,
        ** which is also why nothing here records having been "in sync".
        **
        ** AND A PLAIN cp IS WRONG. The store is journal_mode=wal, so a copy
        ** taken while anything holds it open silently drops committed
        ** transactions still in the -wal. VACUUM INTO takes a consistent
        ** snapshot through SQLite, compacts it, and preserves the SQLCipher
        ** key -- verified: the clone reads under the same key, a stock
        ** sqlite3 still cannot open it, and its manifest is identical. */
        char *zSql; VikiStatus rc; FILE *f;
        if( (f = fopen(argv[1], "rb")) != 0 ){
            fclose(f);
            fprintf(stderr,"%s: %s exists -- refusing to overwrite a store.\n",
                    zProg, argv[1]);
            return 1;
        }
        zSql = sqlite3_mprintf("VACUUM INTO %Q", argv[1]);
        if( !zSql ) return 1;
        rc = viki_sql(zSql, 0, 0);
        sqlite3_free(zSql);
        if( rc!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        fprintf(out, "%s\n", argv[1]);
        fprintf(stderr, "%s: cloned. It is HISTORICAL already -- the source has\n", zProg);
        fprintf(stderr, "%s: moved on. Merge to converge; clone only bounds the first one.\n", zProg);
        return 0;
    }
    if( !strcmp(argv[0],"file") && argc>=2 ){
        VikiFile f; char zId[VIKI_ID_HEX+1]; int i;
        char *zBuf = 0; long nBuf = 0; FILE *in = 0;
        memset(&f,0,sizeof f);
        f.zPath = argv[1]; f.zTs = isoNow();
        for(i=2;i+1<argc;i++){
            if(!strcmp(argv[i],"--content-from")) { in = fopen(argv[++i],"rb");
                if(!in){ fprintf(stderr,"%s: cannot read %s\n",zProg,argv[i]); return 1; } }
            else if(!strcmp(argv[i],"--content")) f.zContent = argv[++i];
            else if(!strcmp(argv[i],"--who"))     f.zWho = argv[++i];
            else if(!strcmp(argv[i],"--ts"))      f.zTs = argv[++i];
            else if(!strcmp(argv[i],"--supersedes")) f.zSupersedes = argv[++i];
        }
        if( !f.zContent ){
            /* stdin when no --content given, so a pipeline can feed it */
            FILE *src = in ? in : stdin;
            size_t cap = 1<<16, got = 0; zBuf = malloc(cap);
            if( !zBuf ) return 1;
            while( !feof(src) ){
                size_t r;
                if( got+4096 > cap ){ char *t = realloc(zBuf, cap*=2); if(!t){free(zBuf);return 1;} zBuf=t; }
                r = fread(zBuf+got, 1, 4096, src); got += r; if( r==0 ) break;
            }
            zBuf[got] = 0; nBuf = (long)got; f.zContent = zBuf;
        }
        if( in ) fclose(in);
        if( viki_file(&f, zId)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); free(zBuf); return 1; }
        free(zBuf);
        fprintf(out, "%s\n", zId);
        (void)nBuf;
        return 0;
    }
    if( !strcmp(argv[0],"checkin") && argc>=2 ){
        VikiCheckin c; char zId[VIKI_ID_HEX+1]; int i, n = 0;
        const char *az[512];
        memset(&c,0,sizeof c);
        c.zTs = isoNow();
        for(i=1;i<argc;i++){
            if(!strcmp(argv[i],"--comment") && i+1<argc) c.zComment = argv[++i];
            else if(!strcmp(argv[i],"--who") && i+1<argc) c.zWho = argv[++i];
            else if(!strcmp(argv[i],"--branch") && i+1<argc) c.zBranch = argv[++i];
            else if(!strcmp(argv[i],"--ts") && i+1<argc) c.zTs = argv[++i];
            else if(!strcmp(argv[i],"--parent") && i+1<argc) c.zSupersedes = argv[++i];
            else if(argv[i][0] != '-' && n < 512) az[n++] = argv[i];
        }
        c.azMember = az; c.nMember = n;
        if( viki_checkin(&c, zId)!=VIKI_OK ){
            fprintf(stderr,"%s: refused -- --comment is required.\n",zProg);
            fprintf(stderr,"%s: a check-in with no message is a group nobody can read later.\n",zProg);
            return 1; }
        fprintf(out, "%s\n", zId);
        return 0;
    }
    if( !strcmp(argv[0],"observe") ){
        /* OBSERVE IS A SET DIFFERENCE, NOT A LOG TAIL, and the schema decides
        ** that rather than taste. viki_assert is WITHOUT ROWID, so the store
        ** keeps NO local arrival order -- "what reached me since I last
        ** looked" is a question it cannot answer. And a ts-ordered tail would
        ** be wrong anyway: an assertion merged from a peer carries the PEER'S
        ** timestamp, which is routinely older than any watermark you hold, so
        ** a tail would silently skip exactly the rows gossip exists to move.
        **
        ** What anti-entropy needs is different and cheaper: ids are content
        ** hashes, so "what do you lack" is a set difference over hex strings
        ** and needs no clock, no watermark, and no agreement about time.
        **
        ** viki_watch() is the OTHER door and is not this: it is per-connection
        ** and cannot see another process's writes, so it serves a host that is
        ** doing the writing, never a peer. */
        const char *zLack = 0, *zAfter = 0; int i, bSeq = 0;
        for(i=1;i<argc;i++){
            if(!strcmp(argv[i],"--lacking") && i+1<argc) zLack = argv[++i];
            else if(!strcmp(argv[i],"--after") && i+1<argc) zAfter = argv[++i];
            else if(!strcmp(argv[i],"--seq")) bSeq = 1;
        }
        if( bSeq ){
            /* THIS DIARY'S CLOCK. Meaningless to a peer -- it orders MY
            ** receipts, not their writes. Keep it as a SENDER-side watermark:
            ** "I have given P everything up to my N." */
            if( viki_sql("SELECT coalesce(max(seq),0) FROM viki_arrival", prRaw, out)
                !=VIKI_OK ){ fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            return 0;
        }
        if( zAfter ){
            /* ROUND DOWN, NEVER UP. Too low costs bandwidth and merge is
            ** idempotent; too high skips rows and reports success. */
            char *zSql = sqlite3_mprintf(
                "SELECT id FROM viki_arrival WHERE seq > %Q ORDER BY seq", zAfter);
            VikiStatus rc;
            if( !zSql ) return 1;
            rc = viki_sql(zSql, prRaw, out);
            sqlite3_free(zSql);
            if( rc!=VIKI_OK ){ fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            return 0;
        }
        if( !zLack ){
            /* the manifest: every id here, sorted, one per line */
            if( viki_sql("SELECT id FROM viki_assert ORDER BY id", prRaw, out)!=VIKI_OK ){
                fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            return 0;
        }
        {   /* ids I hold that the given manifest does not */
            char *zSql; VikiStatus rc; FILE *f = fopen(zLack,"r");
            char zLine[256]; sqlite3_str *pIn;
            if( !f ){ fprintf(stderr,"%s: cannot read %s\n",zProg,zLack); return 1; }
            pIn = sqlite3_str_new(0);
            sqlite3_str_appendall(pIn, "SELECT id FROM viki_assert WHERE id NOT IN (''");
            while( fgets(zLine,sizeof(zLine),f) ){
                size_t n=strlen(zLine);
                while( n && (zLine[n-1]=='\n'||zLine[n-1]=='\r') ) zLine[--n]=0;
                if( n ) sqlite3_str_appendf(pIn, ",%Q", zLine);
            }
            fclose(f);
            sqlite3_str_appendall(pIn, ") ORDER BY id");
            zSql = sqlite3_str_finish(pIn);
            if( !zSql ) return 1;
            rc = viki_sql(zSql, prRaw, out);
            sqlite3_free(zSql);
            if( rc!=VIKI_OK ){ fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            return 0;
        }
    }
    if( !strcmp(argv[0],"ask") && argc>=2 ){
        VikiHits *h = 0; int k = 5, i;
        for(i=2;i<argc-1;i++) if(!strcmp(argv[i],"-k")) k = atoi(argv[i+1]);
        if( viki_ask(argv[1], k, &h)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        n = prHit(out, h); viki_hits_free(h);
        return n ? 0 : 1;            /* nothing found is a non-zero status */
    }
    if( !strcmp(argv[0],"forget") && argc>=2 ){
        VikiStatus rc = viki_forget(argv[1]);
        if( rc==VIKI_ENOTFOUND ){ fprintf(stderr,"%s: no such assertion\n",zProg); return 1; }
        if( rc!=VIKI_OK ){ fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        return 0;
    }
    if( (!strcmp(argv[0],"push") || !strcmp(argv[0],"pull")) && argc>=2 ){
        /* PUSH AND PULL ARE ONE OPERATION WITH THE ENDS SWAPPED, over a PATH.
        ** There is no protocol here and there is nothing to negotiate: ids are
        ** content hashes and the merge is a union, so the only question is
        ** which store receives.
        **
        ** PUSH REFUSES TO CREATE THE DESTINATION, and that is what makes
        ** `clone` a real primitive rather than a convenience. First contact
        ** needs a checkpointed snapshot (a plain cp is wrong under WAL);
        ** afterwards you push. Silently creating an empty peer here would let
        ** a typo look like a successful sync to nowhere.
        **
        ** FOR A DUMB HUB -- a file in a folder something else syncs, holding
        ** no key and running no code -- the round trip is PULL then PUSH, in
        ** that order. Push alone would hand the hub your rows while dropping
        ** whatever another peer left there since. */
        /* strcmp, NOT a character trick. The first version tested
        ** argv[0][1]=='u' -- and "push"[1] and "pull"[1] are BOTH 'u', so
        ** `pull` silently PUSHED. Both commands reported success and the
        ** rows went the wrong way; only a convergence check caught it. */
        int bPush = !strcmp(argv[0],"push");
        sqlite3 *peer = 0;
        const char *zPeerKey = zSrcKeyFile ? zSrcKeyFile : zKeyFile;
        FILE *f = fopen(argv[1], "rb");
        if( !f ){
            fprintf(stderr,"%s: no diary at %s\n",zProg,argv[1]);
            if( bPush ) fprintf(stderr,
                "%s: push does not create one -- first contact is:  viki clone %s\n",
                zProg, argv[1]);
            return 1;
        }
        fclose(f);
        if( sqlite3_open_v2(argv[1], &peer,
                bPush ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY, 0)!=SQLITE_OK ){
            fprintf(stderr,"%s: cannot open %s\n",zProg,argv[1]); return 1; }
        if( apply_key(peer, argv[1], zPeerKey) < 0 ){ sqlite3_close(peer); return 1; }
        if( sqlite3_exec(peer,"SELECT count(*) FROM sqlite_master",0,0,0)!=SQLITE_OK ){
            fprintf(stderr,"%s: cannot read %s -- wrong key, or not a diary\n",zProg,argv[1]);
            sqlite3_close(peer); return 1; }
        if( (bPush ? viki_push(peer,&n) : viki_merge(peer,&n))!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); sqlite3_close(peer); return 1; }
        sqlite3_close(peer);
        fprintf(out, "%d assertion(s) %s\n", n, bPush ? "pushed" : "pulled");
        if( bPush && n )
            fprintf(stderr,"%s: their clock moved, mine did not -- I learned nothing.\n",zProg);
        return 0;
    }
    if( !strcmp(argv[0],"merge") && argc>=2 ){
        sqlite3 *other = 0;
        if( sqlite3_open_v2(argv[1], &other, SQLITE_OPEN_READONLY, 0)!=SQLITE_OK ){
            fprintf(stderr,"%s: cannot open %s\n",zProg,argv[1]); return 1; }
        /* THE SOURCE NEEDS A KEY TOO, and forgetting that made merge work only
        ** between plaintext diaries -- which, once encryption is the default,
        ** means it did not work at all. The source's key defaults to this
        ** diary's (the usual case is your own two diaries); --source-keyfile
        ** is for a peer under a different one. */
        if( apply_key(other, argv[1], zSrcKeyFile ? zSrcKeyFile : zKeyFile) < 0 ){
            sqlite3_close(other); return 1; }
        if( sqlite3_exec(other,"SELECT count(*) FROM sqlite_master",0,0,0)!=SQLITE_OK ){
            fprintf(stderr,"%s: cannot read %s -- wrong key, or not a diary\n",zProg,argv[1]);
            sqlite3_close(other); return 1; }
        if( viki_merge(other, &n)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); sqlite3_close(other); return 1; }
        sqlite3_close(other);
        fprintf(out, "%d assertion(s) added\n", n);
        return 0;
    }
    if( !strcmp(argv[0],"reindex") ){
        VikiChunking ch;
        char zName[32];
        snprintf(zName,sizeof zName,"l%do%d", nLines, nOverlap);
        ch.zName = zName; ch.nLines = nLines; ch.nOverlap = nOverlap;
        if( viki_reindex(&ch, &n)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        fprintf(out, "%d range(s) added under %s\n", n, zName);
        return 0;
    }
    if( !strcmp(argv[0],"cal") && argc>=2 ){
        if( !strcmp(argv[1],"ingest") && argc>=3 ){
            size_t nJ = 0;
            char *zJ = slurp(argv[2], &nJ);
            VikiStatus rc;
            if( !zJ ){ fprintf(stderr,"%s: cannot read %s\n",zProg,argv[2]); return 1; }
            rc = viki_cal_ingest(zJ, argv[2], &n);
            free(zJ);
            if( rc==VIKI_EINVAL ){
                /* REFUSED, not reported as an empty calendar: an adapter that
                ** fetched an HTML error page must not read as a quiet day. */
                fprintf(stderr,"%s: %s is not valid jsCalendar JSON\n",zProg,argv[2]);
                return 1;
            }
            if( rc!=VIKI_OK ){ fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            fprintf(out, "%d event assertion(s)\n", n);
            return 0;
        }
        if( !strcmp(argv[1],"events") ){
            const char *zF = argc>=3 ? argv[2] : 0;
            const char *zT = argc>=4 ? argv[3] : 0;
            if( viki_cal_events(zF, zT, prCal, out)!=VIKI_OK ){
                fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            return 0;
        }
        return usage();
    }
    if( !strcmp(argv[0],"count") && argc>=2 ){
        static const struct { const char *z; VikiCountWhat w; } aMap[] = {
            {"assert",VIKI_N_ASSERT},{"current",VIKI_N_CURRENT},{"range",VIKI_N_RANGE},
            {"vector",VIKI_N_VECTOR},{"chunking",VIKI_N_CHUNKING},{"model",VIKI_N_MODEL}};
        size_t i;
        for(i=0;i<sizeof(aMap)/sizeof(aMap[0]);i++)
            if( !strcmp(argv[1],aMap[i].z) ){
                if( viki_count(aMap[i].w, argc>=3?argv[2]:0, &n)!=VIKI_OK ){
                    fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
                fprintf(out, "%d\n", n);
                return 0;
            }
        return usage();
    }
    if( !strcmp(argv[0],"list") ){
        if( viki_each(argc>=2?argv[1]:0, argc>=3?argv[2]:0, prEach, out)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        return 0;
    }
    if( !strcmp(argv[0],"model") && argc>=2 ){
        /* INGEST. Files are read HERE, once, and never again -- after this
        ** the diary carries the model and needs no directory to exist. */
        if( !strcmp(argv[1],"import") && argc>=3 ){
            static const char *azWant[] = { "model.onnx", "vocab.txt" };
            char zPath[2048], zHash[80], zDesc[256];
            size_t k;
            int nDim = 0;
            for(k=0;k<sizeof(azWant)/sizeof(azWant[0]);k++){
                size_t nB = 0;
                char *zB, zId[VIKI_ID_HEX+1];
                snprintf(zPath,sizeof zPath,"%s/%s",argv[2],azWant[k]);
                zB = slurp(zPath,&nB);
                if( !zB ){ fprintf(stderr,"%s: cannot read %s\n",zProg,zPath); return 1; }
                viki_sha256_hex(zB, nB, zHash);
                snprintf(zDesc,sizeof zDesc,"%s -- imported from %s",azWant[k],argv[2]);
                if( viki_blob_put(zDesc, zHash, zB, (sqlite3_int64)nB, zId)!=VIKI_OK ){
                    fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); free(zB); return 1; }
                fprintf(out,"%-12s %8ld bytes  %.12s\n",azWant[k],(long)nB,zId);
                free(zB);
            }
            {   /* the pinned dimension, out of the manifest and stored as its
                ** own blob so nothing downstream ever parses JSON again */
                size_t nM = 0; char *zM; const char *p2; char zId[VIKI_ID_HEX+1];
                char zDim[32];
                snprintf(zPath,sizeof zPath,"%s/viki-manifest.json",argv[2]);
                zM = slurp(zPath,&nM);
                if( zM && (p2=strstr(zM,"\"dim\""))!=0 && (p2=strchr(p2,':'))!=0 )
                    nDim = atoi(p2+1);
                free(zM);
                if( nDim<=0 ){ fprintf(stderr,"%s: no \"dim\" in %s\n",zProg,zPath); return 1; }
                snprintf(zDim,sizeof zDim,"%d",nDim);
                viki_sha256_hex(zDim, strlen(zDim), zHash);
                if( viki_blob_put("dim -- the pinned embedding width", zHash,
                                  zDim, (sqlite3_int64)strlen(zDim), zId)!=VIKI_OK ){
                    fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
                fprintf(out,"%-12s %8d\n","dim",nDim);
            }
            return 0;
        }
        return usage();
    }
    if( !strcmp(argv[0],"id") && argc>=2 ){
        if( !strcmp(argv[1],"new") && argc>=3 ){
            char zSeed[80];
            VikiIdKey *p = viki_ed25519_generate(argv[2], zSeed);
            FILE *f;
            int fd;
            if( !p ){ fprintf(stderr,"%s: could not generate a key\n",zProg); return 1; }
            if( argc<4 ){ fprintf(stderr,"%s: id new NAME KEYFILE\n",zProg); viki_ed25519_free(p); return 1; }
            /* 0600 AT CREATION, not chmod afterwards: between creat and chmod
            ** is a window, and this is a signing key. */
            fd = open(argv[3], O_WRONLY|O_CREAT|O_EXCL, 0600);
            if( fd<0 ){ fprintf(stderr,"%s: %s: %s\n",zProg,argv[3],strerror(errno)); viki_ed25519_free(p); return 1; }
            f = fdopen(fd,"w");
            fprintf(f, "%s\n%s\n", viki_ed25519_name(p), zSeed);
            fclose(f);
            memset(zSeed, 0, sizeof zSeed);
            fprintf(out, "%s\n", viki_ed25519_pub(p));
            viki_ed25519_free(p);
            return 0;
        }
        if( !strcmp(argv[1],"add") ){
            /* Records the RETAINED identity in this diary, so peers can verify
            ** what it signs. Recording is not trusting. */
            const VikiIdentity *pI = RETAINED(VikiIdentity) ? RECALL(VikiIdentity) : 0;
            if( !pI ){ fprintf(stderr,"%s: id add needs --signer\n",zProg); return 1; }
            fprintf(out, "%s\n", pI->zSigner);
            return 0;
        }
        if( !strcmp(argv[1],"check") && argc>=3 ){
            VikiSigState st2; char zWho[VIKI_ID_HEX+1];
            static const char *az[] = {"unsigned","verified","BAD SIGNATURE","unknown signer"};
            if( viki_signed(argv[2], &st2, zWho)!=VIKI_OK ){
                fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
            fprintf(out, "%s%s%.12s\n", az[st2], st2==VIKI_SIG_NONE?"":"  by ", zWho);
            return st2==VIKI_SIG_BAD ? 1 : 0;
        }
        return usage();
    }
    if( !strcmp(argv[0],"sql") && argc>=2 ){
        if( viki_sql(argv[1], prSql, out)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg()); return 1; }
        return 0;
    }
    return usage();
}

/* ---- the diary SET ---------------------------------------------------
**
** core + private + opened, where `opened` INCLUDES the other two. One
** retained thing rather than a stack of them, because an agent uses all of
** them at once and the inner must not shadow the outer.
**
** The common case is one diary that is both. --core names a second, which is
** where a shared model belongs: it is the tribe's, not yours, and putting it
** beside your notes was only ever an accident of having a single store. */
static const char *zCoreStore = 0;
static sqlite3    *g_dbCore   = 0;

static int build_diaries(VikiDiaries *pd, VikiDiary *pPriv, VikiDiary *pCore,
                         sqlite3 *dbPriv){
    pPriv->zName = "private"; pPriv->db = dbPriv; pPriv->mFlags = 0;
    viki_diaries_one(pd, pPriv);
    if( zCoreStore ){
        g_dbCore = open_store_named(zCoreStore);
        if( !g_dbCore ) return -1;
        pCore->zName = "core"; pCore->db = g_dbCore;
        /* READ-ONLY BY DEFAULT. A shared diary that any peer writes to on a
        ** whim is not shared, it is contended -- and the failure would be
        ** silent, since a write succeeds locally either way. */
        pCore->mFlags = VIKI_D_RDONLY;
        pd->pCore = pCore;
        viki_diaries_add(pd, pCore);
    }
    return 0;
}

/* ---- what the parent retains -----------------------------------------
**
** THIS IS THE API GUARANTEE MADE CONCRETE. Without a held context every
** invocation starts with NOTHING retained -- so a script cannot have signed
** writes or hybrid retrieval at all, whatever it does. With one, the parent
** retains them once and every child inherits the EFFECTS through the socket
** while holding neither the key nor the model.
**
** The order matters: the identity assertion must be written before it can be
** the signer, and writing it needs the store. */
typedef struct {
    VikiIdKey   *pId;
    VikiIdentity id;
    Embedder     emb;
    VikiEmbed    embed;
    int          bId, bEmbed;
} Held;

static int held_open(Held *h){
    memset(h, 0, sizeof *h);
    if( zSignerFile ){
        char zErr[256], zAssert[VIKI_ID_HEX+1];
        h->pId = viki_ed25519_load(zSignerFile, zErr, sizeof zErr);
        if( !h->pId ){ fprintf(stderr,"%s: %s\n",zProg,zErr); return -1; }
        /* core's batteries-included path: it records the identity and fills
        ** the callbacks. The CLI supplies no crypto of its own. */
        /* Recording the identity is idempotent -- it is content-addressed on
        ** (public key, name) -- so this is safe to do on every run and is
        ** what makes the signer's key available to any peer that merges. */
        if( viki_identity_ed25519(h->pId, &h->id, zAssert)!=VIKI_OK ){
            fprintf(stderr,"%s: %s\n",zProg,viki_errmsg());
            viki_ed25519_free(h->pId); h->pId = 0; return -1;
        }
        h->id.zSigner = strdup(zAssert);
        h->bId = 1;
    }else{
        /* VERIFY-ONLY, always. Even with no signing key this process can say
        ** who wrote what -- which is the point of asymmetric signatures and
        ** costs nothing to offer. */
        h->id.zSigner = 0;
        h->id.pApp    = 0;
        h->id.xSign   = 0;
        h->id.xVerify = viki_ed25519_verify;
        h->bId = 1;
    }
    if( zEmbedLib ){
        if( embedder_open(zEmbedLib, &h->emb, &h->embed, zEmbedLib)!=0 ) return -1;
        h->bEmbed = 1;
    }
    return 0;
}
static void held_close(Held *h){
    if( h->bEmbed ) embedder_close(&h->emb);
    if( h->pId ) viki_ed25519_free(h->pId);
    free((void*)h->id.zSigner);
}

/* ---- viki run: one warm context, held by the parent -------------------
**
** PARENT holds the store (and, once wired, the model) and serves.
** CHILD gets $VIKI_CONTEXT and execs the command.
**
** The parent's exit status is the CHILD's, so `viki run make test` behaves
** like `make test` -- a wrapper that swallowed the status would be unusable
** in a script, which is the only place this is worth having. */

/* length-prefixed argv in, `<status>\n<len>\n<bytes>` out. Deliberately dull:
** the protocol is an implementation detail between two copies of the same
** binary, and anything richer would be a second API to keep in step with the
** one in viki_core.h. */
static int wr_all(int fd, const void *p, size_t n){
    const char *z = (const char*)p;
    while( n ){
        ssize_t k = write(fd, z, n);
        if( k<=0 ){ if( errno==EINTR ) continue; return -1; }
        z += k; n -= (size_t)k;
    }
    return 0;
}
static int rd_all(int fd, void *p, size_t n){
    char *z = (char*)p;
    while( n ){
        ssize_t k = read(fd, z, n);
        if( k==0 ) return -1;
        if( k<0 ){ if( errno==EINTR ) continue; return -1; }
        z += k; n -= (size_t)k;
    }
    return 0;
}

static int serve_one(int fd, int nLines, int nOverlap){
    int argc = 0, i, rc;
    char **argv = 0;
    char *zBuf = 0;
    size_t nBuf = 0;
    FILE *out;
    if( rd_all(fd, &argc, sizeof argc) || argc<1 || argc>64 ) return -1;
    argv = (char**)calloc((size_t)argc+1, sizeof(char*));
    if( !argv ) return -1;
    for(i=0;i<argc;i++){
        int n = 0;
        if( rd_all(fd, &n, sizeof n) || n<0 || n>1<<20 ) goto done;
        argv[i] = (char*)malloc((size_t)n+1);
        if( !argv[i] || rd_all(fd, argv[i], (size_t)n) ) goto done;
        argv[i][n] = 0;
    }
    out = open_memstream(&zBuf, &nBuf);
    if( !out ) goto done;
    rc = do_verb(out, argc, argv, nLines, nOverlap);
    fclose(out);
    { int n = (int)nBuf;
      if( wr_all(fd,&rc,sizeof rc) || wr_all(fd,&n,sizeof n)
       || (n && wr_all(fd,zBuf,(size_t)n)) ){ /* client hung up */ } }
    free(zBuf);
done:
    for(i=0;i<argc;i++) free(argv[i]);
    free(argv);
    return 0;
}

/* Returns the verb's status, or -1 if there is no reachable context -- in
** which case the caller opens the store itself. THE DIRECT PATH IS REQUIRED,
** not a fallback: a stale $VIKI_CONTEXT must degrade to working, not fail. */
static int client(const char *zSock, int argc, char **argv){
    struct sockaddr_un sa;
    int fd, i, rc = 0, n = 0;
    char *zBuf;
    if( strlen(zSock) >= sizeof(sa.sun_path) ) return -1;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if( fd<0 ) return -1;
    memset(&sa,0,sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", zSock);
    if( connect(fd,(struct sockaddr*)&sa,sizeof sa)!=0 ){ close(fd); return -1; }
    if( wr_all(fd,&argc,sizeof argc) ){ close(fd); return -1; }
    for(i=0;i<argc;i++){
        int k = (int)strlen(argv[i]);
        if( wr_all(fd,&k,sizeof k) || wr_all(fd,argv[i],(size_t)k) ){ close(fd); return -1; }
    }
    if( rd_all(fd,&rc,sizeof rc) || rd_all(fd,&n,sizeof n) ){ close(fd); return -1; }
    if( n>0 ){
        zBuf = (char*)malloc((size_t)n);
        if( zBuf && rd_all(fd,zBuf,(size_t)n)==0 ) fwrite(zBuf,1,(size_t)n,stdout);
        free(zBuf);
    }
    close(fd);
    return rc;
}

static volatile sig_atomic_t bChildGone = 0;
static void on_sigchld(int s){ (void)s; bChildGone = 1; }

static int run_cmd(const char *zStore, int argc, char **argv,
                   int nLines, int nOverlap){
    char zDir[256], zSock[256];
    struct sockaddr_un sa;
    int srv, rcChild = 1;
    pid_t pid;
    sqlite3 *db;
    VikiDiary  dPriv, dCore;
    VikiDiaries ds;

    if( argc<1 ) return usage();
    /* A 0700 DIRECTORY IS THE WHOLE GUARD. The parent holds a decrypted store,
    ** so reaching this socket is reaching the memory without the passphrase.
    ** Filesystem permissions do that job without a token, without a port, and
    ** without appearing in netstat. */
    snprintf(zDir, sizeof zDir, "%s/viki-%ld",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    if( mkdir(zDir, 0700)!=0 ){
        fprintf(stderr,"%s: cannot create %s: %s\n",zProg,zDir,strerror(errno));
        return 1;
    }
    snprintf(zSock, sizeof zSock, "%s/sock", zDir);
    if( strlen(zSock) >= sizeof(sa.sun_path) ){
        fprintf(stderr,"%s: socket path too long\n",zProg); rmdir(zDir); return 1; }

    srv = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&sa,0,sizeof sa); sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path,sizeof sa.sun_path,"%s",zSock);
    if( srv<0 || bind(srv,(struct sockaddr*)&sa,sizeof sa)!=0 || listen(srv,16)!=0 ){
        fprintf(stderr,"%s: cannot listen: %s\n",zProg,strerror(errno));
        if(srv>=0) close(srv);
        rmdir(zDir); return 1;
    }

    /* THE STORE IS OPENED BEFORE THE FORK, so the child never opens it and
    ** never needs the key -- which is the entire point. SQLite forbids using
    ** one connection across a fork, and nothing here does: the child speaks
    ** the socket and touches no sqlite3*. */
    db = open_store(zStore);
    if( !db ){ close(srv); unlink(zSock); rmdir(zDir); return 1; }
    if( build_diaries(&ds, &dPriv, &dCore, db)!=0 ){
        sqlite3_close(db); close(srv); unlink(zSock); rmdir(zDir); return 1; }

    signal(SIGCHLD, on_sigchld);
    pid = fork();
    if( pid<0 ){
        fprintf(stderr,"%s: fork: %s\n",zProg,strerror(errno));
        sqlite3_close(db); close(srv); unlink(zSock); rmdir(zDir); return 1;
    }
    if( pid==0 ){
        close(srv);
        setenv(VIKI_CTX_ENV, zSock, 1);
        execvp(argv[0], argv);
        fprintf(stderr,"%s: %s: %s\n",zProg,argv[0],strerror(errno));
        _exit(127);
    }

    {   RETAIN_BEGIN(VikiDiaries, &ds, g);
        Held held;
        int bDrain = 0;
        if( held_open(&held)!=0 ){
            kill(pid, SIGTERM); waitpid(pid, 0, 0);
            sqlite3_close(db); close(srv); unlink(zSock); rmdir(zDir);
            return 1;
        }
        /* Retained AROUND the serve loop, so every request the children make
        ** is answered with the identity and the model already in scope. */
        /* CONDITIONAL, and D4 is why. Retaining a ZEROED VikiEmbed is not the
        ** same as retaining none: viki_reindex sees one, finds zModel NULL,
        ** and correctly refuses -- so every CLI invocation silently indexed
        ** nothing. retain's own RETAIN_BEGIN_IF is the answer (the C form of
        ** C++ retain<T>(p,false)); a struct that is present-but-empty is not
        ** absence, and core is right to tell them apart.
        **
        ** The identity is retained UNCONDITIONALLY on purpose: with no signer
        ** it still carries xVerify, so this process can always say who wrote
        ** what even when it cannot sign. */
        RETAIN_BEGIN(VikiIdentity, &held.id, gi);
        RETAIN_BEGIN_IF(VikiEmbed, &held.embed, held.bEmbed, ge);
        /* SELECT WITH A TIMEOUT, NOT A BLOCKING accept().
        **
        ** signal() installs a BSD-style handler with SA_RESTART, so SIGCHLD
        ** does NOT interrupt accept() -- it resumes, and `while(!bChildGone)`
        ** is never re-tested. Measured: the parent hung after the child
        ** exited, having served every request correctly. Polling the flag on
        ** a short timeout is simpler than fighting the signal semantics, and
        ** it also gives the drain below for free.
        **
        ** After the child is gone, keep accepting for one more interval: a
        ** script whose LAST line is `viki note ...` may have the connection in
        ** flight when it exits, and losing that write would make the whole
        ** thing untrustworthy. */
        for(;;){
            fd_set r; struct timeval tv;
            int fd, k;
            tv.tv_sec = 0; tv.tv_usec = 20000;
            FD_ZERO(&r); FD_SET(srv, &r);
            k = select(srv+1, &r, 0, 0, &tv);
            if( k>0 ){
                fd = accept(srv, 0, 0);
                if( fd>=0 ){ serve_one(fd, nLines, nOverlap); close(fd); }
                continue;
            }
            if( k<0 && errno!=EINTR ) break;
            if( bChildGone ){ if( bDrain ) break; bDrain = 1; }
        }
        RETAIN_END(ge);
        RETAIN_END(gi);
        held_close(&held);
        RETAIN_END(g);
    }
    {   int status = 0;
        waitpid(pid, &status, 0);
        rcChild = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    sqlite3_close(db);
    close(srv); unlink(zSock); rmdir(zDir);
    return rcChild;                      /* the CHILD's status, not ours */
}

int main(int argc, char **argv){
    const char *zStore = getenv("VIKI_STORE");
    const char *zCtx   = getenv(VIKI_CTX_ENV);
    int nLines = 40, nOverlap = 10;
    int i, rc;
    sqlite3 *db;
    VikiDiary  dPriv, dCore;
    VikiDiaries ds;

    if( argc>0 ) zProg = argv[0];
    if( !zStore ) zStore = "viki.db";
    /* option scan, before the verb */
    for(i=1;i<argc;){
        if( !strcmp(argv[i],"--key") ){
            /* argv is world-readable in `ps`, and shell history keeps it. A
            ** flag that exists will end up in a cron line, so it does not
            ** exist. */
            fprintf(stderr,"%s: --key is refused: argv is visible to every "
                           "process on this machine.\n"
                           "%s: use --keyfile PATH (mode 600), or $VIKI_KEY.\n",
                    zProg, zProg);
            return 2;
        }
        if( !strcmp(argv[i],"--signer") && i+1<argc ){ zSignerFile=argv[i+1]; memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--embedder") && i+1<argc ){ zEmbedLib=argv[i+1]; memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--keyfile") && i+1<argc ){ zKeyFile=argv[i+1]; memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--source-keyfile") && i+1<argc ){ zSrcKeyFile=argv[i+1]; memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--plaintext") ){ bPlaintext=1; memmove(argv+i,argv+i+1,(size_t)(argc-i-1)*sizeof(char*)); argc-=1; }
        else if( !strcmp(argv[i],"--core") && i+1<argc ){ zCoreStore=argv[i+1]; memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--store") && i+1<argc ){ zStore=argv[i+1]; memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--lines") && i+1<argc ){ nLines=atoi(argv[i+1]); memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else if( !strcmp(argv[i],"--overlap") && i+1<argc ){ nOverlap=atoi(argv[i+1]); memmove(argv+i,argv+i+2,(size_t)(argc-i-2)*sizeof(char*)); argc-=2; }
        else i++;
    }
    if( argc<2 ) return usage();

    if( !strcmp(argv[1],"run") )
        return run_cmd(zStore, argc-2, argv+2, nLines, nOverlap);

    /* A CONTEXT IS USED WHEN REACHABLE AND NEVER REQUIRED. A stale
    ** $VIKI_CONTEXT -- a killed parent, a copied environment -- degrades to
    ** opening the store directly rather than failing, the same way a missing
    ** embedder degrades retrieval instead of refusing it. */
    if( zCtx && zCtx[0] ){
        rc = client(zCtx, argc-1, argv+1);
        if( rc>=0 ) return rc;
    }
    db = open_store(zStore);
    if( !db ) return 1;
    if( build_diaries(&ds, &dPriv, &dCore, db)!=0 ){ sqlite3_close(db); return 1; }
    {   Held held;
        RETAIN_BEGIN(VikiDiaries, &ds, g);
        if( held_open(&held)!=0 ){ sqlite3_close(db); return 1; }
        /* CONDITIONAL, and D4 is why. Retaining a ZEROED VikiEmbed is not the
        ** same as retaining none: viki_reindex sees one, finds zModel NULL,
        ** and correctly refuses -- so every CLI invocation silently indexed
        ** nothing. retain's own RETAIN_BEGIN_IF is the answer (the C form of
        ** C++ retain<T>(p,false)); a struct that is present-but-empty is not
        ** absence, and core is right to tell them apart.
        **
        ** The identity is retained UNCONDITIONALLY on purpose: with no signer
        ** it still carries xVerify, so this process can always say who wrote
        ** what even when it cannot sign. */
        RETAIN_BEGIN(VikiIdentity, &held.id, gi);
        RETAIN_BEGIN_IF(VikiEmbed, &held.embed, held.bEmbed, ge);
        rc = do_verb(stdout, argc-1, argv+1, nLines, nOverlap);
        RETAIN_END(ge);
        RETAIN_END(gi);
        held_close(&held);
        RETAIN_END(g);
    }
    sqlite3_close(db);
    return rc;
}

/* open_store() with the same keying, for a diary that is not the private one.
** Named separately so the read-only intent is visible at the call site. */
static sqlite3 *open_store_named(const char *zPath){ return open_store(zPath); }

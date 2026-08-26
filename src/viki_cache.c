#include "viki_cache.h"
#include "sha256.h"
#include <sqlite3.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define VIKI_UV_NAME "viki-cache.db"

/* The pinned model travels as three uv blobs under one stable prefix.
** Names are a wire contract (a puller built from a different checkout
** must find them), so they are spelled out here once and nowhere else. */
#define VIKI_UV_MODEL_MANIFEST "viki-model/viki-manifest.json"

/* THE VERSIONED EPOCH PIN, in the checkout rather than in uv.
**
** Warren, 2026-08-25: "state is artifacts. infrastructure is versioned
** artifacts." The model BYTES are neither -- they are 23MB of derived,
** replaceable payload, and D-12 puts them in uv for exactly that reason. The
** PIN is infrastructure: it declares which epoch this tribe is on, and that
** claim has to be checkable.
**
** Verifying uv bytes against a uv manifest is circular: whoever can replace
** model.onnx replaces the manifest vouching for it in the same push, and
** neither carries a hash in the sync protocol. A VERSIONED file is
** content-addressed and Merkle-linked through its check-in manifest, so a peer
** can detect substitution without trusting the sender. Same bytes, same
** channel, an attestation that is worth something. */
#define VIKI_VERSIONED_MANIFEST "viki-model.json"

/* THE SIGNATURE AND THE TRUST ANCHOR.
**
** Warren, 2026-08-25: "infrastructure versions are signed? i think that closes
** the loop." A versioned pin is tamper-EVIDENT -- Merkle-linked, so nobody can
** alter it undetected. It is not AUTHORITATIVE: every tribe member can commit,
** so a member pushing a bogus epoch produces an artifact just as well-formed as
** the real one. Integrity answers "has this changed?"; a signature answers
** "who said it?", and an epoch pin is a claim that needs an author.
**
** THE SPLIT THAT MAKES THIS WORK, and it is worth stating because it is not
** the obvious one: the SIGNATURE is self-authenticating, so it does not care
** what channel carried it -- a valid signature over the uv pin is worth exactly
** as much as one over the versioned pin, because an attacker who replaces both
** blobs still cannot forge it. What DOES need the Merkle chain is the SIGNER
** LIST, since a verifier that trusts a substituted key list verifies the
** attacker's signature happily. So:
**
**     the pin       may travel by uv        (signature protects it)
**     the signers   must be VERSIONED       (nothing else protects them)
**
** which is why viki-signers.json is read only from the checkout and never
** from uv, and why the signature check runs on BOTH pin paths. */
#define VIKI_VERSIONED_SIGNERS "viki-signers.json"
#define VIKI_VERSIONED_SIG     VIKI_VERSIONED_MANIFEST ".sig"
#define VIKI_UV_MODEL_SIG      VIKI_UV_MODEL_MANIFEST ".sig"

/* Push order is deliberate: blobs first, MANIFEST LAST. The manifest is
** the epoch pin (VIKI_DESIGN.md), and it is also what the skip-if-
** unchanged check below reads, so an interrupted push that got the model
** up but not the manifest leaves the old pin in place and simply retries
** next time -- rather than advertising an epoch whose bytes never
** arrived. Pull mirrors it: manifest installed only after the blobs it
** names are on disk and checksum-verified. */
/* REFUSE TO PUBLISH A PRIVATE BLOB.
**
** SYNC.md classes a blob as derived / grow-only / owned / private, and private
** is the only class with no safe sync at any frequency. identity.db holds
** private keys each wrapped under a passphrase, inside a SQLCipher container
** whose key is PUBLIC by design -- QUEUE 49 keeps the known key for its
** per-page HMAC (tamper detection), not for secrecy. Publishing it hands every
** wrapped key to anyone with repo read access, to attack offline at leisure
** with no rate limit.
**
** Matched on the BASENAME so a path dressed up as ../../identity.db is caught.
** A check in code rather than a line in a document, because SYNC.md already
** says so and a document cannot refuse. */
int viki_cache_refuse_private(const char *zPath){
    static const char *azPrivate[] = {
        "identity.db", "identity.db-wal", "identity.db-journal", "identity.db-shm", NULL
    };
    const char *base;
    int i;
    if( !zPath ) return 0;
    base = strrchr(zPath, '/');
    base = base ? base + 1 : zPath;
    for( i = 0; azPrivate[i]; i++ ){
        if( strcmp(base, azPrivate[i]) == 0 ){
            fprintf(stderr,
                "viki: REFUSING to publish '%s'.\n"
                "  It is a PRIVATE blob (SYNC.md): passphrase-wrapped private keys inside a\n"
                "  container whose key is public by design, so publishing hands every wrapped\n"
                "  key to anyone with repo read access to attack offline.\n"
                "  Private blobs have no safe sync at any frequency.\n", zPath);
            return 1;
        }
    }
    return 0;
}

static const struct {
    const char *zBase;    /* file name inside the model directory */
    const char *zUv;      /* stable unversioned-file name */
    const char *zShaKey;  /* manifest field pinning its sha256, or NULL */
} aModelFile[] = {
    { "vocab.txt",          "viki-model/vocab.txt",  "vocab_sha256" },
    { "model.onnx",         "viki-model/model.onnx", "model_sha256" },
    { "viki-manifest.json", VIKI_UV_MODEL_MANIFEST,  NULL },
};
#define VIKI_N_MODEL_FILE ((int)(sizeof(aModelFile)/sizeof(aModelFile[0])))
/* The manifest is the last entry, by the ordering argument above. */
#define VIKI_MANIFEST_IX  (VIKI_N_MODEL_FILE - 1)

static int find_on_path(const char *name){
    const char *pathEnv = getenv("PATH");
    char buf[4096];
    const char *p, *colon;

    if( !pathEnv ) return 0;
    p = pathEnv;
    while( *p ){
        colon = strchr(p, ':');
        size_t n = colon ? (size_t)(colon - p) : strlen(p);
        if( n > 0 && n < sizeof(buf) - strlen(name) - 2 ){
            memcpy(buf, p, n);
            snprintf(buf + n, sizeof(buf) - n, "/%s", name);
            if( access(buf, X_OK) == 0 ) return 1;
        }
        p = colon ? colon + 1 : p + strlen(p);
    }
    return 0;
}

const char *viki_fossil_binary(void){
    const char *override = getenv("VIKI_FOSSIL_BIN");
    if( override && override[0] ) return override;
    if( find_on_path("fossil-see") ) return "fossil-see";
    return "fossil";
}

const char *viki_fossil_user(void){
    const char *override = getenv("VIKI_FOSSIL_USER");
    const char *osUser = getenv("USER");
    if( override && override[0] ) return override;
    if( osUser && osUser[0] ) return osUser;
    return "viki";
}

const char *viki_model_dir(void){
    const char *zDir = getenv("VIKI_MODEL_DIR");
    if( zDir && zDir[0] ) return zDir;
    return "build/dist/model";
}

/* Runs argv (NULL-terminated) as a child process, waits for it, and
** returns its exit code (or -1 on fork/exec failure). stdout/stderr are
** inherited so `fossil uv ...` output is visible directly -- these are
** infrequent, interactive-ish operations, not something to capture and
** re-format. bQuiet sends both streams to /dev/null instead, for the
** probes below whose FAILURE is a normal outcome (`uv export` of a name
** the hub has never held is fossil_fatal + "no such uv-file", which
** would otherwise look like a real error to whoever reads the log). */
static int run_ex(char *const argv[], int bQuiet){
    pid_t pid = fork();
    int status;

    if( pid < 0 ){
        perror("viki: fork");
        return -1;
    }
    if( pid == 0 ){
        if( bQuiet ){
            int fd = open("/dev/null", O_WRONLY);
            if( fd >= 0 ){
                dup2(fd, 1);
                dup2(fd, 2);
                if( fd > 2 ) close(fd);
            }
        }
        execvp(argv[0], argv);
        fprintf(stderr, "viki: exec %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    if( waitpid(pid, &status, 0) < 0 ){
        perror("viki: waitpid");
        return -1;
    }
    if( WIFEXITED(status) ) return WEXITSTATUS(status);
    return -1;
}

static int run(char *const argv[]){ return run_ex(argv, 0); }

/* -------------------------------------------------- small file helpers -- */

static long long file_size(const char *zPath){
    struct stat st;
    if( stat(zPath, &st) != 0 || !S_ISREG(st.st_mode) ) return -1;
    return (long long)st.st_size;
}

/* Human-readable size, always in MB with one decimal: the point of
** printing it at all is that the model is ~23 MB and the reader should
** see that number, so a unit that shrinks it back to "22.0" vs "0.2" is
** exactly the comparison wanted. */
static const char *fmt_mb(long long n, char *zBuf, size_t nBuf){
    snprintf(zBuf, nBuf, "%.1f MB", (double)n / (1024.0*1024.0));
    return zBuf;
}

/* Reads a whole file into a NUL-terminated malloc'd buffer. Used for the
** manifest (a few hundred bytes) and for the model (~23 MB) -- the
** latter only because viki_sha256_hex() has no streaming API. */
static unsigned char *read_whole_file(const char *zPath, size_t *pnOut){
    FILE *f = fopen(zPath, "rb");
    long long sz;
    unsigned char *pBuf;
    size_t nRead;

    if( !f ) return NULL;
    sz = file_size(zPath);
    if( sz < 0 ){ fclose(f); return NULL; }
    pBuf = malloc((size_t)sz + 1);
    if( !pBuf ){ fclose(f); return NULL; }
    nRead = fread(pBuf, 1, (size_t)sz, f);
    fclose(f);
    if( nRead != (size_t)sz ){ free(pBuf); return NULL; }
    pBuf[sz] = 0;
    if( pnOut ) *pnOut = (size_t)sz;
    return pBuf;
}

static int files_equal(const char *zA, const char *zB){
    FILE *fa, *fb;
    char bufA[65536], bufB[65536];
    int rc = 1;

    if( file_size(zA) != file_size(zB) ) return 0;
    fa = fopen(zA, "rb");
    if( !fa ) return 0;
    fb = fopen(zB, "rb");
    if( !fb ){ fclose(fa); return 0; }
    for(;;){
        size_t na = fread(bufA, 1, sizeof(bufA), fa);
        size_t nb = fread(bufB, 1, sizeof(bufB), fb);
        if( na != nb || memcmp(bufA, bufB, na) != 0 ){ rc = 0; break; }
        if( na == 0 ) break;
    }
    fclose(fa);
    fclose(fb);
    return rc;
}

static int copy_file(const char *zFrom, const char *zTo){
    FILE *fi = fopen(zFrom, "rb");
    FILE *fo;
    char buf[65536];
    size_t n;

    if( !fi ) return 1;
    fo = fopen(zTo, "wb");
    if( !fo ){ fclose(fi); return 1; }
    while( (n = fread(buf, 1, sizeof(buf), fi)) > 0 ){
        if( fwrite(buf, 1, n, fo) != n ){ fclose(fi); fclose(fo); return 1; }
    }
    fclose(fi);
    return fclose(fo) == 0 ? 0 : 1;
}

/* mkdir -p. The default model directory is "build/dist/model", three
** levels of which may not exist in a fresh clone. */
static int mkdir_p(const char *zPath){
    char zBuf[4096];
    size_t i, n;
    struct stat st;

    n = strlen(zPath);
    if( n == 0 || n >= sizeof(zBuf) ) return 1;
    memcpy(zBuf, zPath, n + 1);
    while( n > 1 && zBuf[n-1] == '/' ) zBuf[--n] = 0;
    for( i = 1; i <= n; i++ ){
        if( zBuf[i] != '/' && zBuf[i] != 0 ) continue;
        zBuf[i] = 0;
        if( stat(zBuf, &st) != 0 && mkdir(zBuf, 0755) != 0 && errno != EEXIST ){
            fprintf(stderr, "viki: mkdir %s: %s\n", zBuf, strerror(errno));
            return 1;
        }
        if( i < n ) zBuf[i] = '/';
    }
    return 0;
}

/* Pulls a "key": "value" string field out of the manifest. Same
** deliberately-minimal approach as embed.c's read_manifest(): this file
** is written by build.sh, not by an arbitrary producer, and pulling in a
** JSON parser to read three flat string fields would be the larger risk. */
static int manifest_field(const char *zJson, const char *zKey, char *zOut, size_t nOut){
    char zNeedle[64];
    const char *p, *end;
    size_t n;

    snprintf(zNeedle, sizeof(zNeedle), "\"%s\"", zKey);
    p = strstr(zJson, zNeedle);
    if( !p ) return 1;
    p = strchr(p + strlen(zNeedle), ':');
    if( !p ) return 1;
    p = strchr(p, '"');
    if( !p ) return 1;
    p++;
    end = strchr(p, '"');
    if( !end ) return 1;
    n = (size_t)(end - p);
    if( n >= nOut ) return 1;
    memcpy(zOut, p, n);
    zOut[n] = 0;
    return 0;
}

static int sha256_file_hex(const char *zPath, char zHexOut[65]){
    size_t n = 0;
    unsigned char *pBuf = read_whole_file(zPath, &n);
    if( !pBuf ) return 1;
    viki_sha256_hex(pBuf, n, zHexOut);
    free(pBuf);
    return 0;
}

static void temp_path(char *zBuf, size_t nBuf, const char *zTag){
    const char *zTmp = getenv("TMPDIR");
    if( !zTmp || !zTmp[0] ) zTmp = "/tmp";
    snprintf(zBuf, nBuf, "%s/viki-uv-%s-%ld", zTmp, zTag, (long)getpid());
}

/* `fossil uv export NAME OUT`, silent on failure -- see run_ex(). */
static int uv_export_quiet(const char *zFossil, const char *zUv, const char *zOut){
    char *argv[] = { (char*)zFossil, "uv", "export", (char*)zUv, (char*)zOut, NULL };
    return run_ex(argv, 1);
}

/* ------------------------------------------- signature verification -- */
/*
** THE VERIFIER IS A SUBPROCESS, for the same reason fossil is: viki links no
** crypto library and that is a hard constraint (four platforms, no fossil-see
** prerequisite). `viki-identity` carries LibreSSL from fossil-see's vendor
** tree, so it is exactly the thing that may be absent.
**
** WHICH MAKES ONE STATE LOAD-BEARING: "signed, but I could not check it" must
** never print like "verified". That is the same failure the Chrome reader's
** `blind` status exists to prevent -- a check that silently does not run turns
** into false calm, which is worse than no check because it is believed. Every
** outcome below is reported explicitly, and --require-signature turns the
** unknowable ones into refusals for a tribe that wants that policy.
*/
typedef enum {
    VIKI_SIG_OK = 0,        /* verified against a listed signer            */
    VIKI_SIG_UNSIGNED,      /* no .sig alongside the pin                   */
    VIKI_SIG_NO_ANCHOR,     /* no viki-signers.json in the checkout        */
    VIKI_SIG_NO_VERIFIER,   /* viki-identity not available -- CANNOT CHECK */
    VIKI_SIG_REJECTED       /* did not verify against any listed signer    */
} VikiSigState;

const char *viki_identity_binary(void){
    const char *override = getenv("VIKI_IDENTITY_BIN");
    if( override && override[0] ) return override;
    if( find_on_path("viki-identity") ) return "viki-identity";
    return NULL;
}

/* Trims trailing whitespace/newline in place -- a .sig file written by a shell
** redirect carries a newline the base64 decoder must not see. */
static void trim_tail(char *z){
    size_t n = strlen(z);
    while( n > 0 && (z[n-1]=='\n' || z[n-1]=='\r' || z[n-1]==' ' || z[n-1]=='\t') ) z[--n] = 0;
}

/* Walks viki-signers.json, yielding (name, ed25519) pairs in order.
**
** DELIBERATELY A SCANNER, NOT A JSON PARSER: it finds the next "name" and then
** the next "ed25519" after it, so within one object the fields must appear in
** that order. viki already parses its manifest this way (manifest_field), the
** file is written by this program, and a real JSON parser is a dependency this
** binary does not otherwise need. Stated rather than hidden, because a
** hand-rolled scanner over a security-relevant file is a fair thing to check.
*/
static const char *signers_next(const char *p, char *zName, size_t nName,
                                char *zKey, size_t nKey){
    const char *q;
    if( !p ) return NULL;
    p = strstr(p, "\"name\"");
    if( !p ) return NULL;
    if( manifest_field(p, "name", zName, nName) != 0 ) return NULL;
    q = strstr(p, "\"ed25519\"");
    if( !q ) return NULL;
    if( manifest_field(q, "ed25519", zKey, nKey) != 0 ) return NULL;
    return q + 1;
}

/*
** Verify zPinPath against zSigPath using the checkout's signer list.
** On VIKI_SIG_OK, zWhoOut names the signer that vouched for it.
*/
static VikiSigState verify_pin(const char *zPinPath, const char *zSigPath,
                               char *zWhoOut, size_t nWho){
    const char *zId;
    unsigned char *pSig, *pSigners;
    char zSig[512], zName[128], zKey[256];
    const char *p;
    int bAnyTried = 0;
    VikiSigState rc = VIKI_SIG_REJECTED;

    zWhoOut[0] = 0;

    pSig = read_whole_file(zSigPath, NULL);
    if( !pSig ) return VIKI_SIG_UNSIGNED;
    snprintf(zSig, sizeof(zSig), "%s", (const char*)pSig);
    free(pSig);
    trim_tail(zSig);
    if( !zSig[0] ) return VIKI_SIG_UNSIGNED;

    /* The anchor is read from the CHECKOUT only -- never uv. See the comment
    ** on VIKI_VERSIONED_SIGNERS: this is the one input whose substitution the
    ** signature cannot detect, so it is the one that must be Merkle-attested. */
    pSigners = read_whole_file(VIKI_VERSIONED_SIGNERS, NULL);
    if( !pSigners ) return VIKI_SIG_NO_ANCHOR;

    zId = viki_identity_binary();
    if( !zId ){ free(pSigners); return VIKI_SIG_NO_VERIFIER; }

    p = (const char*)pSigners;
    while( (p = signers_next(p, zName, sizeof(zName), zKey, sizeof(zKey))) != NULL ){
        char *argv[] = { (char*)zId, "verify", "-p", zKey, "-i", (char*)zPinPath,
                         "-s", zSig, NULL };
        int x = run_ex(argv, 1);
        bAnyTried = 1;
        if( x == 0 ){
            snprintf(zWhoOut, nWho, "%s", zName);
            rc = VIKI_SIG_OK;
            break;
        }
        /* 127 is exec failure, not a bad signature. Without this a missing
        ** verifier would read as REJECTED -- reporting tampering when the only
        ** fact established is that nothing ran. */
        if( x == 127 || x < 0 ){ rc = VIKI_SIG_NO_VERIFIER; break; }
    }
    if( !bAnyTried && rc != VIKI_SIG_OK ) rc = VIKI_SIG_NO_ANCHOR;   /* empty list */
    free(pSigners);
    return rc;
}

/*
** Print the outcome and decide whether it stops the pull.
** Returns 0 to continue, 1 to refuse.
**
** REJECTED IS ALWAYS FATAL and is not a policy call: a pin that fails
** verification is evidence, and installing a model on that basis is the one
** thing this feature exists to prevent. Everything else is a policy call and
** obeys bRequire, because refusing an unsigned pin by default would break
** every tribe that has not adopted signing yet.
*/
static int report_sig(VikiSigState st, const char *zWho, int bRequire){
    switch( st ){
        case VIKI_SIG_OK:
            fprintf(stderr, "viki cache pull:   epoch pin SIGNED by '%s' (ed25519, verified)\n", zWho);
            return 0;
        case VIKI_SIG_REJECTED:
            fprintf(stderr,
                "viki cache pull: SIGNATURE REJECTED on the epoch pin\n"
                "viki cache pull:   The pin carries a signature that verifies against NONE of the\n"
                "viki cache pull:   keys in '%s'. Either the pin was altered, or it was signed by\n"
                "viki cache pull:   an identity this tribe does not list. Both are refusals.\n"
                "viki cache pull:   Refusing to install -- viki degrades to BM25-only rather than\n"
                "viki cache pull:   running inference on a model vouched for by nobody.\n",
                VIKI_VERSIONED_SIGNERS);
            return 1;
        case VIKI_SIG_NO_VERIFIER:
            fprintf(stderr,
                "viki cache pull:   epoch pin is SIGNED but CANNOT BE CHECKED -- no viki-identity\n"
                "viki cache pull:     on PATH (set VIKI_IDENTITY_BIN, or sh edge/tools/build-tools.sh).\n"
                "viki cache pull:     This is NOT a passing check. Nothing was verified.\n");
            return bRequire;
        case VIKI_SIG_NO_ANCHOR:
            fprintf(stderr,
                "viki cache pull:   epoch pin is signed, but this checkout has no '%s',\n"
                "viki cache pull:     so there is no trusted key to check it against. Nothing was\n"
                "viki cache pull:     verified. Commit a signer list to close this.\n",
                VIKI_VERSIONED_SIGNERS);
            return bRequire;
        case VIKI_SIG_UNSIGNED:
        default:
            fprintf(stderr,
                "viki cache pull:   epoch pin is UNSIGNED -- integrity only, no authority.\n"
                "viki cache pull:     Any tribe member could have published it. See SYNC.md.\n");
            return bRequire;
    }
}

/* ------------------------------------------------------------- push -- */

/* Publishes the pinned model directory as uv blobs. Returns 0 if the
** model is now published OR legitimately absent, 1 only on a real
** failure (a `fossil uv add` that errored).
**
** ABSENCE IS NOT AN ERROR. Degraded, model-less operation is a
** first-class path in VIKI_DESIGN.md -- `viki index`/`viki ask` already
** run BM25-only when no model is present -- so a peer that has never
** built one must still be able to publish its embedding-free cache. It
** is called out loudly instead, because the consequence (every peer
** cloning this hub stays BM25-only) is invisible otherwise. */
static int push_model(const char *zFossil){
    const char *zDir = viki_model_dir();
    char zLocal[VIKI_N_MODEL_FILE][4096];
    char zManifestProbe[4096];
    char zSize[32];
    long long nTotal = 0;
    int i, rc;
    unsigned char *pManifest;
    char zModelId[128];

    for( i = 0; i < VIKI_N_MODEL_FILE; i++ ){
        long long sz;
        snprintf(zLocal[i], sizeof(zLocal[i]), "%s/%s", zDir, aModelFile[i].zBase);
        sz = file_size(zLocal[i]);
        if( sz < 0 ){
            fprintf(stderr,
                "viki cache push: no model to publish -- '%s' is missing.\n"
                "viki cache push:   NOT AN ERROR: the embedding cache is still published. But no\n"
                "viki cache push:   model travels with it, so peers cloning this hub get BM25-only\n"
                "viki cache push:   (degraded) retrieval. Set $VIKI_MODEL_DIR or build build/dist/model.\n",
                zLocal[i]);
            return 0;
        }
        nTotal += sz;
    }

    pManifest = read_whole_file(zLocal[VIKI_MANIFEST_IX], NULL);
    if( !pManifest ) return 1;
    if( manifest_field((char*)pManifest, "model_id", zModelId, sizeof(zModelId)) != 0 ){
        snprintf(zModelId, sizeof(zModelId), "(unknown)");
    }

    /* SKIP AN UNCHANGED EPOCH. uv is latest-wins with no history (D-12),
    ** so re-adding identical bytes is harmless -- but not free: fossil's
    ** `uv add` unconditionally REPLACEs the row (unversioned_write() in
    ** fossil's unversioned.c has no is-it-different test) and stamps
    ** mtime=now. Measured on the 23 MB pinned model: 1.14s of re-hashing
    ** and re-deflating per push, and the bumped mtime then shows up at
    ** every sync as `UV-PUSH-MTIME-ONLY: viki-model/model.onnx`. The wire
    ** cost of that is trivial (~586 bytes, fossil compares hashes before
    ** sending content) -- the CPU and the noise are the reasons. The
    ** manifest is exactly the right thing to compare, because it IS the
    ** epoch pin: it carries model_id plus the sha256 of both blobs, so
    ** identical manifest == identical model. (Consequence, stated
    ** plainly: editing model.onnx WITHOUT bumping the manifest will not
    ** re-publish it. That is the epoch contract, not an oversight.)
    **
    ** The comparison is against what THIS repository already holds --
    ** which is what previous pushes put there, or what a previous `uv
    ** sync` brought down. A never-pushed spoke has no uv rows at all
    ** (plain `fossil clone` does not carry unversioned content), so its
    ** first push uploads the model even if the hub already has that
    ** epoch; the alternative -- syncing first to find out -- would
    ** download the same 23 MB it is trying to avoid uploading. */
    temp_path(zManifestProbe, sizeof(zManifestProbe), "pushprobe");
    if( uv_export_quiet(zFossil, VIKI_UV_MODEL_MANIFEST, zManifestProbe) == 0
     && files_equal(zManifestProbe, zLocal[VIKI_MANIFEST_IX]) ){
        unlink(zManifestProbe);
        free(pManifest);
        fprintf(stderr,
            "viki cache push: model epoch '%s' (%s) is already published under viki-model/ --\n"
            "viki cache push:   not re-pushing. Bump viki-manifest.json to publish a new epoch.\n",
            zModelId, fmt_mb(nTotal, zSize, sizeof(zSize)));
        return 0;
    }
    unlink(zManifestProbe);
    free(pManifest);

    fprintf(stderr, "viki cache push: publishing model epoch '%s' from '%s' (%s total)\n",
            zModelId, zDir, fmt_mb(nTotal, zSize, sizeof(zSize)));
    fprintf(stderr,
        "viki cache push:   uv blobs are LATEST-WINS with no history (D-12): this REPLACES any\n"
        "viki cache push:   model previously published to this hub, irreversibly.\n");

    rc = 0;
    for( i = 0; i < VIKI_N_MODEL_FILE; i++ ){
        char *argvAdd[] = { (char*)zFossil, "uv", "add", zLocal[i], "--as",
                            (char*)aModelFile[i].zUv, NULL };
        if( viki_cache_refuse_private(zLocal[i]) ){ rc = 1; break; }
        fprintf(stderr, "viki cache push:   %-30s %s\n", aModelFile[i].zUv,
                fmt_mb(file_size(zLocal[i]), zSize, sizeof(zSize)));
        if( run(argvAdd) != 0 ){
            fprintf(stderr, "viki cache push: 'fossil uv add %s' FAILED -- model NOT published\n",
                    aModelFile[i].zUv);
            rc = 1;
            break;
        }
    }

    /* PUBLISHING AN EPOCH IS AN INFRASTRUCTURE CHANGE, so it leaves a versioned
    ** declaration behind. Written, not committed: a commit on someone's behalf
    ** is a surprise, and the pin is only meaningful once a human has put it in
    ** the timeline deliberately. Until then `pull` says so out loud. */
    if( rc == 0 ){
        unsigned char *pMan = read_whole_file(zLocal[VIKI_MANIFEST_IX], NULL);
        if( pMan ){
            FILE *f = fopen(VIKI_VERSIONED_MANIFEST, "wb");
            if( f ){
                fputs((const char*)pMan, f);
                fclose(f);
                fprintf(stderr,
                    "viki cache push:\n"
                    "viki cache push:   wrote %s -- COMMIT IT to make the epoch pin verifiable.\n"
                    "viki cache push:     fossil add %s && fossil commit -m 'model epoch %s'\n"
                    "viki cache push:   Until it is committed, a puller verifies the model against\n"
                    "viki cache push:   a uv blob that whoever replaced the model could also replace.\n"
                    "viki cache push:\n"
                    "viki cache push:   And SIGN it, so the pin carries authority and not just\n"
                    "viki cache push:   integrity -- any tribe member can commit one:\n"
                    "viki cache push:     viki-identity sign <you> -i %s > %s\n"
                    "viki cache push:     fossil add %s\n"
                    "viki cache push:   Signing is NOT done here on purpose: it costs a passphrase,\n"
                    "viki cache push:   and a push that silently prompts for one is a push that gets\n"
                    "viki cache push:   run from cron with a key in an env var.\n",
                    VIKI_VERSIONED_MANIFEST, VIKI_VERSIONED_MANIFEST, zModelId,
                    VIKI_VERSIONED_MANIFEST, VIKI_VERSIONED_SIG, VIKI_VERSIONED_SIG);
            }
            free(pMan);
        }
    }
    return rc;
}

/* Runs argv and captures stdout+stderr into a malloc'd buffer (caller
** frees). Returns the child's exit code, or -1. Needed because
** `fossil uv sync` reports the failure this project cares about most in its
** OUTPUT rather than its exit status -- see uv_sync_checked(). */
static char *run_capture_text(char *const argv[], int *pExit){
    int pipefd[2];
    pid_t pid;
    char *buf;
    size_t cap = 65536, len = 0;

    if( pExit ) *pExit = -1;
    if( pipe(pipefd) != 0 ) return NULL;
    pid = fork();
    if( pid < 0 ){ close(pipefd[0]); close(pipefd[1]); return NULL; }
    if( pid == 0 ){
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    buf = malloc(cap);
    if( !buf ){ close(pipefd[0]); waitpid(pid, NULL, 0); return NULL; }
    for(;;){
        ssize_t n;
        if( len + 4096 + 1 > cap ){
            char *bufNew = realloc(buf, cap * 2);
            if( !bufNew ) break;
            buf = bufNew; cap *= 2;
        }
        n = read(pipefd[0], buf + len, 4096);
        if( n <= 0 ) break;
        len += (size_t)n;
    }
    buf[len] = 0;
    close(pipefd[0]);
    {
        int status;
        if( waitpid(pid, &status, 0) >= 0 && WIFEXITED(status) ){
            if( pExit ) *pExit = WEXITSTATUS(status);
        }
    }
    return buf;
}

/* Runs `fossil uv sync` and FAILS when the server refused the upload.
**
** THIS IS THE THIRD TIME THIS PROJECT HAS BEEN BITTEN BY THE SAME SHAPE.
** `fossil sql` exits 0 on a failed query (FINDINGS.md; it is what let
** sweep_sources() delete every forum: row). `command -v sqlite3` tested
** presence rather than fts5 capability (QUEUE 33). And here: uploading
** unversioned content requires the Fossil `y` capability, the server
** rejects the push with "Write permissions for unversioned files missing",
** and fossil's own sync_unversioned() DISCARDS the return value -- so
** `fossil uv sync` prints a warning and exits 0 with the hub empty.
**
** Reproduced against a live HTTP hub using exactly the capabilities
** server/SERVER_SETUP.md recommends for an agent: push exit 0, hub uv rows
** 0, spoke uv rows 1. The operator then debugs the phone, three days later
** and a thousand miles from the laptop where the push silently did nothing.
**
** So the exit status is not the authority; the output is. A push that did
** not land must not report success. */
static int uv_sync_checked(const char *zFossil, const char *zWhat){
    char *argvSync[] = { (char*)zFossil, "uv", "sync", NULL };
    int rc = -1;
    char *zOut = run_capture_text(argvSync, &rc);
    int bRefused = 0;

    if( !zOut ){
        fprintf(stderr, "viki cache %s: could not run 'fossil uv sync'\n", zWhat);
        return 1;
    }
    fputs(zOut, stderr);
    if( strstr(zOut, "uv-pull-only")
     || strstr(zOut, "not authorized")
     || strstr(zOut, "Write permissions for unversioned files missing") ){
        bRefused = 1;
    }
    free(zOut);

    if( rc != 0 ){
        fprintf(stderr, "viki cache %s: 'fossil uv sync' failed (exit %d)\n", zWhat, rc);
        return 1;
    }
    if( bRefused ){
        fprintf(stderr,
            "viki cache %s: the SERVER REFUSED the unversioned upload, and fossil still\n"
            "viki cache %s:   exited 0. Nothing was published.\n"
            "viki cache %s:   Uploading unversioned content needs the Fossil 'y'\n"
            "viki cache %s:   capability; a user with 'c i o r w' cannot push a cache.\n"
            "viki cache %s:   Fix: fossil user capabilities <user> +y  (on the hub).\n",
            zWhat, zWhat, zWhat, zWhat, zWhat);
        return 1;
    }
    return 0;
}

/* Publishes a CONSISTENT SNAPSHOT of the cache rather than the live file.
**
** viki_db.c opens the cache with journal_mode=WAL, and `fossil uv add`
** reads only the main database file. So with a long-lived reader holding
** the WAL open -- which is exactly the topology VIKIVERSE.md recommends as
** the default, a local `viki serve` -- the bare .db lags the live database
** and the push publishes a VALID BUT STALE cache. Measured: 53KB of WAL
** holding content that the pushed file did not contain.
**
** That is the silent-partial-corpus hazard this project has already proven
** it cannot detect after the fact: peers pull a cache missing recent work
** and no confidence threshold distinguishes it (FINDINGS.md).
**
** VACUUM INTO produces a defined, checkpointed, single-file snapshot, and
** as a side effect removes the torn-read risk from a concurrent
** `viki index`. Returns 0 and fills zSnap on success. */
static int cache_snapshot(const char *zCacheDbPath, char *zSnap, size_t nSnap){
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int rc;

    snprintf(zSnap, nSnap, "%s.push-snapshot", zCacheDbPath);
    unlink(zSnap);

    if( sqlite3_open_v2(zCacheDbPath, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki cache push: cannot open cache '%s': %s\n",
                zCacheDbPath, db ? sqlite3_errmsg(db) : "(no handle)");
        sqlite3_close(db);
        return 1;
    }
    rc = sqlite3_prepare_v2(db, "VACUUM INTO ?1", -1, &st, NULL);
    if( rc == SQLITE_OK ){
        sqlite3_bind_text(st, 1, zSnap, -1, SQLITE_STATIC);
        rc = sqlite3_step(st) == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
    }
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki cache push: VACUUM INTO failed: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return rc == SQLITE_OK ? 0 : 1;
}

int viki_cmd_cache_push_opts(const char *zCacheDbPath, unsigned mFlags){
    const char *fossil = viki_fossil_binary();
    char zSnap[2048];
    char *argvAdd[7];
    int rc, rcModel = 0;

    /* Refuse a private blob BEFORE snapshotting it -- a snapshot of identity.db
    ** is still identity.db, and leaving a decrypted-ish copy on disk on the way
    ** to a refusal would be its own small leak. */
    if( viki_cache_refuse_private(zCacheDbPath) ) return 1;

    /* Snapshot first -- never publish the live WAL-backed file. See
    ** cache_snapshot(). */
    if( cache_snapshot(zCacheDbPath, zSnap, sizeof(zSnap)) != 0 ) return 1;

    argvAdd[0] = (char*)fossil;
    argvAdd[1] = "uv";
    argvAdd[2] = "add";
    argvAdd[3] = zSnap;
    argvAdd[4] = "--as";
    argvAdd[5] = (char*)VIKI_UV_NAME;
    argvAdd[6] = NULL;

    fprintf(stderr, "viki cache push: %s uv add %s --as %s\n", fossil, zSnap, VIKI_UV_NAME);
    rc = run(argvAdd);
    if( rc != 0 ){
        fprintf(stderr, "viki cache push: 'fossil uv add' failed (exit %d)\n", rc);
        unlink(zSnap);
        return 1;
    }
    unlink(zSnap);

    if( mFlags & VIKI_CACHE_NO_MODEL ){
        fprintf(stderr,
            "viki cache push: --no-model: publishing the embedding cache only. Peers that do not\n"
            "viki cache push:   already have the model will run BM25-only.\n");
    }else{
        /* A model failure does not skip the sync below: the cache add has
        ** already happened locally and is worth getting to the hub either
        ** way. The nonzero status still surfaces at the end. */
        rcModel = push_model(fossil);
    }

    fprintf(stderr, "viki cache push: %s uv sync\n", fossil);
    if( uv_sync_checked(fossil, "push") != 0 ) return 1;
    return rcModel;
}

/* ------------------------------------------------------------- pull -- */

/* Materializes the published model into viki_model_dir() -- the same
** directory viki.c resolves before opening the embedder, so a fresh
** clone that pulls can immediately `viki ask` in hybrid mode with no
** further configuration.
**
** Returns 0 when the model is now present OR when the hub simply has no
** model published (a clear, non-fatal outcome: the cache still pulled,
** retrieval is just BM25-only). Returns 1 only for a model that arrived
** BROKEN -- a checksum that disagrees with the epoch pin is a real
** failure and gets said out loud rather than silently degraded, because
** the pinned checksum is the only integrity statement viki has about a
** binary it is about to load and run inference with. */
static int pull_model(const char *zFossil, int bRequireSig){
    const char *zDir = viki_model_dir();
    char zTmpManifest[4096], zDst[4096], zSize[32];
    char zModelId[128], zWantHash[128], zGotHash[65];
    char zWho[128];
    VikiSigState zSigState = VIKI_SIG_UNSIGNED;
    unsigned char *pManifest;
    long long nTotal = 0;
    int i;

    zWho[0] = 0;

    /* PREFER THE VERSIONED PIN. If the checkout carries viki-model.json, that
    ** is the epoch declaration and it is Merkle-attested; the uv copy is then
    ** only a convenience for peers who have not pulled the checkout yet. */
    temp_path(zTmpManifest, sizeof(zTmpManifest), "manifest");
    pManifest = read_whole_file(VIKI_VERSIONED_MANIFEST, NULL);
    if( pManifest ){
        /* COPY IT TO THE TEMP PATH the rest of this function already uses.
        ** The first cut set zTmpManifest[0]=0 here, which silently disabled the
        ** files_equal() "already present -- nothing to fetch" check below and
        ** made every pull re-export ~23 MB. Same-shaped bug as the two-line
        ** recipient: a value left empty for one branch, read by another. */
        FILE *f = fopen(zTmpManifest, "wb");
        if( f ){ fputs((const char*)pManifest, f); fclose(f); }
        fprintf(stderr, "viki cache pull:   epoch pin from %s (versioned, Merkle-attested)\n",
                VIKI_VERSIONED_MANIFEST);
        zSigState = verify_pin(VIKI_VERSIONED_MANIFEST, VIKI_VERSIONED_SIG,
                               zWho, sizeof(zWho));
        goto have_manifest;
    }

    if( uv_export_quiet(zFossil, VIKI_UV_MODEL_MANIFEST, zTmpManifest) != 0 ){
        fprintf(stderr,
            "viki cache pull: no model published on this hub (no uv blob '%s').\n"
            "viki cache pull:   The embedding cache pulled fine; retrieval here will be BM25-only\n"
            "viki cache pull:   unless a model is available locally. Not an error -- run\n"
            "viki cache pull:   'viki cache push' from a peer that has one.\n",
            VIKI_UV_MODEL_MANIFEST);
        unlink(zTmpManifest);
        return 0;
    }

    pManifest = read_whole_file(zTmpManifest, NULL);
    if( !pManifest ){ unlink(zTmpManifest); return 1; }
    /* Fell back to the uv manifest. Say plainly that the check is circular --
    ** it still catches corruption, which is what D-12 needed, but a log line
    ** reading "verified" without this qualification claims more than it can. */
    fprintf(stderr,
        "viki cache pull:   epoch pin from uv '%s' -- UNVERIFIED SOURCE.\n"
        "viki cache pull:     uv is name-addressed and latest-wins, so whoever can replace the\n"
        "viki cache pull:     model can replace this pin too. The sha256 check below catches\n"
        "viki cache pull:     CORRUPTION, not substitution. Commit %s to fix that.\n",
        VIKI_UV_MODEL_MANIFEST, VIKI_VERSIONED_MANIFEST);

    /* AND YET THE SIGNATURE STILL COUNTS HERE. The transport is untrusted, but
    ** a signature is self-authenticating -- an attacker who replaces the uv pin
    ** cannot forge one over their replacement. A verified signature on this
    ** path is worth exactly as much as on the versioned one, which is why the
    ** check is not skipped for being "the unverified source". */
    {
        char zTmpSig[4096];
        temp_path(zTmpSig, sizeof(zTmpSig), "manifest-sig");
        if( uv_export_quiet(zFossil, VIKI_UV_MODEL_SIG, zTmpSig) == 0 ){
            zSigState = verify_pin(zTmpManifest, zTmpSig, zWho, sizeof(zWho));
        }else{
            zSigState = VIKI_SIG_UNSIGNED;
        }
        unlink(zTmpSig);
    }

have_manifest:
    if( report_sig(zSigState, zWho, bRequireSig) != 0 ){
        unlink(zTmpManifest);
        free(pManifest);
        return 1;
    }
    if( manifest_field((char*)pManifest, "model_id", zModelId, sizeof(zModelId)) != 0 ){
        snprintf(zModelId, sizeof(zModelId), "(unknown)");
    }

    /* Same epoch check as push, in the other direction: if this checkout
    ** already holds this exact epoch pin, the ~23 MB export is pure
    ** waste. The pin alone is not enough evidence, though -- a manifest
    ** whose blobs were deleted underneath it would skip the fetch and
    ** leave the directory broken -- so require the blobs on disk too. */
    snprintf(zDst, sizeof(zDst), "%s/%s", zDir, aModelFile[VIKI_MANIFEST_IX].zBase);
    if( files_equal(zTmpManifest, zDst) ){
        int bComplete = 1;
        for( i = 0; i < VIKI_MANIFEST_IX; i++ ){
            char zHave[4096];
            snprintf(zHave, sizeof(zHave), "%s/%s", zDir, aModelFile[i].zBase);
            if( file_size(zHave) <= 0 ) bComplete = 0;
        }
        if( bComplete ){
            fprintf(stderr, "viki cache pull: model epoch '%s' already present at '%s' -- nothing to fetch\n",
                    zModelId, zDir);
            unlink(zTmpManifest);
            free(pManifest);
            return 0;
        }
        fprintf(stderr, "viki cache pull: '%s' pins epoch '%s' but its files are missing -- refetching\n",
                zDst, zModelId);
    }

    if( mkdir_p(zDir) != 0 ){ unlink(zTmpManifest); free(pManifest); return 1; }

    for( i = 0; i < VIKI_MANIFEST_IX; i++ ){   /* manifest installed last */
        snprintf(zDst, sizeof(zDst), "%s/%s", zDir, aModelFile[i].zBase);
        fprintf(stderr, "viki cache pull: %s uv export %s %s\n", zFossil, aModelFile[i].zUv, zDst);
        if( uv_export_quiet(zFossil, aModelFile[i].zUv, zDst) != 0 ){
            fprintf(stderr,
                "viki cache pull: '%s' is missing from the hub even though '%s' is present --\n"
                "viki cache pull:   the published model is INCOMPLETE. Re-run 'viki cache push'\n"
                "viki cache pull:   from a peer that has the full model directory.\n",
                aModelFile[i].zUv, VIKI_UV_MODEL_MANIFEST);
            unlink(zTmpManifest);
            free(pManifest);
            return 1;
        }
        nTotal += file_size(zDst);

        /* Verify against the epoch pin's own checksum. build.sh records
        ** model_sha256/vocab_sha256 in the manifest; nothing else in viki
        ** ever checks them, so this is the one place a corrupted or
        ** truncated model gets caught -- before ONNX Runtime is asked to
        ** load it. Older manifests without the field are tolerated (noted,
        ** not failed): the pin is versioned content and viki must still
        ** work against a hub written by an older build. */
        {
            const char *zKey = aModelFile[i].zShaKey;
            if( !zKey || manifest_field((char*)pManifest, zKey, zWantHash, sizeof(zWantHash)) != 0 ){
                fprintf(stderr, "viki cache pull:   (manifest has no %s -- integrity unverified)\n",
                        zKey ? zKey : "checksum");
            }else if( sha256_file_hex(zDst, zGotHash) != 0 ){
                fprintf(stderr, "viki cache pull:   cannot read back '%s' to verify it\n", zDst);
                unlink(zTmpManifest);
                free(pManifest);
                return 1;
            }else if( strcmp(zGotHash, zWantHash) != 0 ){
                fprintf(stderr,
                    "viki cache pull: CHECKSUM MISMATCH on '%s'\n"
                    "viki cache pull:   manifest pins %s\n"
                    "viki cache pull:   pulled bytes are %s\n"
                    "viki cache pull:   Refusing to install the epoch pin -- the model directory is\n"
                    "viki cache pull:   left without a manifest, so viki degrades to BM25-only\n"
                    "viki cache pull:   rather than running inference on unverified bytes.\n",
                    zDst, zWantHash, zGotHash);
                unlink(zTmpManifest);
                free(pManifest);
                return 1;
            }else{
                fprintf(stderr, "viki cache pull:   %s sha256 verified against the epoch pin\n",
                        aModelFile[i].zBase);
            }
        }
    }
    free(pManifest);

    /* Install the pin only now that everything it names is on disk and
    ** verified -- see the push-order comment at the top of this file. */
    snprintf(zDst, sizeof(zDst), "%s/viki-manifest.json", zDir);
    if( copy_file(zTmpManifest, zDst) != 0 ){
        fprintf(stderr, "viki cache pull: cannot write '%s': %s\n", zDst, strerror(errno));
        unlink(zTmpManifest);
        return 1;
    }
    nTotal += file_size(zDst);
    unlink(zTmpManifest);

    fprintf(stderr, "viki cache pull: model epoch '%s' installed at '%s' (%s) -- hybrid retrieval available\n",
            zModelId, zDir, fmt_mb(nTotal, zSize, sizeof(zSize)));
    return 0;
}

/* Merges a pulled cache INTO the local one instead of overwriting it.
**
** `fossil uv export` is a plain blob_write_to_file() -- no atomic rename,
** no existence check, no backup. Pointed straight at the live cache, as
** pull used to do, it DESTROYS whatever the local device had indexed and
** not yet pushed. Reproduced: a chunk present locally before the pull was
** gone after it, exit 0, no warning.
**
** That directly contradicts D-11, whose whole premise is "computed once by
** whoever sees the content first, THEN SHARED" -- a union, not a
** whole-file replacement in whichever direction ran last. It also retires
** the multi-writer race: two peers pushing different subsets can no longer
** clobber each other on the way back down, because the merge is by
** content-addressed key and therefore commutative.
**
** ON A FRESH CLONE (no local cache, or one with no chunks) this instead
** moves the pulled file into place verbatim. That keeps the pulled artifact
** byte-identical to the pushed one, which is what test/m1.sh C10 asserts,
** and it is the correct behaviour anyway: there is nothing to merge with.
**
** Local rows always win. INSERT OR IGNORE on viki_chunk's primary key
** (content_hash, model_id, chunk_ix) is safe precisely because a chunk is
** a deterministic function of that key -- two peers cannot disagree about
** the bytes without also disagreeing about the hash. viki_source is merged
** the same way rather than replaced, so a peer's paths and mtimes never
** overwrite this device's own staleness state.
**
** Returns 0 on success. */
static int cache_merge_in(const char *zCacheDbPath, const char *zIncoming){
    sqlite3 *db = NULL;
    char *zErr = NULL;
    char *zSql = NULL;
    struct stat st;
    int rc = 1;
    sqlite3_int64 nBefore = 0, nAfter = 0;
    sqlite3_stmt *pStmt = NULL;

    /* No local cache at all -> take the incoming file verbatim. */
    if( stat(zCacheDbPath, &st) != 0 || st.st_size == 0 ){
        if( rename(zIncoming, zCacheDbPath) != 0 ){
            fprintf(stderr, "viki cache pull: cannot install cache at %s: %s\n",
                    zCacheDbPath, strerror(errno));
            return 1;
        }
        (void)chmod(zCacheDbPath, 0600);
        return 0;
    }

    if( sqlite3_open(zCacheDbPath, &db) != SQLITE_OK ){
        fprintf(stderr, "viki cache pull: cannot open local cache: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    /* An empty local cache is also "nothing to merge with", but we already
    ** hold it open, so finish through the merge path rather than racing a
    ** rename against our own handle. */
    if( sqlite3_prepare_v2(db, "SELECT count(*) FROM viki_chunk", -1, &pStmt, NULL)==SQLITE_OK
     && sqlite3_step(pStmt)==SQLITE_ROW ){
        nBefore = sqlite3_column_int64(pStmt, 0);
    }
    sqlite3_finalize(pStmt);
    pStmt = NULL;

    zSql = sqlite3_mprintf("ATTACH DATABASE %Q AS inc", zIncoming);
    if( !zSql ){ sqlite3_close(db); return 1; }
    rc = sqlite3_exec(db, zSql, NULL, NULL, &zErr);
    sqlite3_free(zSql);
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki cache pull: cannot read pulled cache: %s\n",
                zErr ? zErr : "(no message)");
        sqlite3_free(zErr);
        sqlite3_close(db);
        return 1;
    }

    rc = sqlite3_exec(db,
        "BEGIN;"
        "INSERT OR IGNORE INTO viki_chunk(content_hash,model_id,chunk_ix,chunk_text,embedding)"
        "  SELECT content_hash,model_id,chunk_ix,chunk_text,embedding FROM inc.viki_chunk;"
        /* The FTS index is REBUILT from the merged viki_chunk rather than
        ** copied out of inc.chunk_fts, and that is not laziness.
        ** chunk_fts is external-content (viki_db.c), so an entry is bound
        ** to a viki_chunk ROWID -- and rowids are assigned locally by the
        ** INSERT above, so the incoming cache's rowids name different rows
        ** here (or no rows at all). Copying them would produce an index
        ** whose snippets and deletes silently resolve to the wrong chunk.
        **
        ** Rebuilding is also what makes a peer running an OLDER build
        ** mergeable: inc.chunk_fts may still be a plain fts5 table with an
        ** incompatible shape, and this path no longer reads it at all.
        ** Only inc.viki_chunk -- unchanged by that work -- is required.
        **
        ** Cost is O(local corpus) per pull instead of O(delta). Pull is not
        ** a hot path (it follows a network sync), and correctness here is
        ** the thing that was already broken twice; see QUEUE 40. */
        "INSERT INTO chunk_fts(chunk_fts) VALUES('rebuild');"
        "COMMIT;", NULL, NULL, &zErr);
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki cache pull: merge failed: %s\n", zErr ? zErr : "(no message)");
        sqlite3_free(zErr);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_exec(db, "DETACH DATABASE inc", NULL, NULL, NULL);
        sqlite3_close(db);
        return 1;
    }

    /* viki_source is merged separately and tolerantly: older caches may not
    ** have every column this build knows about, so a failure here is not
    ** fatal -- the chunks are what matter and they are already in. */
    sqlite3_exec(db,
        "INSERT OR IGNORE INTO viki_source(path,mtime,content_hash)"
        "  SELECT path,mtime,content_hash FROM inc.viki_source;", NULL, NULL, NULL);

    if( sqlite3_prepare_v2(db, "SELECT count(*) FROM viki_chunk", -1, &pStmt, NULL)==SQLITE_OK
     && sqlite3_step(pStmt)==SQLITE_ROW ){
        nAfter = sqlite3_column_int64(pStmt, 0);
    }
    sqlite3_finalize(pStmt);

    sqlite3_exec(db, "DETACH DATABASE inc", NULL, NULL, NULL);
    sqlite3_close(db);
    unlink(zIncoming);

    fprintf(stderr,
        "viki cache pull: merged -- %lld chunk(s) local before, %lld after (+%lld new).\n"
        "viki cache pull:   local work was NOT overwritten.\n",
        (long long)nBefore, (long long)nAfter, (long long)(nAfter - nBefore));
    return 0;
}

int viki_cmd_cache_pull_opts(const char *zCacheDbPath, unsigned mFlags){
    const char *fossil = viki_fossil_binary();
    char *argvSync[] = { (char*)fossil, "uv", "sync", NULL };
    char *argvExport[] = { (char*)fossil, "uv", "export", (char*)VIKI_UV_NAME, NULL, NULL };
    char zTmp[2048];
    int rc;

    fprintf(stderr, "viki cache pull: %s uv sync\n", fossil);
    rc = run(argvSync);
    if( rc != 0 ){
        fprintf(stderr, "viki cache pull: 'fossil uv sync' failed (exit %d)\n", rc);
        return 1;
    }

    /* Export BESIDE the live cache, never onto it: `fossil uv export` is an
    ** unconditional overwrite (see cache_merge_in). */
    snprintf(zTmp, sizeof(zTmp), "%s.pull-incoming", zCacheDbPath);
    unlink(zTmp);
    argvExport[4] = zTmp;   /* index 4 is the DESTINATION; index 3 is the uv name */

    fprintf(stderr, "viki cache pull: %s uv export %s %s\n", fossil, VIKI_UV_NAME, zTmp);
    rc = run(argvExport);
    if( rc != 0 ){
        fprintf(stderr,
            "viki cache pull: 'fossil uv export' failed (exit %d) -- "
            "if this is a fresh clone with nothing pushed yet, that's expected\n", rc);
        unlink(zTmp);
        return 1;
    }
    if( cache_merge_in(zCacheDbPath, zTmp) != 0 ){
        unlink(zTmp);
        return 1;
    }

    if( mFlags & VIKI_CACHE_NO_MODEL ){
        fprintf(stderr, "viki cache pull: --no-model: embedding cache only, model not fetched\n");
        return 0;
    }
    return pull_model(fossil, (mFlags & VIKI_CACHE_REQUIRE_SIG) ? 1 : 0);
}

int viki_cmd_cache_push(const char *zCacheDbPath){
    return viki_cmd_cache_push_opts(zCacheDbPath, 0);
}

int viki_cmd_cache_pull(const char *zCacheDbPath){
    return viki_cmd_cache_pull_opts(zCacheDbPath, 0);
}

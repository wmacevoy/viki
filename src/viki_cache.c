#include "viki_cache.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define VIKI_UV_NAME "viki-cache.db"

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

/* Runs argv (NULL-terminated) as a child process, waits for it, and
** returns its exit code (or -1 on fork/exec failure). stdout/stderr are
** inherited so `fossil uv ...` output is visible directly -- these are
** infrequent, interactive-ish operations, not something to capture and
** re-format. */
static int run(char *const argv[]){
    pid_t pid = fork();
    int status;

    if( pid < 0 ){
        perror("viki: fork");
        return -1;
    }
    if( pid == 0 ){
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

int viki_cmd_cache_push(const char *zCacheDbPath){
    const char *fossil = viki_fossil_binary();
    char *argvAdd[] = { (char*)fossil, "uv", "add", (char*)zCacheDbPath, "--as", VIKI_UV_NAME, NULL };
    char *argvSync[] = { (char*)fossil, "uv", "sync", NULL };
    int rc;

    fprintf(stderr, "viki cache push: %s uv add %s --as %s\n", fossil, zCacheDbPath, VIKI_UV_NAME);
    rc = run(argvAdd);
    if( rc != 0 ){
        fprintf(stderr, "viki cache push: 'fossil uv add' failed (exit %d)\n", rc);
        return 1;
    }

    fprintf(stderr, "viki cache push: %s uv sync\n", fossil);
    rc = run(argvSync);
    if( rc != 0 ){
        fprintf(stderr, "viki cache push: 'fossil uv sync' failed (exit %d)\n", rc);
        return 1;
    }
    return 0;
}

int viki_cmd_cache_pull(const char *zCacheDbPath){
    const char *fossil = viki_fossil_binary();
    char *argvSync[] = { (char*)fossil, "uv", "sync", NULL };
    char *argvExport[] = { (char*)fossil, "uv", "export", VIKI_UV_NAME, (char*)zCacheDbPath, NULL };
    int rc;

    fprintf(stderr, "viki cache pull: %s uv sync\n", fossil);
    rc = run(argvSync);
    if( rc != 0 ){
        fprintf(stderr, "viki cache pull: 'fossil uv sync' failed (exit %d)\n", rc);
        return 1;
    }

    fprintf(stderr, "viki cache pull: %s uv export %s %s\n", fossil, VIKI_UV_NAME, zCacheDbPath);
    rc = run(argvExport);
    if( rc != 0 ){
        fprintf(stderr,
            "viki cache pull: 'fossil uv export' failed (exit %d) -- "
            "if this is a fresh clone with nothing pushed yet, that's expected\n", rc);
        return 1;
    }
    return 0;
}

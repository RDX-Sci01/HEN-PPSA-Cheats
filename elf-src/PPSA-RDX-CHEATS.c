#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

/* prospero-clang's limits.h may not expose SIZE_MAX — define it portably */
#ifndef SIZE_MAX
#  define SIZE_MAX ((size_t)-1)
#endif
#include <curl/curl.h>
#include <minizip/unzip.h>

/* ------------------------------------------------------------------ */
/*  PATH CONSTANTS                                                      */
/* ------------------------------------------------------------------ */
#define TMP_DIR      "/data/etaHEN/tmp"
#define ZIP_FILE     "/data/etaHEN/tmp/cheats.zip"
#define EXTRACT_DIR  "/data/etaHEN/tmp/extracted"

/* The zip contains a "cheats/" subfolder; deploy its contents here   */
#define CHEATS_SRC   "/data/etaHEN/tmp/extracted/cheats"
#define CHEATS_DST   "/data/etaHEN/cheats"

#define DOWNLOAD_URL \
    "https://github.com/RDX-Sci01/HEN-PPSA-Cheats/releases/download/latest/cheats.zip"

/* ------------------------------------------------------------------ */
/*  NOTIFY                                                              */
/* ------------------------------------------------------------------ */
#include <stdarg.h>

typedef struct notify_request {
    char unused[45];
    char message[3075];
} notify_request_t;

extern int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify_popup(const char *fmt, ...)
{
    notify_request_t req = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message) - 1, fmt, args);
    va_end(args);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* Also log to stdout so nothing is lost if the popup system is unavailable */
#define notify(ok, msg)     do { notify_popup("%s", (msg)); \
                                 printf("[NOTIFY][%s] %s\n", (ok) ? "OK" : "FAIL", (msg)); } while(0)
#define notify_system(...)  do { notify_popup(__VA_ARGS__); \
                                 printf(__VA_ARGS__); } while(0)

/* ------------------------------------------------------------------ */
/*  CURL WRITE CONTEXT                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int fd;
} download_ctx_t;

/* ------------------------------------------------------------------ */
/*  SAFE WRITE — writes every byte or returns false                    */
/* ------------------------------------------------------------------ */
static bool write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            perror("write");
            return false;
        }
        p   += (size_t)w;
        len -= (size_t)w;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  CURL WRITE CALLBACK                                                 */
/* ------------------------------------------------------------------ */
static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    download_ctx_t *ctx   = (download_ctx_t *)userdata;
    size_t          total = size * nmemb;
    return write_all(ctx->fd, ptr, total) ? total : 0;
}

/* ------------------------------------------------------------------ */
/*  CURL PROGRESS CALLBACK                                              */
/* ------------------------------------------------------------------ */
static int progress_callback(void      *clientp,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow)
{
    (void)clientp; (void)ultotal; (void)ulnow;

    if (dltotal > 0)
        printf("\rDownloading... %.1f%%   ",
               (double)dlnow / (double)dltotal * 100.0);
    else
        printf("\rDownloading... %lld bytes   ", (long long)dlnow);

    fflush(stdout);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  MKDIR -P                                                            */
/* ------------------------------------------------------------------ */
static void mkdir_p(const char *path)
{
    char   tmp[1024];
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (!len) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ------------------------------------------------------------------ */
/*  PARENT DIR                                                          */
/* ------------------------------------------------------------------ */
static void parent_dir(const char *path, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash && slash != out) *slash = '\0';
    else if (slash == out)     out[1] = '\0';
}

/* ------------------------------------------------------------------ */
/*  RECURSIVE REMOVE (iterative — avoids stack overflow on PS5)       */
/* ------------------------------------------------------------------ */
#define RM_STACK_MAX 4096

static void remove_recursive(const char *root)
{
    char (*dir_stack)[1024]  = malloc(sizeof(*dir_stack)  * RM_STACK_MAX);
    if (!dir_stack) return;
    char (*work_stack)[1024] = malloc(sizeof(*work_stack) * RM_STACK_MAX);
    if (!work_stack) { free(dir_stack); return; }

    int dir_top  = 0;
    int work_top = 0;

    snprintf(work_stack[work_top++], 1024, "%s", root);

    while (work_top > 0) {
        char cur[1024];
        snprintf(cur, sizeof(cur), "%s", work_stack[--work_top]);

        struct stat st;
        if (lstat(cur, &st) < 0) continue;

        if (!S_ISDIR(st.st_mode)) {
            unlink(cur);
            continue;
        }

        if (dir_top < RM_STACK_MAX)
            snprintf(dir_stack[dir_top++], 1024, "%s", cur);

        DIR *dir = opendir(cur);
        if (!dir) continue;

        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;

            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", cur, e->d_name);

            struct stat cs;
            if (lstat(child, &cs) < 0) continue;

            if (S_ISDIR(cs.st_mode)) {
                if (work_top < RM_STACK_MAX)
                    snprintf(work_stack[work_top++], 1024, "%s", child);
            } else {
                unlink(child);
            }
        }
        closedir(dir);
    }

    for (int i = dir_top - 1; i >= 0; i--)
        rmdir(dir_stack[i]);

    free(work_stack);
    free(dir_stack);
}

/* ------------------------------------------------------------------ */
/*  DOWNLOAD FILE VIA CURL                                              */
/* ------------------------------------------------------------------ */
static bool download_file(const char *url, const char *out_path)
{
    CURL *curl = curl_easy_init();
    if (!curl) { notify_system("curl_easy_init failed\n"); return false; }

    unlink(out_path);

    int fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror(out_path);
        curl_easy_cleanup(curl);
        return false;
    }

    download_ctx_t ctx = { .fd = fd };

    curl_easy_setopt(curl, CURLOPT_URL,              url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,        &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,   0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,   0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);

    CURLcode res = curl_easy_perform(curl);
    printf("\n");

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    close(fd);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        notify_system("curl error: %s\n", curl_easy_strerror(res));
        unlink(out_path);
        return false;
    }
    if (http_code != 200) {
        notify_system("HTTP error: %ld\n", http_code);
        unlink(out_path);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  EXTRACT ZIP (path-traversal-safe)                                  */
/*                                                                      */
/*  Critical discipline: every code path that calls                    */
/*  unzOpenCurrentFile() MUST call unzCloseCurrentFile() before        */
/*  jumping to next_entry, or the iterator state is corrupted and      */
/*  subsequent entries are silently skipped.                            */
/* ------------------------------------------------------------------ */
static bool extract_zip(const char *zip_path, const char *out_dir)
{
    unzFile zip = unzOpen(zip_path);
    if (!zip) {
        notify_system("unzOpen failed: %s\n", zip_path);
        return false;
    }

    mkdir_p(out_dir);

    int go = unzGoToFirstFile(zip);
    if (go != UNZ_OK) {
        notify_system("zip is empty or corrupt (code %d)\n", go);
        unzClose(zip);
        return false;
    }

    int total = 0, skipped = 0;
    char filename[512];
    char fullpath[1024];

    for (;;) {
        /* --- get entry metadata --- */
        if (unzGetCurrentFileInfo(zip, NULL, filename, sizeof(filename),
                                  NULL, 0, NULL, 0) != UNZ_OK) {
            notify_system("unzGetCurrentFileInfo failed — skipping entry\n");
            goto next_entry;
        }

        /* --- path-traversal guard --- */
        if (strstr(filename, "../") || strstr(filename, "..\\") ||
            filename[0] == '/')
            goto next_entry;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", out_dir, filename);

        /* --- directory entry --- */
        {
            size_t flen = strlen(filename);
            if (flen && filename[flen - 1] == '/') {
                mkdir_p(fullpath);
                goto next_entry;   /* no file data — no open/close needed */
            }
        }

        /* --- ensure parent dir exists --- */
        {
            char par[1024];
            parent_dir(fullpath, par, sizeof(par));
            mkdir_p(par);
        }

        /* --- open zip entry --- */
        if (unzOpenCurrentFile(zip) != UNZ_OK) {
            notify_system("unzOpenCurrentFile failed: %s\n", filename);
            goto next_entry;   /* entry was never opened — no close needed */
        }

        /* --- write to disk --- */
        {
            int fd = open(fullpath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0) {
                notify_system("open failed (errno=%d): %s\n", errno, fullpath);
                unzCloseCurrentFile(zip);   /* must close before skipping */
                skipped++;
                goto next_entry;
            }

            char buf[262144];   /* 256 KB — fewer round-trips per file */
            int  bytes;
            bool ok = true;

            while ((bytes = unzReadCurrentFile(zip, buf, sizeof(buf))) > 0) {
                if (!write_all(fd, buf, (size_t)bytes)) { ok = false; break; }
            }
            if (bytes < 0) {
                notify_system("decompression error in: %s\n", filename);
                ok = false;
            }

            close(fd);
            unzCloseCurrentFile(zip);

            if (!ok) {
                unlink(fullpath);
                unzClose(zip);
                return false;
            }

            total++;
            if (total % 100 == 0)
                notify_popup("Extracting... %d files done 🐑 ", total);
        }

next_entry:
        {
            int next = unzGoToNextFile(zip);
            if (next == UNZ_END_OF_LIST_OF_FILE) break;
            if (next != UNZ_OK) {
                notify_system("zip navigation error (code %d)\n", next);
                unzClose(zip);
                return false;
            }
        }
    }

    unzClose(zip);
    notify_popup("Extraction complete — %d files extracted 🐑", total);
    return true;
}

/* ------------------------------------------------------------------ */
/*  ATOMIC DEPLOY — remove old dst, rename src into place             */
/* ------------------------------------------------------------------ */
static bool atomic_deploy_dir(const char *src_dir, const char *dst_dir)
{
    remove_recursive(dst_dir);

    if (rename(src_dir, dst_dir) != 0) {
        perror("rename");
        notify_system("atomic rename failed: %s -> %s\n", src_dir, dst_dir);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  MAIN                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    int exit_code = 1;

    notify(true, "Starting cheat update 🐑🐑 ");

    mkdir_p(TMP_DIR);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* ---- 1. Download ---- */
    notify(true, "Downloading cheats.zip... 🐑 ");
    if (!download_file(DOWNLOAD_URL, ZIP_FILE)) {
        notify(false, "Download failed");
        goto cleanup;
    }

    /* ---- 2. Extract ---- */
    notify(true, "Extracting archive... 🐑 ");
    remove_recursive(EXTRACT_DIR);   /* clear any leftovers from prior run */
    if (!extract_zip(ZIP_FILE, EXTRACT_DIR)) {
        notify(false, "Extraction failed");
        goto cleanup;
    }

    /* ---- 3. Atomic deploy ---- */
    notify(true, "Deploying cheats... 🐑🐑 ");
    {
        char dst_parent[1024];
        parent_dir(CHEATS_DST, dst_parent, sizeof(dst_parent));
        mkdir_p(dst_parent);
    }
    if (!atomic_deploy_dir(CHEATS_SRC, CHEATS_DST)) {
        notify(false, "Deploy failed");
        goto cleanup;
    }

    notify(true, "Cheats updated successfully! 🐑🐑🐑🐑 ");
    exit_code = 0;

cleanup:
    notify(true, "Cleaning up...");
    remove_recursive(EXTRACT_DIR);
    unlink(ZIP_FILE);
    curl_global_cleanup();
    notify(true, "Done 🐑 ");

    return exit_code;
}

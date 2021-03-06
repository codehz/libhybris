#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <fcntl.h>
#include <wchar.h>
#include <stdio.h>
#ifndef __APPLE__
#include <stdio_ext.h>
#endif

#include "../include/hybris/hook.h"
#include "../include/hybris/binding.h"

struct open_redirect {
    const char *from;
    const char *to;
};

struct open_redirect open_redirects[] = {
        { "/dev/log/main", "/dev/log_main" },
        { "/dev/log/radio", "/dev/log_radio" },
        { "/dev/log/system", "/dev/log_system" },
        { "/dev/log/events", "/dev/log_events" },
        { NULL, NULL }
};

#ifdef __APPLE__

int darwin_convert_fd_flags_to_native(int flags)
{
    int ret = flags & 4;
    if (flags & 0100) ret |= O_CREAT;
    if (flags & 0200) ret |= O_EXCL;
    if (flags & 01000) ret |= O_TRUNC;
    if (flags & 02000) ret |= O_APPEND;
    if (flags & 04000) ret |= O_NONBLOCK;
    return ret;
}

int darwin_convert_fd_flags_from_native(int flags)
{
    int ret = flags & 4;
    if (flags & O_CREAT)
        ret |= 0100;
    return ret;
}

#endif

int my_open(const char *pathname, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;
    const char *target_path = pathname;

    if (pathname != NULL) {
        struct open_redirect *entry = &open_redirects[0];
        while (entry->from != NULL) {
            if (strcmp(pathname, entry->from) == 0) {
                target_path = entry->to;
                break;
            }
            entry++;
        }
    }

#ifdef __APPLE__
    flags = darwin_convert_fd_flags_to_native(flags);
#endif

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }

    return open(target_path, flags, mode);
}


struct bionic_stat64 {
    unsigned long long st_dev;
    unsigned char __pad0[4];
    unsigned long __st_ino;
    unsigned int st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    unsigned long long st_rdev;
    unsigned char __pad3[4];
    long long st_size;
    unsigned long st_blksize;
    unsigned long long st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    unsigned long long st_ino;
};

void stat_to_bionic_stat(struct stat *s, struct bionic_stat64 *b) {
    b->st_dev = s->st_dev;
    b->__st_ino = s->st_ino;
    b->st_mode = s->st_mode;
    b->st_nlink = s->st_nlink;
    b->st_uid = s->st_uid;
    b->st_gid = s->st_gid;
    b->st_rdev = s->st_rdev;
    b->st_size = s->st_size;
    b->st_blksize = (unsigned long) s->st_blksize;
    b->st_blocks = (unsigned long long) s->st_blocks;
#ifdef __APPLE__
    b->st_atim = s->st_atimespec;
    b->st_mtim = s->st_mtimespec;
    b->st_ctim = s->st_ctimespec;
#else
    b->st_atim = s->st_atim;
    b->st_mtim = s->st_mtim;
    b->st_ctim = s->st_ctim;
#endif
    b->st_ino = s->st_ino;
}

#ifndef __APPLE__
void stat64_to_bionic_stat(struct stat64 *s, struct bionic_stat64 *b) {
    b->st_dev = s->st_dev;
    b->__st_ino = s->__st_ino;
    b->st_mode = s->st_mode;
    b->st_nlink = s->st_nlink;
    b->st_uid = s->st_uid;
    b->st_gid = s->st_gid;
    b->st_rdev = s->st_rdev;
    b->st_size = s->st_size;
    b->st_blksize = (unsigned long) s->st_blksize;
    b->st_blocks = (unsigned long long) s->st_blocks;
    b->st_atim = s->st_atim;
    b->st_mtim = s->st_mtim;
    b->st_ctim = s->st_ctim;
    b->st_ino = s->st_ino;
}
#endif

int my_stat(const char* path, struct bionic_stat64 *s)
{
    struct stat tmp;
    int ret = stat(path, &tmp);
    stat_to_bionic_stat(&tmp, s);
    return ret;
}

int my_fstat(int fd, struct bionic_stat64 *s)
{
    struct stat tmp;
    int ret = fstat(fd, &tmp);
    stat_to_bionic_stat(&tmp, s);
    return ret;
}

#ifndef __APPLE__
int my_stat64(const char* path, struct bionic_stat64 *s)
{
    struct stat64 tmp;
    int ret = stat64(path, &tmp);
    stat64_to_bionic_stat(&tmp, s);
    return ret;
}

int my_fstat64(int fd, struct bionic_stat64 *s)
{
    struct stat64 tmp;
    int ret = fstat64(fd, &tmp);
    stat64_to_bionic_stat(&tmp, s);
    return ret;
}
#endif

#ifdef __APPLE__

struct android_flock {
    short l_type;
    short l_whence;
    long l_start;
    long l_len;
    long l_sysid;
    int l_pid;
    long pad[4];
};

int darwin_my_fcntl(int fd, int cmd, ...)
{
    int ret = -1;
    va_list ap;
    va_start(ap, cmd);
    if (cmd == 2) {
        int flags = va_arg(ap, int);
        ret = fcntl(fd, F_SETFD, flags);
    } else if (cmd == 4) {
        int flags = va_arg(ap, int);
        ret = fcntl(fd, F_SETFL, darwin_convert_fd_flags_to_native(flags));
    } else if (cmd == 6) {
        struct android_flock* afl = va_arg(ap, struct android_flock*);
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = afl->l_type;
        fl.l_whence = afl->l_whence;
        fl.l_start = afl->l_start;
        fl.l_len = afl->l_len;
        fl.l_pid = afl->l_pid;
        ret = fcntl(fd, F_SETLK, &fl);
    } else {
        printf("unsupported fcntl %i\n", cmd);
    }
    va_end(ap);
    return ret;
}

#endif


/*
 * __isthreaded is used in bionic's stdio.h to choose between a fast internal implementation
 * and a more classic stdio function call.
 * For example:
 * #define  __sfeof(p)  (((p)->_flags & __SEOF) != 0)
 * #define  feof(p)     (!__isthreaded ? __sfeof(p) : (feof)(p))
 *
 * We see here that if __isthreaded is false, then it will use directly the bionic's FILE structure
 * instead of calling one of the hooked methods.
 * Therefore we need to set __isthreaded to true, even if we are not in a multi-threaded context.
 */
static int __my_isthreaded = 1;

struct aFILE {
    unsigned char* _p;
    int	_r, _w;
#if defined(__LP64__)
    int	_flags, _file;
#else
    short _flags, _file;
#endif
    FILE* actual;
};

/*
 * redirection for bionic's __sF, which is defined as:
 *   FILE __sF[3];
 *   #define stdin  &__sF[0];
 *   #define stdout &__sF[1];
 *   #define stderr &__sF[2];
 *   So the goal here is to catch the call to file methods where the FILE* pointer
 *   is either stdin, stdout or stderr, and translate that pointer to a valid glibc
 *   pointer.
 *   Currently, only fputs is managed.
 */
#define BIONIC_SIZEOF_FILE 84
static char my_sF[3*BIONIC_SIZEOF_FILE] = {0};
static FILE *_get_actual_fp(struct aFILE *fp)
{
    char *c_fp = (char*)fp;
    if (c_fp == &my_sF[0])
        return stdin;
    else if (c_fp == &my_sF[BIONIC_SIZEOF_FILE])
        return stdout;
    else if (c_fp == &my_sF[BIONIC_SIZEOF_FILE*2])
        return stderr;

    return fp->actual;
}

static void my_clearerr(struct aFILE *fp)
{
    clearerr(_get_actual_fp(fp));
}

static struct aFILE* my_fopen(const char *filename, const char *mode)
{
    FILE* file = fopen(filename, mode);
    if (file == NULL)
        return NULL;
    struct aFILE* afile = (struct aFILE*) malloc(sizeof(struct aFILE));
    afile->_file = (short) fileno(file);
    afile->actual = file;
    afile->_flags = 0;
    return afile;
}

static struct aFILE* my_fdopen(int fd, const char *mode)
{
    FILE* file = fdopen(fd, mode);
    if (file == NULL)
        return NULL;
    struct aFILE* afile = (struct aFILE*) malloc(sizeof(struct aFILE));
    afile->_file = (short) fileno(file);
    afile->actual = file;
    return afile;
}

static int my_fclose(struct aFILE* fp)
{
    return fclose(_get_actual_fp(fp));
}

static int my_feof(struct aFILE* fp)
{
    return feof(_get_actual_fp(fp));
}

static int my_ferror(struct aFILE* fp)
{
    return ferror(_get_actual_fp(fp));
}

static int my_fflush(struct aFILE* fp)
{
    // return fflush(_get_actual_fp(fp));
    return 0;
}

static int my_fgetc(struct aFILE* fp)
{
    return fgetc(_get_actual_fp(fp));
}

static int my_fgetpos(struct aFILE* fp, fpos_t *pos)
{
    return fgetpos(_get_actual_fp(fp), pos);
}

static char* my_fgets(char *s, int n, struct aFILE* fp)
{
    return fgets(s, n, _get_actual_fp(fp));
}

FP_ATTRIB static int my_fprintf(struct aFILE* fp, const char *fmt, ...)
{
    int ret = 0;

    va_list args;
    va_start(args,fmt);
    ret = vfprintf(_get_actual_fp(fp), fmt, args);
    va_end(args);

    return ret;
}

static int my_fputc(int c, struct aFILE* fp)
{
    return fputc(c, _get_actual_fp(fp));
}

static int my_fputs(const char *s, struct aFILE* fp)
{
    return fputs(s, _get_actual_fp(fp));
}

static size_t my_fread(void *ptr, size_t size, size_t nmemb, struct aFILE* fp)
{
    size_t ret = fread(ptr, size, nmemb, _get_actual_fp(fp));
    fp->_flags = (short) (feof_unlocked(_get_actual_fp(fp)) ? 0x0020 : 0);
    return ret;
}

static FILE* my_freopen(const char *filename, const char *mode, struct aFILE* fp)
{
    return freopen(filename, mode, _get_actual_fp(fp));
}

FP_ATTRIB static int my_fscanf(struct aFILE* fp, const char *fmt, ...)
{
    int ret = 0;

    va_list args;
    va_start(args,fmt);
    ret = vfscanf(_get_actual_fp(fp), fmt, args);
    va_end(args);

    return ret;
}

static int my_fseek(struct aFILE* fp, long offset, int whence)
{
    return fseek(_get_actual_fp(fp), offset, whence);
}

static int my_fseeko(struct aFILE* fp, off_t offset, int whence)
{
    return fseeko(_get_actual_fp(fp), offset, whence);
}

static int my_fsetpos(struct aFILE* fp, const fpos_t *pos)
{
    return fsetpos(_get_actual_fp(fp), pos);
}

static long my_ftell(struct aFILE* fp)
{
    return ftell(_get_actual_fp(fp));
}

static off_t my_ftello(struct aFILE* fp)
{
    return ftello(_get_actual_fp(fp));
}

static void (*record)(const void *ptr, size_t length);

static size_t set_record(void (*r)(const void *ptr, size_t length)) {
    record = r;
}

static size_t my_fwrite(const void *ptr, size_t size, size_t nmemb, struct aFILE* fp)
{
    FILE *f = _get_actual_fp(fp);
    if (f == stdout) {
        if (record) {
            record(ptr, size * nmemb);
        }
        return size * nmemb;
    }
    return fwrite(ptr, size, nmemb, f);
}

static int my_getc(struct aFILE* fp)
{
    return getc(_get_actual_fp(fp));
}

static ssize_t my_getdelim(char ** lineptr, size_t *n, int delimiter, struct aFILE* fp)
{
    return getdelim(lineptr, n, delimiter, _get_actual_fp(fp));
}

static ssize_t my_getline(char **lineptr, size_t *n, struct aFILE* fp)
{
    return getline(lineptr, n, _get_actual_fp(fp));
}

static int my_putc(int c, struct aFILE* fp)
{
    FILE *f = _get_actual_fp(fp);
    if (f == stdout) {
        if (record) {
            record(&c, 1);
        }
        return c;
    }
    return putc(c, f);
}

static void my_rewind(struct aFILE* fp)
{
    rewind(_get_actual_fp(fp));
}

static void my_setbuf(struct aFILE* fp, char *buf)
{
    setbuf(_get_actual_fp(fp), buf);
}

static int my_setvbuf(struct aFILE* fp, char *buf, int mode, size_t size)
{
    return setvbuf(_get_actual_fp(fp), buf, mode, size);
}

static int my_ungetc(int c, struct aFILE* fp)
{
    return ungetc(c, _get_actual_fp(fp));
}

static int my_vfprintf(struct aFILE* fp, const char *fmt, va_list arg)
{
    return vfprintf(_get_actual_fp(fp), fmt, arg);
}


static int my_vfscanf(struct aFILE* fp, const char *fmt, va_list arg)
{
    return vfscanf(_get_actual_fp(fp), fmt, arg);
}

static int my_fileno(struct aFILE* fp)
{
    return fileno(_get_actual_fp(fp));
}


static int my_pclose(struct aFILE* fp)
{
    return pclose(_get_actual_fp(fp));
}

static void my_flockfile(struct aFILE* fp)
{
    return flockfile(_get_actual_fp(fp));
}

static int my_ftrylockfile(struct aFILE* fp)
{
    return ftrylockfile(_get_actual_fp(fp));
}

static void my_funlockfile(struct aFILE* fp)
{
    return funlockfile(_get_actual_fp(fp));
}


static int my_getc_unlocked(struct aFILE* fp)
{
    return getc_unlocked(_get_actual_fp(fp));
}

static int my_putc_unlocked(int c, struct aFILE* fp)
{
    return putc_unlocked(c, _get_actual_fp(fp));
}

/* exists only on the BSD platform
static char* my_fgetln(FILE *fp, size_t *len)
{
    return fgetln(_get_actual_fp(fp), len);
}
*/

static int my_fpurge(struct aFILE* fp)
{
#ifdef __APPLE__
    fpurge(_get_actual_fp(fp));
#else
    __fpurge(_get_actual_fp(fp));
#endif

    return 0;
}

static int my_getw(struct aFILE* fp)
{
    return getw(_get_actual_fp(fp));
}

static int my_putw(int w, struct aFILE* fp)
{
    return putw(w, _get_actual_fp(fp));
}

static void my_setbuffer(struct aFILE* fp, char *buf, int size)
{
    setbuffer(_get_actual_fp(fp), buf, size);
}

static int my_setlinebuf(struct aFILE* fp)
{
    setlinebuf(_get_actual_fp(fp));

    return 0;
}

static wint_t my_getwc(struct aFILE* fp)
{
    return getwc(_get_actual_fp(fp));
}

static wint_t my_ungetwc(wint_t c, struct aFILE* fp)
{
    return ungetwc(c, _get_actual_fp(fp));
}

static wint_t my_putwc(wchar_t c, struct aFILE* fp)
{
    return putwc(c, _get_actual_fp(fp));
}

#ifdef __APPLE__

int darwin_my_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    // Mac OS has a broken poll implementation
    struct timeval t;
    t.tv_sec = timeout / 1000;
    t.tv_usec = (timeout % 1000) * 1000;

    fd_set r_fdset, w_fdset, e_fdset;
    int maxfd = 0;
    FD_ZERO(&r_fdset);
    FD_ZERO(&w_fdset);
    FD_ZERO(&e_fdset);
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].fd > maxfd)
            maxfd = fds[i].fd;
        if (fds[i].events & POLLIN || fds[i].events & POLLPRI)
            FD_SET(fds[i].fd, &r_fdset);
        if (fds[i].events & POLLOUT)
            FD_SET(fds[i].fd, &w_fdset);
        FD_SET(fds[i].fd, &e_fdset);
    }
    int ret = select(maxfd + 1, &r_fdset, &w_fdset, &e_fdset, &t);
    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (FD_ISSET(fds[i].fd, &r_fdset))
            fds[i].revents |= POLLIN;
        if (FD_ISSET(fds[i].fd, &w_fdset))
            fds[i].revents |= POLLOUT;
        if (FD_ISSET(fds[i].fd, &e_fdset))
            fds[i].revents |= POLLERR;
    }
    return ret;
}

#endif

struct _hook io_hooks[] = {
    /* fcntl.h */
    {"open", my_open},
#ifdef __APPLE__
    {"fcntl", darwin_my_fcntl},
#else
    {"fcntl", fcntl},
#endif
    /* sys/stat.h */
    {"stat", my_stat},
    {"fstat", my_fstat},
#ifdef __APPLE__
    {"stat64", my_stat},
    {"fstat64", my_fstat},
#else
    {"stat64", my_stat64},
    {"fstat64", my_fstat64},
#endif
    {"chmod", chmod},
    {"fchmod", fchmod},
    {"umask", umask},
    {"mkdir", mkdir},
    /* stdio.h */
    {"__isthreaded", &__my_isthreaded},
    {"__sF", &my_sF},
    {"fopen", my_fopen},
    {"fdopen", my_fdopen},
    {"popen", popen},
    {"puts", puts},
    {"sprintf", sprintf},
    {"asprintf", asprintf},
    {"vasprintf", vasprintf},
    {"snprintf", snprintf},
    {"vsprintf", vsprintf},
    {"vsnprintf", vsnprintf},
    {"clearerr", my_clearerr},
    {"fclose", my_fclose},
    {"feof", my_feof},
    {"ferror", my_ferror},
    {"fflush", my_fflush},
    {"fgetc", my_fgetc},
    {"fgetpos", my_fgetpos},
    {"fgets", my_fgets},
    {"fprintf", my_fprintf},
    {"fputc", my_fputc},
    {"fputs", my_fputs},
    {"fread", my_fread},
    {"freopen", my_freopen},
    {"fscanf", my_fscanf},
    {"fseek", my_fseek},
    {"fseeko", my_fseeko},
    {"fsetpos", my_fsetpos},
    {"ftell", my_ftell},
    {"ftello", my_ftello},
    {"fwrite", my_fwrite},
    {"getc", my_getc},
    {"getdelim", my_getdelim},
    {"getline", my_getline},
    {"putc", my_putc},
    {"rewind", my_rewind},
    {"setbuf", my_setbuf},
    {"setvbuf", my_setvbuf},
    {"ungetc", my_ungetc},
    {"vfprintf", my_vfprintf},
    {"vfscanf", my_vfscanf},
    {"fileno", my_fileno},
    {"pclose", my_pclose},
    {"perror", perror},
    {"flockfile", my_flockfile},
    {"ftrylockfile", my_ftrylockfile},
    {"funlockfile", my_funlockfile},
    {"getc_unlocked", my_getc_unlocked},
    {"putc_unlocked", my_putc_unlocked},
    //{"fgetln", my_fgetln},
    {"fpurge", my_fpurge},
    {"getw", my_getw},
    {"putw", my_putw},
    {"setbuffer", my_setbuffer},
    {"setlinebuf", my_setlinebuf},
    {"remove", remove},
    {"rename", rename},
    /* wchar.t */
    {"getwc", my_getwc},
    {"ungetwc", my_ungetwc},
    {"putwc", my_putwc},
    /* poll.h */
#ifdef __APPLE__
    {"poll", darwin_my_poll},
#else
    {"poll", poll},
#endif
    {"select", select},
    {"set_record", set_record},
    {NULL, NULL}
};
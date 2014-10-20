/*
 * ls
 *
 * Advanced Programming in the UNIX Environment - Fall 2014
 * Midterm Assignment
 * http://www.cs.stevens.edu/~jschauma/631/f14-midterm.html
 *
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <bsd/stdlib.h>
#include <bsd/string.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <grp.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#define DEFAULT_BLOCKSIZE 512
#define DEFAULT_TERMWIDTH 80
#define MAX_HUMAN_LEN 4
#define WARN(...) do { warn(__VA_ARGS__); rval++; } while(0)

static enum {
    /* -f,   default    -t,        -S     */
    NO_SORT, NAME_SORT, TIME_SORT, SIZE_SORT
} sort_method = NAME_SORT;

static enum {
    /* -t,     -u,        -c     */
    USE_MTIME, USE_ATIME, USE_CTIME
} time_to_use = USE_MTIME;

static bool reversed_sort   = false;    /* -r */
static bool print_inode     = false;    /* -i */
static bool print_blocks    = false;    /* -s */
static bool print_indicator = false;    /* -F */
static bool print_dir       = false;    /* -d */
static bool print_id        = false;    /* -n */
static bool long_format     = false;    /* -l */
static bool humanize        = false;    /* -h */
static bool raw_print       = false;    /* -q, -w */
static bool by_column       = true;     /* -C */
static bool horizontal      = false;    /* -x */
static bool show_hidden     = false;    /* -a, -A */
static int  block_size      = DEFAULT_BLOCKSIZE;
static int  terminal_width  = DEFAULT_TERMWIDTH;
static int  rval            = EXIT_SUCCESS;
static bool line_break_before_dir = false;
static time_t six_month_ago;

static void
usage(void)
{
    (void)fprintf(stderr, "Usage: ls [-AacCdFfhiklnqRrSstuwx1] [file ...]\n");
    exit(EXIT_FAILURE);
}

static inline int
uint_length(uintmax_t n)
{
    if (n < 10)
        return 1;
    return floor(log10(n)) + 1;
}

static inline uintmax_t
get_blocks(uintmax_t blocks)
{
    return (blocks * S_BLKSIZE + block_size - 1) / block_size;
}

static inline const char *
get_username(uid_t uid)
{
    struct passwd *p = getpwuid(uid);
    if (p)
        return p->pw_name;
    return NULL;
}

static inline const char *
get_groupname(gid_t gid)
{
    struct group *g = getgrgid(gid);
    if (g)
        return g->gr_name;
    return NULL;
}

static inline time_t
get_time(struct stat *s)
{
    switch (time_to_use) {
    case USE_ATIME:
        return s->st_atime;
    case USE_CTIME:
        return s->st_ctime;
    case USE_MTIME:
    default:
        return s->st_mtime;
    }
}

static inline const char *
get_human(uintmax_t n)
{
    static char human_buf[MAX_HUMAN_LEN + 1];
    if (humanize_number(human_buf, sizeof(human_buf), n, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE) == -1)
        WARN("humanize_number");
    return human_buf;
}

/*
 * compare function for fts_open()
 */
static int
ftscmp(const FTSENT **a, const FTSENT **b)
{
    if ((*a)->fts_level == FTS_ROOTLEVEL) {
        if ((*a)->fts_info == FTS_D) {
            if ((*b)->fts_info != FTS_D)
                return 1;
        } else {
            if ((*b)->fts_info == FTS_D)
                return -1;
        }
    }
    if (sort_method == NO_SORT)
        return 0;

    int r = reversed_sort ? -1 : 1;

    if ((*a)->fts_info == FTS_NS) {
        if ((*b)->fts_info != FTS_NS)
            return 1;
    } else if ((*b)->fts_info == FTS_NS)
        return -1;
    else {
        switch (sort_method) {
        case TIME_SORT: {
            time_t atime = get_time((*a)->fts_statp);
            time_t btime = get_time((*b)->fts_statp);
            if (atime < btime)
                return r;
            else if (atime > btime)
                return -r;
            break;
        }
        case SIZE_SORT:
            if ((*a)->fts_statp->st_size < (*b)->fts_statp->st_size)
                return r;
            else if ((*a)->fts_statp->st_size > (*b)->fts_statp->st_size)
                return -r;
        case NAME_SORT:
        default:
            break;
        }
    }
    return r * strcmp((*a)->fts_name, (*b)->fts_name);
}

static inline FTSENT *
next_visable_fts(FTSENT *p)
{
    if (p)
        do
            p = p->fts_link;
        while (p && p->fts_number == 0);
    return p;
}

static void
get_width(int width[], FTSENT *p, int n_columns, int n_rows)
{
    memset(width, 0, n_columns * sizeof(int));
    for (int i = 0; p; p = next_visable_fts(p), i++) {
        if (horizontal) {
            if (width[i % n_columns] < p->fts_namelen)
                width[i % n_columns] = p->fts_namelen;
        } else {
            if (width[i / n_rows] < p->fts_namelen)
                width[i / n_rows] = p->fts_namelen;
        }
    }
}

static void
adjust_column(FTSENT *head, int item_count, int others_width)
{
    FTSENT *p;
    int i;
    int width[item_count];
    int max_columns = 1;
    int n_rows;

    /* calculate maximum number of columns */
    for (int n_columns = 2; n_columns <= item_count; ) {
        n_rows = (item_count + n_columns - 1) / n_columns;
        get_width(width, head, n_columns, n_rows);

        int sum = others_width * n_columns;
        for (i = 0; i < n_columns; i++)
            sum += width[i];
        if (sum > terminal_width)
            break;

        max_columns = n_columns;

        if (n_columns < n_rows || horizontal)
            n_columns++;
        else if (n_rows > 1)
            n_columns = (item_count + n_rows - 2) / (n_rows - 1);
        else
            break;
    }
    if (max_columns == 1)
        return;

    n_rows = (item_count + max_columns - 1) / max_columns;
    get_width(width, head, max_columns, n_rows);

    if (horizontal || n_rows == 1) {
        for (p = head, i = 0; p; p = next_visable_fts(p)) {
            p->fts_number = width[i++ % max_columns];
            /* if is not last one of the row */
            if (i % max_columns && i < item_count)
                p->fts_pointer = p; /* just make it != NULL */
        }
        return;
    }

    /* make fts_pointer points to the item on the right */
    FTSENT *q = head;
    for (i = 0; i < n_rows; i++)
        q = next_visable_fts(q);
    for (p = head, i = 0; p; p = next_visable_fts(p)) {
        p->fts_number = width[i++ / n_rows];
        p->fts_pointer = q;
        q = next_visable_fts(q);
    }
    /* rearrange the linked list */
    for (q = p = head, i = 0; i < n_rows; q = p) {
        p = next_visable_fts(p);
        while (q->fts_pointer)
            q = q->fts_link = q->fts_pointer;
        q->fts_link = ++i < n_rows ? p : NULL;
    }
}

/*
 * print non-printable characters as '?'
 */
static inline void
escape_print(char *str, int len)
{
    for (int i = 0; i < len; i++)
        putchar(isprint(str[i]) ? str[i] : '?');
}

/*
 * print file name, indicator and symbolic link
 */
static void
print_file_name(FTSENT *p)
{
    escape_print(p->fts_name, p->fts_namelen);

    if (print_indicator) {
        switch (p->fts_statp->st_mode & S_IFMT) {
        case S_IFDIR:
            putchar('/');
            break;
        case S_IFLNK:
            putchar('@');
            break;
        case S_IFIFO:
            putchar('|');
            break;
        case S_IFSOCK:
            putchar('=');
            break;
#ifdef S_IFWHT
        case S_IFWHT:
            putchar('%');
            break;
#endif
        default:
            if (p->fts_statp->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
                putchar('*');
            else if (by_column)
                putchar(' ');
        }
    }
    if (long_format && S_ISLNK(p->fts_statp->st_mode)) {
        int len = -1;
        char path[PATH_MAX], buf[PATH_MAX];

        if (p->fts_level != FTS_ROOTLEVEL) {
            len = strlen(p->fts_parent->fts_accpath);
            memcpy(path, p->fts_parent->fts_accpath, len);
            path[len] = '/';
        }
        strncpy(path + len + 1, p->fts_name, p->fts_namelen + 1);

        if ((len = readlink(path, buf, sizeof(buf) - 1)) == -1)
            WARN("cannot read symbolic link %s", path);
        else {
            buf[len] = 0;
            printf(" -> ");
            escape_print(buf, len);
        }
    }
    if (by_column && p->fts_pointer)
        printf("%*c", (int)(p->fts_number - p->fts_namelen + 1), ' ');
    else
        putchar('\n');
}

static void
print_fts_children(FTS *ftsp)
{
    FTSENT *head = fts_children(ftsp, 0);
    ino_t max_inode = 0;
    blkcnt_t max_blocks = 0;
    nlink_t max_nlink = 0;
    uintmax_t blocks_sum = 0;
    int max_uid = 0;
    int max_gid = 0;
    size_t max_size = 0;
    int max_major = 0;
    int max_minor = 0;
    int item_count = 0;
    const char *tmp;

    /* Calculate all the max */
    for (FTSENT *p = head; p; p = p->fts_link) {
        switch (p->fts_info) {
        case FTS_NS:
        case FTS_DNR:
        case FTS_ERR:
            warnx("%s: %s", p->fts_name, strerror(p->fts_errno));
            break;
        case FTS_D:
            if (p->fts_level == FTS_ROOTLEVEL && !print_dir)
                break;
            /* FALLTHROUGH */
        default:
            if (p->fts_name[0] == '.' && !show_hidden)
                break;
            item_count++;
            p->fts_number = 1;  /* flag it to print later */

            struct stat *sp = p->fts_statp;

            if (print_inode && max_inode < sp->st_ino)
                max_inode = sp->st_ino;

            blocks_sum += sp->st_blocks;
            if (print_blocks && max_blocks < sp->st_blocks)
                max_blocks = sp->st_blocks;

            if (long_format) {
                if (max_nlink < sp->st_nlink)
                    max_nlink = sp->st_nlink;
                if (print_id) {
                    if (max_uid < sp->st_uid)
                        max_uid = sp->st_uid;
                    if (max_gid < sp->st_gid)
                        max_gid = sp->st_gid;
                } else {
                    int len;
                    if ((tmp = get_username(sp->st_uid)) == NULL)
                        len = uint_length(sp->st_uid);
                    else
                        len = strlen(tmp);
                    if (max_uid < len)
                        max_uid = len;
                    if ((tmp = get_groupname(sp->st_gid)) == NULL)
                        len = uint_length(sp->st_gid);
                    else
                        len = strlen(tmp);
                    if (max_gid < len)
                        max_gid = len;
                }
                if (S_ISCHR(sp->st_mode) || S_ISBLK(sp->st_mode)) {
                    if (max_major < major(sp->st_rdev))
                        max_major = major(sp->st_rdev);
                    if (max_minor < minor(sp->st_rdev))
                        max_minor = minor(sp->st_rdev);
                } else if (max_size < sp->st_size)
                    max_size = sp->st_size;
            }
        }
    }

    if (item_count == 0)    /* nothing will be printed */
        return;

    if (print_blocks || long_format) {
        if (head->fts_level != FTS_ROOTLEVEL) {
            if (humanize)
                printf("total %s\n", get_human(blocks_sum * S_BLKSIZE));
            else
                printf("total %ju\n", get_blocks(blocks_sum));
        }
        if (print_blocks) {
            if (humanize)
                max_blocks = MAX_HUMAN_LEN;
            else
                max_blocks = uint_length(get_blocks(max_blocks));
        }
        if (long_format) {
            max_nlink = uint_length(max_nlink);
            if (print_id) {
                max_uid = uint_length(max_uid);
                max_gid = uint_length(max_gid);
            }
            max_size = uint_length(max_size);
            if (max_major) {    /* skip if no block or character file */
                max_major = uint_length(max_major);
                max_minor = uint_length(max_minor);
                if (max_size < max_major + max_minor + 2)
                    max_size = max_major + max_minor + 2;
                else
                    max_major = max_size - max_minor - 2;
            }
        }
    }
    if (print_inode)
        max_inode = uint_length(max_inode);

    if (head->fts_number == 0)
        head = next_visable_fts(head);

    if (by_column)
        /* if max_inode or max_blocks is not 0, add 1 more for space */
        adjust_column(head, item_count, 1 + print_indicator + max_inode + !!max_inode + max_blocks + !!max_blocks);

    for (FTSENT *p = head; p; p = next_visable_fts(p)) {
        struct stat *sp = p->fts_statp;
        if (print_inode)
            printf("%*ju ", (int)max_inode, (uintmax_t)sp->st_ino);
        if (print_blocks) {
            if (humanize)
                printf("%*s ", (int)max_blocks, get_human(sp->st_blocks * S_BLKSIZE));
            else
                printf("%*ju ", (int)max_blocks, get_blocks(sp->st_blocks));
        }
        if (long_format) {
            char buf[200];  /* 200 is from man page of strftime */
            strmode(sp->st_mode, buf);
            printf("%s %*ju ", buf, (int)max_nlink, (uintmax_t)sp->st_nlink);

            if (print_id || (tmp = get_username(sp->st_uid)) == NULL)
                printf("%-*u ", max_uid, sp->st_uid);
            else
                printf("%-*s ", max_uid, tmp);
            if (print_id || (tmp = get_groupname(sp->st_gid)) == NULL)
                printf("%-*u ", max_gid, sp->st_gid);
            else
                printf("%-*s ", max_gid, tmp);

            if (S_ISCHR(sp->st_mode) || S_ISBLK(sp->st_mode))
                printf("%*u, %*u ", max_major, major(sp->st_rdev), max_minor, minor(sp->st_rdev));
            else if (humanize)
                printf("%*s ", MAX_HUMAN_LEN, get_human(sp->st_size));
            else
                printf("%*ju ", (int)max_size, (uintmax_t)sp->st_size);

            char *time_fmt[2] = { "%b %e %H:%M", "%b %e  %Y" };
            time_t t = get_time(sp);
            struct tm *tp = localtime(&t);
            if (tp == NULL)
                WARN("localtime");
            else if (strftime(buf, sizeof(buf), time_fmt[t < six_month_ago], tp) == 0)
                WARN("strftime");
            else
                printf("%s ", buf);
        }

        print_file_name(p);
    }
    errno = 0;
    line_break_before_dir = true;
}


int
main(int argc, char* argv[])
{
    char opt;
    char *env;
    FTS *ftsp;
    FTSENT *p;
    int fts_options = FTS_PHYSICAL;
    /* default is current directory, cast to char *[] make it writable */
    char **fts_argv = (char *[]){ ".", NULL };
    bool is_recursive = false;      /* -R */

    /* true for super-user */
    show_hidden = !getuid();

    /* set -w and -1 for non terminal */
    if (!isatty(STDOUT_FILENO)) {
        raw_print = true;
        by_column = false;
    }

    if (print_blocks && (env = getenv("BLOCKSIZE")) != NULL)
        if ((block_size = strtol(env, NULL, 10)) <= 0)
            block_size = DEFAULT_BLOCKSIZE;

    while ((opt = getopt(argc, argv, "AacCdFfhiklnqRrSstuwx1")) != -1) {
        switch (opt) {
        case 'a':
            fts_options |= FTS_SEEDOT;
            /* FALLTHROUGH */
        case 'A':
            show_hidden = true;
            break;
        case 't':
            sort_method = TIME_SORT;
            break;
        case 'u':
            time_to_use = USE_ATIME;
            break;
        case 'c':
            time_to_use = USE_CTIME;
            break;
        case 'S':
            sort_method = SIZE_SORT;
            break;
        case 'f':
            sort_method = NO_SORT;
            break;
        case 'r':
            reversed_sort = true;
            break;
        case 'R':
            is_recursive = true;
            break;
        case 'd':
            print_dir = true;
            is_recursive = false;
            break;
        case 'F':
            print_indicator = true;
            break;
        case 'i':
            print_inode = true;
            break;
        case 's':
            print_blocks = true;
            break;
        case 'h':
            humanize = true;
            break;
        case 'k':
            humanize = false;
            block_size = 1024;
            break;
        case 'n':
            print_id = true;
            /* FALLTHROUGH */
        case 'l':
            long_format = true;
            by_column = false;
            break;
        case '1':
            by_column = false;
            long_format = false;
            break;
        case 'C':
            by_column = true;
            long_format = false;
            horizontal = false;
            break;
        case 'x':
            by_column = true;
            long_format = false;
            horizontal = true;
            break;
        case 'q':
            raw_print = false;
            break;
        case 'w':
            raw_print = true;
            break;
        default:
            usage();
        }
    }
    argv += optind;
    argc -= optind;

    /* use operands if given */
    if (argc > 0)
        fts_argv = argv;

    if (long_format)
        six_month_ago = time(NULL) - 6 * 30 * 24 * 60 * 60;
    else if (!print_indicator && !print_dir)
        fts_options |= FTS_COMFOLLOW;

    if (by_column) {
        /* get real terminal width first */
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1)
            terminal_width = w.ws_col;
        else if (errno != ENOTTY)
            WARN("ioctl");
        /* override it if COLUMNS exist and valid */
        if ((env = getenv("COLUMNS")) != NULL)
            if ((w.ws_col = strtol(env, NULL, 10)) > 0)
                terminal_width = w.ws_col;
    }

    if ((ftsp = fts_open(fts_argv, fts_options, ftscmp)) == NULL)
        err(EXIT_FAILURE, "fts_open");

    print_fts_children(ftsp);

    if (!print_dir) {
        while ((p = fts_read(ftsp)) != NULL) {
            switch (p->fts_info) {
            case FTS_D:
                if (p->fts_name[0] != '.' || show_hidden || p->fts_level == FTS_ROOTLEVEL) {
                    if (is_recursive || argc > 1) {
                        if (line_break_before_dir)
                            putchar('\n');
                        else
                            line_break_before_dir = true;
                        printf("%s:\n", p->fts_path);
                    }
                    print_fts_children(ftsp);
                    if (is_recursive || errno)  /* don't skip to show error */
                        break;
                }
                (void)fts_set(ftsp, p, FTS_SKIP);
                break;
            case FTS_DC:
                warnx("%s causes a cycle", p->fts_name);
                rval++;
                break;
            case FTS_DNR:
            case FTS_ERR:
                warnx("%s: %s", p->fts_name, strerror(p->fts_errno));
                rval++;
            default:
                break;
            }
        }
        if (errno)
            err(EXIT_FAILURE, "fts_read");
    }

    (void)fts_close(ftsp);

    return rval;
}

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
#include <fcntl.h>
#include <fts.h>
#include <grp.h>
#include <inttypes.h>
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


static enum {
    /* -f,   default    -t,        -S */
    NO_SORT, NAME_SORT, TIME_SORT, SIZE_SORT
} sort_method = NAME_SORT;

static enum {
    /* -t, -u,    -c */
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
static bool raw_print       = false;    /* -w */
static bool by_column       = true;     /* -C */
static bool show_hidden     = false;    /* -a */
static int  block_size      = DEFAULT_BLOCKSIZE;
static int  terminal_width  = DEFAULT_TERMWIDTH;
static bool line_break_before_dir = false;
static time_t six_month_ago;

static void
usage(void)
{
    (void)fprintf(stderr, "Usage: ls [-AacCdFfhiklnqRrSstuwx1] [file ...]\n");
    exit(EXIT_FAILURE);
}

static int
uint_length(unsigned int n)
{
    if (n == 0)
        return 1;
    return floor(log10(n)) + 1;
}

static unsigned
get_blocks(unsigned blocks)
{
    return (blocks * S_BLKSIZE + block_size - 1) / block_size;
}

static const char *
get_username(uid_t uid)
{
    struct passwd *p = getpwuid(uid);
    if (p)
        return p->pw_name;
    return NULL;
}

static const char *
get_groupname(gid_t gid)
{
    struct group *g = getgrgid(gid);
    if (g)
        return g->gr_name;
    return NULL;
}

static time_t
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

static const char *
get_human(unsigned n)
{
    static char human_buf[MAX_HUMAN_LEN + 1];
    if (humanize_number(human_buf, sizeof(human_buf), n, "",
        HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE) == -1) {
        warn("humanize_number");
        return "";
    }
    return human_buf;
}

/* 
 * replace non-printable characters with '?'
 */
static void
escape_string(char *str, int len)
{
    for (int i = 0; i < len; i++)
        if (!isprint(str[i]))
            str[i] = '?';
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

    int r = reversed_sort ? -1 : 1;

    if ((*a)->fts_info == FTS_NS) {
        if ((*b)->fts_info == FTS_NS)
            if (sort_method == NO_SORT)
                return 0;
            else
                return r * strcmp((*a)->fts_name, (*b)->fts_name);
        else
            return 1;
    } else {
        if ((*b)->fts_info == FTS_NS)
            return -1;
    }

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
        break;
    case NO_SORT:
        return 0;
    case NAME_SORT:
    default:
        break;
    }
    return r * strcmp((*a)->fts_name, (*b)->fts_name);
}


static void
print_file_name(FTSENT *p)
{
    if (!raw_print)
        escape_string(p->fts_name, p->fts_namelen);

    printf("%s", p->fts_name);
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
            perror("readlink");
        else {
            buf[len] = 0;
            if (!raw_print)
                escape_string(buf, len);

            printf(" -> %s", buf);
        }
    }
    if (by_column) {
        if (p->fts_pointer) {
            printf("%*c", (int)(p->fts_number - p->fts_namelen + 1), ' ');
            return;
        }
    }
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
    int max_size = 0;
    int max_major = 0;
    int max_minor = 0;
    int item_count = 0;
    int max_columns = 1;
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
        if (head->fts_level != FTS_ROOTLEVEL)
            printf("total %u\n", get_blocks(blocks_sum));
        if (print_blocks)
            max_blocks = uint_length(get_blocks(max_blocks));
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

    if (by_column) {
        int i = 0;
        int others_width = 1 + max_inode + !!max_inode + 
            max_blocks + !!max_blocks;
        int width[item_count];
        int n_columns = 2;
        int n_rows;

        while (n_columns <= item_count) {
            n_rows = (item_count + n_columns - 1) / n_columns;

            for (i = 0; i < n_columns; i++)
                width[i] = 0;
            i = 0;
            for (FTSENT *p = head; p; p = p->fts_link) {
                if (p->fts_number != 0) {
                    if (width[i / n_rows] < p->fts_namelen)
                        width[i / n_rows] = p->fts_namelen;
                    i++;
                }
            }
            int sum = 0;
            for (i = 0; i < n_columns; i++)
                sum += width[i];
            if (sum + others_width * n_columns > terminal_width)
                break;
            max_columns = n_columns;

            if (n_columns < n_rows)
                n_columns++;
            else if (n_rows != 1)
                n_columns = (item_count + n_rows - 2) / (n_rows - 1);
            else
                break;
        }
        if (max_columns > 1) {
            n_rows = (item_count + max_columns - 1) / max_columns;
            for (i = 0; i < max_columns; i++)
                width[i] = 0;
            i = 0;
            for (FTSENT *p = head; p; p = p->fts_link) {
                if (p->fts_number != 0) {
                    if (width[i / n_rows] < p->fts_namelen)
                        width[i / n_rows] = p->fts_namelen;
                    i++;
                }
            }
            FTSENT *q = head;
            for (i = 0; i < n_rows; q = q->fts_link)
                i += q->fts_number != 0;
            /* make fts_pointer points to the item on the right */
            i = 0;
            for (FTSENT *p = head; p; p = p->fts_link) {
                if (p->fts_number != 0) {
                    p->fts_number = width[i / n_rows];
                    i++;
                    p->fts_pointer = q;
                    if (q)
                        q = q->fts_link;
                }
            }
            /* rearrange the linked list */
            i = 1;
            for (FTSENT *p = head; i <= n_rows;) {
                q = p;
                p = p->fts_link;
                if (q->fts_number != 0) {
                    while (q->fts_pointer)
                        q = q->fts_link = q->fts_pointer;
                    if (i++ < n_rows)
                        q->fts_link = p;
                    else
                        q->fts_link = NULL;
                }
            }
        }
    }

    for (FTSENT *p = head; p; p = p->fts_link) {
        if (p->fts_number == 0)
            continue;
        struct stat *sp = p->fts_statp;
        if (print_inode)
            printf("%*ju ", (int)max_inode, (uintmax_t)sp->st_ino);
        if (print_blocks) {
            if (humanize)
                printf("%*s ", MAX_HUMAN_LEN,
                    get_human(sp->st_blocks * S_BLKSIZE));
            else
                printf("%*u ", (int)max_blocks, get_blocks(sp->st_blocks));
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
                printf("%*u, %*u ", max_major, major(sp->st_rdev),
                    max_minor, minor(sp->st_rdev));
            else if (humanize)
                printf("%*s ", MAX_HUMAN_LEN, get_human(sp->st_size));
            else
                printf("%*ju ", max_size, (uintmax_t)sp->st_size);

            char *time_fmt[2] = { "%b %e %H:%M", "%b %e  %Y" };
            time_t t = get_time(sp);
            tmp = time_fmt[t < six_month_ago];
            struct tm *tp = localtime(&t);
            if (tp == NULL)
                perror("localtime");
            else {
                if (strftime(buf, sizeof(buf), tmp, tp) == 0)
                    perror("strftime");
                else
                    printf("%s ", buf);
            }
        }

        print_file_name(p);
    }

    line_break_before_dir = true;
}


int
main(int argc, char* argv[])
{
    char c;
    char *env;
    FTS *ftsp;
    FTSENT *p;
    int fts_options = FTS_PHYSICAL;
    /* default is current directory, cast to char *[] make it writable */
    char **fts_argv = (char *[]){ ".", NULL };
    bool is_recursive = false;      /* -R */

    /* always show hidden for super user */
    show_hidden = !getuid();

    /* set -w and -1 for non terminal */
    if (!isatty(STDOUT_FILENO)) {
        raw_print = true;
        by_column = false;
    }

    if (print_blocks && (env = getenv("BLOCKSIZE")) != NULL)
        if ((block_size = strtol(env, NULL, 10)) <= 0)
            block_size = DEFAULT_BLOCKSIZE;

    while ((c = getopt(argc, argv, "AacCdFfhiklnqRrSstuwx1")) != -1) {
        switch (c) {
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
        case 'l':
            long_format = true;
            break;
        case '1':
            by_column = false;
            long_format = false;
            break;
        case 'C':
            by_column = true;
            long_format = false;
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

    if (long_format) {
        six_month_ago = time(NULL) - 6 * 30 * 24 * 60 * 60;
    }
    else if (!print_indicator && !print_dir)
        fts_options |= FTS_COMFOLLOW;

    if (by_column) {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1)
            warn("ioctl");
        else
            terminal_width = w.ws_col;
    }

    if ((ftsp = fts_open(fts_argv, fts_options, ftscmp)) == NULL)
        err(EXIT_FAILURE, "fts_open");

    print_fts_children(ftsp);

    if (print_dir) {
        (void)fts_close(ftsp);
        return EXIT_SUCCESS;
    }

    while ((p = fts_read(ftsp)) != NULL) {
        switch (p->fts_info) {
        case FTS_D:
            if (p->fts_name[0] != '.' || show_hidden ||
                p->fts_level == FTS_ROOTLEVEL) {
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
            break;
        case FTS_DNR:
        case FTS_ERR:
            warnx("%s: %s", p->fts_name, strerror(p->fts_errno));
        default:
            break;
        }
    }

    if (errno)
        err(EXIT_FAILURE, "fts_read");

    (void)fts_close(ftsp);

    return EXIT_SUCCESS;
}

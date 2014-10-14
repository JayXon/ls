/*
 * ls
 *
 * Advanced Programming in the UNIX Environment - Fall 2014
 * Midterm Assignment
 * http://www.cs.stevens.edu/~jschauma/631/f14-midterm.html
 *
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



static enum {
    NO_SORT, NAME_SORT, TIME_SORT, SIZE_SORT
} sort_method = NAME_SORT;

static enum {
    MTIME, ATIME, CTIME
} time_to_use = MTIME;

static bool reversed_sort   = false;
static bool print_indicator = false;
static bool print_dir       = false;
static bool raw_print       = false;
static bool by_column       = true;
static bool show_hidden;


static void
usage(void)
{
    (void)fprintf(stderr, "Usage: ls [-AacdFfhiklnqRrSstuw1] [file ...]\n");
    exit(EXIT_FAILURE);
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
        time_t atime, btime;
        switch (time_to_use) {
        case MTIME:
            atime = (*a)->fts_statp->st_mtime;
            btime = (*b)->fts_statp->st_mtime;
            break;
        case ATIME:
            atime = (*a)->fts_statp->st_atime;
            btime = (*b)->fts_statp->st_atime;
            break;
        case CTIME:
            atime = (*a)->fts_statp->st_ctime;
            btime = (*b)->fts_statp->st_ctime;
            break;
        }
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
    case NAME_SORT:
        break;
    case NO_SORT:
        return 0;
    }
    return r * strcmp((*a)->fts_name, (*b)->fts_name);
}


static void
print_file_name(FTSENT *p)
{
    if (!raw_print) {
        /* escape non-printable characters */
        for (short i = 0; i < p->fts_namelen; i++)
            if (!isprint(p->fts_name[i]))
                p->fts_name[i] = '?';
    }
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
        }
    }
    putchar('\n');
}



static void
print_fts_children(FTS *ftsp)
{
    for (FTSENT *p = fts_children(ftsp, 0); p; p = p->fts_link) {
        switch (p->fts_info) {
        case FTS_NS:
        case FTS_DNR:
        case FTS_ERR:
            warnx("%s: %s", p->fts_name, strerror(p->fts_errno));
            break;
        case FTS_D:
            if (p->fts_level == FTS_ROOTLEVEL && !print_dir)
                return;
            /* FALLTHROUGH */
        default:
            if (p->fts_name[0] != '.' || show_hidden) {
                print_file_name(p);
            }
            break;
        }
    }
}


int
main(int argc, char* argv[])
{
    char c;
    FTS *ftsp;
    FTSENT *p;
    int fts_options = FTS_COMFOLLOW | FTS_PHYSICAL;
    /* default is current directory, cast to char *[] make it writable */
    char **fts_argv = (char *[]){ ".", NULL };
    bool is_recursive = false;
    bool print_line_break = false;

    /* always show hidden for super user */
    show_hidden = !getuid();

    if (!isatty(fileno(stdout))) {
        raw_print = true;
        by_column = false;
    }

    while ((c = getopt(argc, argv, "AacdFfhiklnqRrSstuw1")) != -1) {
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
            time_to_use = ATIME;
            break;
        case 'c':
            time_to_use = CTIME;
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
            fts_options &= ~FTS_COMFOLLOW;
            is_recursive = false;
            break;
        case 'F':
            print_indicator = true;
            fts_options &= ~FTS_COMFOLLOW;
            break;
        case 'h':
        case 'i':
        case 'k':
        case 'l':
        case 'n':
        case 's':
        case 'q':
            raw_print = false;
            break;
        case 'w':
            raw_print = true;
            break;
        case '1':
            by_column = false;
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
                    if (print_line_break)
                        putchar('\n');
                    else
                        print_line_break = true;
                    printf("%s:\n", p->fts_path);
                }
                print_fts_children(ftsp);
                if (is_recursive)
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

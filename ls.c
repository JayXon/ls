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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



enum {
    NO_SORT, NAME_SORT, TIME_SORT, SIZE_SORT
} sort_method;

enum {
    MTIME, ATIME, CTIME
} time_to_use;

static bool reversed_sort;


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


int
main(int argc, char* argv[])
{
    char c;
    FTS *ftsp;
    FTSENT *p;
    int fts_options = FTS_COMFOLLOW | FTS_LOGICAL;
    /* default is current directory, cast to char *[] make it writable */
    char **fts_argv = (char *[]){ ".", NULL };
    bool show_hidden = !getuid();

    sort_method = NAME_SORT;
    reversed_sort = false;
    time_to_use = MTIME;

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
        case 'd':
        case 'F':
        case 'h':
        case 'i':
        case 'k':
        case 'l':
        case 'n':
        case 'q':
        case 'R':
        case 's':
        case 'w':
        case '1':
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

    while ((p = fts_read(ftsp)) != NULL) {
        switch (p->fts_info) {
        case FTS_D:
            if (p->fts_level == FTS_ROOTLEVEL) {
                if (argc > 1)
                    printf("%s:\n", p->fts_path);
                break;
            } else
                (void)fts_set(ftsp, p, FTS_SKIP);
            /* FALLTHROUGH */
        case FTS_DOT:
        case FTS_F:
            if (p->fts_name[0] != '.' || show_hidden) {
                printf("%s\n", p->fts_name);
            }
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

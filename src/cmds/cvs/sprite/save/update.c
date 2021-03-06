#ifndef lint
static char rcsid[] = "$Id: update.c,v 1.27.1.2 91/02/06 18:30:07 berliner Exp $";
#endif !lint

/*
 *    Copyright (c) 1989, Brian Berliner
 *
 *    You may distribute under the terms of the GNU General Public License
 *    as specified in the README file that comes with the CVS 1.0 kit.
 *
 *	"update" updates the version in the present directory with respect to
 *	the RCS repository.  The present version must have been created by
 *	"checkout".  The user can keep up-to-date by calling "update" whenever
 *	he feels like it.
 *
 *	The present version can be committed by "commit", but this keeps the
 *	version in tact.
 *
 *	"update" accepts the following options:
 *		-p	Prunes empty directories
 *		-l	Local; does not do a recursive update
 *		-q	Quiet; does not print a message when
 *				recursively updating
 *		-Q	Really quiet
 *		-d	Directory; causes update to create new directories
 *				as they are added to the RCS repository
 *		-f	Forces a match for the -r revision
 *		-r tag	Revision; only extract from revision "tag"
 *		-D date	Updates to the revision specified by "date"
 *
 *	Arguments following the options are taken to be file names
 *	to be updated, rather than updating the entire directory.
 *
 *	Modified or non-existent RCS files are checked out and reported
 *	as U <user_file>
 *
 *	Modified user files are reported as M <user_file>.  If both the
 *	RCS file and the user file have been modified, the user file
 *	is replaced by the result of rcsmerge, and a backup file is
 *	written for the user in .#file.version.  If this throws up
 *	irreconcilable differences, the file is reported as C <user_file>,
 *	and as M <user_file> otherwise.
 *
 *	Files added but not yet committed are reported as A <user_file>.
 *	Files removed but not yet decommitted are reported as R <user_file>.
 *
 *	If the current directory contains subdirectories that hold
 *	concurrent versions, these are updated too.  If the -d option
 *	was specified, new directories added to the repository are
 *	automatically created and updated as well.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "cvs.h"

char update_dir[MAXPATHLEN];
int update_recursive = 1;
int update_build_dirs = 0;
int update_prune_dirs = 0;

static int really_recursive = 1;

update(argc, argv)
    int argc;
    char *argv[];
{
    FILE *fp;
    int c, err = 0;

    if (argc == -1)
	update_usage();
    optind = 1;
    while ((c = getopt(argc, argv, "pflQqdr:D:")) != -1) {
	switch (c) {
	case 'l':
	    really_recursive = 0;
	    update_recursive = 0;
	    break;
	case 'Q':
	    really_quiet = 1;
	    /* FALL THROUGH */
	case 'q':
	    quiet = 1;
	    break;
	case 'd':
	    update_build_dirs = 1;
	    break;
	case 'f':
	    force_tag_match = 1;
	    break;
	case 'r':
	    (void) strcpy(Tag, optarg);
	    break;
	case 'D':
	    Make_Date(optarg, Date);
	    break;
	case 'p':
	    update_prune_dirs = 1;
	    break;
	case '?':
	default:
	    update_usage();
	    break;
	}
    }
    argc -= optind;
    argv += optind;
    if (!isdir(CVSADM)) {
	if (!quiet)
	    warn(0, "warning: no %s directory found", CVSADM);
	if (argc <= 0) {
	    err += update_descend(update_recursive);
	} else {
	    int i;

	    for (i = 0; i < argc; i++) {
		if (isdir(argv[i])) {
		    (void) strcat(Dlist, " ");
		    (void) strcat(Dlist, argv[i]);
		} else {
		    warn(0, "nothing known about %s", argv[i]);
		    err++;
		}
	    }
	    err += update_process_lists();
	}
	return (err);
    }
    Name_Repository();
    Reader_Lock();
    if (argc <= 0) {
	/*
	 * When updating the entire directory, and recursively building
	 * directories, must make sure that the "static" file in the
	 * administration is removed before calling Find_Names().
	 */
	if (update_build_dirs)
	    (void) unlink(CVSADM_ENTSTAT);
	if (force_tag_match && (Tag[0] != '\0' || Date[0] != '\0'))
	    Find_Names(&fileargc, fileargv, ALLPLUSATTIC);
	else
	    Find_Names(&fileargc, fileargv, ALL);
	fp = open_file(CVSADM_MOD, "w+"); /* create a NULL Mod file */
	(void) fclose(fp);
	argc = fileargc;
	argv = fileargv;
    } else {
	/*
	 * Not recursive if files were specified on the command line
	 */
	update_recursive = 0;
    }
    if (Collect_Sets(argc, argv) != 0)
	error(0, "failed; correct the above errors first");
    free_names(&fileargc, fileargv);
    err += update_process_lists();
    /*
     * XXX - Might be nice to sort the Mod file here, removing unique
     * entries as we go, but it's currently not necessary, as "diff"
     * is the only one that uses it, and he does the sort "as needed".
     */
    Lock_Cleanup(0);
    /*
     * Make directories, and descend them if requested to.
     */
    err += update_make_dirs(update_build_dirs && update_recursive);
    err += update_descend(update_recursive);
    Lock_Cleanup(0);
    return (err);
}

/*
 * Process the lists created by Collect_Sets().
 */
static
update_process_lists()
{
    char backup[MAXPATHLEN], dlist[MAXLISTLEN];
    FILE *fp;
    char *cp;
    int update_Files = 0, err = 0;

    /*
     * Wlist is the "remove entry" list.
     */
    for (cp = strtok(Wlist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	update_Files = 1;
	(void) strcpy(User, cp);
	Scratch_Entry(User);
	(void) unlink(User);
    }
    /*
     * Olist is the "needs checking out" list.
     */
    for (cp = strtok(Olist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	update_Files = 1;
	(void) strcpy(User, cp);
	Locate_RCS();
	(void) sprintf(backup, "%s/%s%s", CVSADM, CVSPREFIX, User);
	if (isreadable(User))
	    rename_file(User, backup);
	else
	    (void) unlink(backup);
	if (Tag[0] != '\0' || Date[0] != '\0') {
	    Version_Number(Rcs, Tag, Date, VN_Rcs);
	    (void) sprintf(prog, "%s/%s -q -r%s %s %s", Rcsbin, RCS_CO,
			   VN_Rcs, Rcs, User);
	} else {
	    (void) sprintf(prog, "%s/%s -q %s %s", Rcsbin, RCS_CO, Rcs, User);
	}
	if (system(prog) == 0) {
	    if (cvswrite == TRUE)
		xchmod(User, 1);
	    Version_TS(Rcs, Tag, User);
	    Register(User, VN_Rcs, TS_User);
	    if (!really_quiet) {
		if (update_dir[0])
		    printf("U %s/%s\n", update_dir, User);
		else
		    printf("U %s\n", User);
	    }
	} else {
	    if (isreadable(backup))
		rename_file(backup, User);
	    warn(0, "could not check out %s", User);
	    err++;
	}
	(void) unlink(backup);
    }
    /*
     * Mlist is the "modified, needs checking in" list.
     */
    for (cp = strtok(Mlist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	update_Files = 1;
	(void) strcpy(User, cp);
	if (!really_quiet) {
	    if (update_dir[0])
		printf("M %s/%s\n", update_dir, User);
	    else
		printf("M %s\n", User);
	}
	fp = open_file(CVSADM_MOD, "a");
	if (fprintf(fp, "%s\n", User) == EOF)
	    error(1, "cannot write %s", CVSADM_MOD);
	(void) fclose(fp);
    }
    /*
     * Alist is the "to be added" list.
     */
    for (cp = strtok(Alist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	update_Files = 1;
	(void) strcpy(User, cp);
	if (!really_quiet) {
	    if (update_dir[0])
		printf("A %s/%s\n", update_dir, User);
	    else
		printf("A %s\n", User);
	}
    }
    /*
     * Rlist is the "to be removed" list.
     */
    for (cp = strtok(Rlist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	update_Files = 1;
	(void) strcpy(User, cp);
	if (!really_quiet) {
	    if (update_dir[0])
		printf("R %s/%s\n", update_dir, User);
	    else
		printf("R %s\n", User);
	}
    }
    /*
     * Glist is the "modified, needs merging" list.
     *
     * The users currently modified file is moved to a backup file
     * name ".#filename.version", so that it will stay around for about
     * three days before being automatically removed by some cron
     * daemon.  The "version" is the version of the file that the
     * user was most up-to-date with before the merge.
     */
    for (cp = strtok(Glist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	update_Files = 1;
	(void) strcpy(User, cp);
	Locate_RCS();
	Version_TS(Rcs, Tag, User);
	(void) sprintf(backup, "%s%s.%s", BAKPREFIX, User, VN_User);
	(void) unlink(backup);
	copy_file(User, backup);
	xchmod(User, 1);
	(void) sprintf(prog, "%s/%s -r%s %s", Rcsbin, RCS_MERGE,
		       VN_User, Rcs);
	if (system(prog) != 0) {
	    warn(0, "could not merge revision %s of %s",
		 VN_User, User);
	    warn(0, "backup file for %s is in %s", User, backup);
	    err++;
	    continue;
	}
	Register(User, VN_Rcs, TS_Rcs);
	(void) sprintf(prog, "%s -s '%s' %s", GREP, RCS_MERGE_PAT, User);
	if (system(prog) == 0) {
	    warn(0, "conflicts found in %s", User);
	    if (!really_quiet) {
		if (update_dir[0])
		    printf("C %s/%s\n", update_dir, User);
		else
		    printf("C %s\n", User);
	    }
	} else {
	    if (!really_quiet) {
		if (update_dir[0])
		    printf("M %s/%s\n", update_dir, User);
		else
		    printf("M %s\n", User);
	    }
	}
	fp = open_file(CVSADM_MOD, "a");
	if (fprintf(fp, "%s\n", User) == EOF)
	    error(1, "cannot write %s", CVSADM_MOD);
	(void) fclose(fp);
    }
    /*
     * Slist is a list of symbolic links that need to be created.
     */
    for (cp = strtok(Slist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	char 	tmp[MAXPATHLEN];
	char 	src[MAXPATHLEN];
	int	size;
	(void) strcpy(User, cp);
	(void) sprintf(tmp, "%s/%s", Repository, User);
	size = readlink(tmp, src, MAXPATHLEN);
	if (size == -1) {
	    error(1, "readlink of %s failed", tmp);
	}
	src[size] = '\0';
	if (symlink(src, User) != 0) {
	    error(1, "symlink of %s to %s failed", User, src);
	}
	if (!really_quiet) {
	    printf("S %s\n", User);
	}
    }
    if (Dlist[0]) {
	int save_recursive = update_recursive;

	update_recursive = really_recursive;
	Lock_Cleanup(0);		/* cleanup locks before descending */
	(void) strcpy(dlist, Dlist);	/* to get it on the stack... */
	for (cp = strtok(dlist, " \t"); cp; cp = strtok((char *)NULL, " \t")) {
	    err += update_descend_dir(cp);
	}
	update_recursive = save_recursive;
    }
    if (update_Files != 0)
	Entries2Files();
    return (err);
}

/*
 * When automatically creating directories from the repository in the
 * local work directory, we scan for directories that don't exist locally
 * and create them with a NULL administration directory for now, which
 * is filled by update later.
 */
static
update_make_dirs(doit)
    int doit;
{
    char fname[MAXPATHLEN], tmp[MAXPATHLEN];
    DIR *dirp;
    struct dirent *dp;
    int err = 0;

    if (doit) {
	if ((dirp = opendir(Repository)) == NULL) {
	    warn(0, "cannot open directory %s", Repository);
	    err++;
	} else while ((dp = readdir(dirp)) != NULL) {
	    if (strcmp(dp->d_name, ".") == 0 ||
		strcmp(dp->d_name, "..") == 0 ||
		strcmp(dp->d_name, CVSATTIC) == 0 ||
		strcmp(dp->d_name, CVSLCK) == 0)
		continue;
	    (void) sprintf(fname, "%s/%s", Repository, dp->d_name);
	    (void) sprintf(tmp, "%s/%s", dp->d_name, CVSADM);
	    if (!isdir(fname))
		continue;
	    if (islink(dp->d_name) || isdir(tmp))
		continue;
	    if (!isdir(dp->d_name) && isfile(dp->d_name)) {
		warn(0, "file %s should be a directory; please move it", dp->d_name);
		err++;
	    } else {
		make_directory(dp->d_name);
		if (chdir(dp->d_name) < 0) {
		    warn(0, "cannot chdir to %s", dp->d_name);
		    err++;
		} else {
		    (void) strcpy(tmp, Repository);
		    (void) strcpy(Repository, fname);
		    Create_Admin(Repository, DFLT_RECORD);
		    (void) chdir("..");
		    (void) strcpy(Repository, tmp);
		}
	    }
	}
	if (dirp)
	    (void) closedir(dirp);
    }
    return (err);
}

/*
 * If doalldirs is set, does a recursive update by calling update_descend_dir()
 * for each file in the current directory.
 */
static
update_descend(doalldirs)
    int doalldirs;
{
    DIR *dirp;
    struct dirent *dp;
    int err = 0;

    if (doalldirs) {
	if ((dirp = opendir(".")) == NULL) {
	    err++;
	} else {
	    while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
		    continue;
		err += update_descend_dir(dp->d_name);
	    }
	    (void) closedir(dirp);
	}
    }
    return (err);
}

/*
 * This is the recursive function that walks the argument directory looking
 * for sub-directories that have CVS administration files in them
 * and updates them recursively.
 *
 * Note that we do not follow symbolic links here, which is a feature!
 */
static
update_descend_dir(dir)
    char *dir;
{
    char cwd[MAXPATHLEN], fname[MAXPATHLEN];
    char *cp;
    int err;

    (void) sprintf(fname, "%s/%s", dir, CVSADM);
    if (!isdir(dir) || islink(dir) || !isdir(fname))
	return (0);
    if (getwd(cwd) == NULL) {
	warn(0, "cannot get working directory: %s", cwd);
	return (1);
    }
    if (update_dir[0] == '\0')
	(void) strcpy(update_dir, dir);
    else {
	(void) strcat(update_dir, "/");
	(void) strcat(update_dir, dir);
    }
    if (!quiet)
	printf("%s %s: Updating %s\n", progname, command, update_dir);
    if (chdir(dir) < 0) {
	warn(1, "cannot chdir to %s", update_dir);
	err = 1;
	goto out;
    }
    err = update(0, (char **)0);
    if ((cp = rindex(update_dir, '/')) != NULL)
	*cp = '\0';
    else
	update_dir[0] = '\0';
out:
    if (chdir(cwd) < 0)
	error(1, "cannot chdir to %s", cwd);
    if (update_prune_dirs && isemptydir(dir)) {
	(void) sprintf(prog, "%s -fr %s", RM, dir);
	(void) system(prog);
    }
    return (err);
}

/*
 * Returns 1 if the argument directory is completely empty, other than
 * the existence of the CVS.adm directory entry.  Zero otherwise.
 */
isemptydir(dir)
    char *dir;
{
    DIR *dirp;
    struct dirent *dp;

    if ((dirp = opendir(dir)) == NULL) {
        warn(0, "cannot open directory %s for empty check", dir);
	return (0);
    }
    while ((dp = readdir(dirp)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0 &&
            strcmp(dp->d_name, CVSADM) != 0) {
            (void) closedir(dirp);
            return (0);
        }
    }
    (void) closedir(dirp);
    return (1);
}

static
update_usage()
{
    (void) fprintf(stderr,
		   "Usage: %s %s [-Qqlfp] [-d] [-r tag|-D date] [files...]\n",
		   progname, command);
    exit(1);
}

/*
 * Taken from perf which in turn take it from GIT
 */

#include "kvm/util.h"

#include <linux/magic.h>	/* For HUGETLBFS_MAGIC */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>

static void report(const char *prefix, const char *err, va_list params)
{
	char msg[1024];
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, " %s%s\n", prefix, msg);
}

static NORETURN void die_builtin(const char *err, va_list params)
{
	report(" Fatal: ", err, params);
	exit(128);
}

static void error_builtin(const char *err, va_list params)
{
	report(" Error: ", err, params);
}

static void warn_builtin(const char *warn, va_list params)
{
	report(" Warning: ", warn, params);
}

static void info_builtin(const char *info, va_list params)
{
	report(" Info: ", info, params);
}

void die(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	die_builtin(err, params);
	va_end(params);
}

int pr_error(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	error_builtin(err, params);
	va_end(params);
	return -1;
}

void pr_warning(const char *warn, ...)
{
	va_list params;

	va_start(params, warn);
	warn_builtin(warn, params);
	va_end(params);
}

void pr_info(const char *info, ...)
{
	va_list params;

	va_start(params, info);
	info_builtin(info, params);
	va_end(params);
}

void die_perror(const char *s)
{
	perror(s);
	exit(1);
}

void *mmap_hugetlbfs(const char *htlbfs_path, u64 size)
{
	char mpath[PATH_MAX];
	int fd;
	struct statfs sfs;
	void *addr;

	if (statfs(htlbfs_path, &sfs) < 0)
		die("Can't stat %s\n", htlbfs_path);

	if (sfs.f_type != HUGETLBFS_MAGIC) {
		die("%s is not hugetlbfs!\n", htlbfs_path);
	}

	if (sfs.f_bsize == 0 || (unsigned long)sfs.f_bsize > size) {
		die("Can't use hugetlbfs pagesize %ld for mem size %lld\n",
		    sfs.f_bsize, size);
	}

	snprintf(mpath, PATH_MAX, "%s/kvmtoolXXXXXX", htlbfs_path);
	fd = mkstemp(mpath);
	if (fd < 0)
		die("Can't open %s for hugetlbfs map\n", mpath);
	unlink(mpath);
	if (ftruncate(fd, size) < 0)
		die("Can't ftruncate for mem mapping size %lld\n",
		    size);
	addr = mmap(NULL, size, PROT_RW, MAP_PRIVATE, fd, 0);
	close(fd);

	return addr;
}

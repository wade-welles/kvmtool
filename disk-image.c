#include "kvm/disk-image.h"

#include "kvm/read-write.h"
#include "kvm/qcow.h"
#include "kvm/util.h"

#include <linux/fs.h>	/* for BLKGETSIZE64 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

struct disk_image *disk_image__new(int fd, uint64_t size, struct disk_image_operations *ops)
{
	struct disk_image *self;

	self		= malloc(sizeof *self);
	if (!self)
		return NULL;

	self->fd	= fd;
	self->size	= size;
	self->ops	= ops;
	return self;
}

struct disk_image *disk_image__new_readonly(int fd, uint64_t size, struct disk_image_operations *ops)
{
	struct disk_image *self;

	self = disk_image__new(fd, size, ops);
	if (!self)
		return NULL;

	self->priv = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_NORESERVE, fd, 0);
	if (self->priv == MAP_FAILED)
		die("mmap() failed");
	return self;
}

static ssize_t raw_image__read_sector_iov(struct disk_image *self, uint64_t sector, const struct iovec *iov, int iovcount)
{
	uint64_t offset = sector << SECTOR_SHIFT;

	return preadv_in_full(self->fd, iov, iovcount, offset);
}

static ssize_t raw_image__write_sector_iov(struct disk_image *self, uint64_t sector, const struct iovec *iov, int iovcount)
{
	uint64_t offset = sector << SECTOR_SHIFT;

	return pwritev_in_full(self->fd, iov, iovcount, offset);
}

static int raw_image__read_sector_ro_mmap(struct disk_image *self, uint64_t sector, void *dst, uint32_t dst_len)
{
	uint64_t offset = sector << SECTOR_SHIFT;

	if (offset + dst_len > self->size)
		return -1;

	memcpy(dst, self->priv + offset, dst_len);

	return 0;
}

static int raw_image__write_sector_ro_mmap(struct disk_image *self, uint64_t sector, void *src, uint32_t src_len)
{
	uint64_t offset = sector << SECTOR_SHIFT;

	if (offset + src_len > self->size)
		return -1;

	memcpy(self->priv + offset, src, src_len);

	return 0;
}

static void raw_image__close_ro_mmap(struct disk_image *self)
{
	if (self->priv != MAP_FAILED)
		munmap(self->priv, self->size);
}

static struct disk_image_operations raw_image_ops = {
	.read_sector_iov	= raw_image__read_sector_iov,
	.write_sector_iov	= raw_image__write_sector_iov
};

static struct disk_image_operations raw_image_ro_mmap_ops = {
	.read_sector		= raw_image__read_sector_ro_mmap,
	.write_sector		= raw_image__write_sector_ro_mmap,
	.close			= raw_image__close_ro_mmap,
};

static struct disk_image *raw_image__probe(int fd, struct stat *st, bool readonly)
{
	if (readonly)
		return disk_image__new_readonly(fd, st->st_size, &raw_image_ro_mmap_ops);
	else
		return disk_image__new(fd, st->st_size, &raw_image_ops);
}

static struct disk_image *blkdev__probe(const char *filename, struct stat *st)
{
	uint64_t size;
	int fd;

	if (!S_ISBLK(st->st_mode))
		return NULL;

	fd		= open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
		close(fd);
		return NULL;
	}

	return disk_image__new_readonly(fd, size, &raw_image_ro_mmap_ops);
}

struct disk_image *disk_image__open(const char *filename, bool readonly)
{
	struct disk_image *self;
	struct stat st;
	int fd;

	if (stat(filename, &st) < 0)
		return NULL;

	if (S_ISBLK(st.st_mode))
		return blkdev__probe(filename, &st);

	fd		= open(filename, readonly ? O_RDONLY : O_RDWR);
	if (fd < 0)
		return NULL;

	self = qcow_probe(fd);
	if (self)
		return self;

	self = raw_image__probe(fd, &st, readonly);
	if (self)
		return self;

	if (close(fd) < 0)
		warning("close() failed");

	return NULL;
}

void disk_image__close(struct disk_image *self)
{
	/* If there was no disk image then there's nothing to do: */
	if (!self)
		return;

	if (self->ops->close)
		self->ops->close(self);

	if (close(self->fd) < 0)
		warning("close() failed");

	free(self);
}

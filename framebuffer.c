#include "kvm/framebuffer.h"

#include <linux/kernel.h>
#include <linux/list.h>
#include <stdlib.h>

static LIST_HEAD(framebuffers);

struct framebuffer *fb__register(struct framebuffer *fb)
{
	INIT_LIST_HEAD(&fb->node);
	list_add(&fb->node, &framebuffers);

	return fb;
}

int fb__attach(struct framebuffer *fb, struct fb_target_operations *ops)
{
	if (fb->nr_targets >= FB_MAX_TARGETS)
		return -1;

	fb->targets[fb->nr_targets++] = ops;

	return 0;
}

static int start_targets(struct framebuffer *fb)
{
	unsigned long i;

	for (i = 0; i < fb->nr_targets; i++) {
		struct fb_target_operations *ops = fb->targets[i];
		int err = 0;

		if (ops->start)
			err = ops->start(fb);

		if (err)
			return err;
	}

	return 0;
}

int fb__start(void)
{
	struct framebuffer *fb;

	list_for_each_entry(fb, &framebuffers, node) {
		int err;

		err = start_targets(fb);
		if (err)
			return err;
	}

	return 0;
}

void fb__stop(void)
{
	struct framebuffer *fb;

	list_for_each_entry(fb, &framebuffers, node) {
		free(fb->mem);
	}
}

static void write_to_targets(struct framebuffer *fb, u64 addr, u8 *data, u32 len)
{
	unsigned long i;

	for (i = 0; i < fb->nr_targets; i++) {
		struct fb_target_operations *ops = fb->targets[i];

		ops->write(fb, addr, data, len);
	}
}

void fb__write(u64 addr, u8 *data, u32 len)
{
	struct framebuffer *fb;

	list_for_each_entry(fb, &framebuffers, node) {
		write_to_targets(fb, addr, data, len);
	}
}

#include <stdlib.h>
#include <assert.h>

#include "fio.h"
#include "list.h"
#include "crc/crc16.h"

#define HASH_BUCKETS	512
#define HASH_MASK	(HASH_BUCKETS - 1)

unsigned int file_hash_size = HASH_BUCKETS * sizeof(struct list_head);

static struct list_head *file_hash;
static struct fio_mutex *hash_lock;

static void dump_hash(void)
{
	struct list_head *n;
	unsigned int i;

	for (i = 0; i < HASH_BUCKETS; i++) {
		list_for_each(n, &file_hash[i]) {
			struct fio_file *f;

			f = list_entry(n, struct fio_file, hash_list);
			printf("%d: %s\n", i, f->file_name);
		}
	}
}

static unsigned short hash(const char *name)
{
	return crc16((const unsigned char *) name, strlen(name)) & HASH_MASK;
}

void remove_file_hash(struct fio_file *f)
{
	fio_mutex_down(hash_lock);

	if (f->flags & FIO_FILE_HASHED) {
		assert(!list_empty(&f->hash_list));
		list_del_init(&f->hash_list);
		f->flags &= ~FIO_FILE_HASHED;
	}

	fio_mutex_up(hash_lock);
}

static struct fio_file *__lookup_file_hash(const char *name)
{
	struct list_head *bucket = &file_hash[hash(name)];
	struct list_head *n;

	list_for_each(n, bucket) {
		struct fio_file *f = list_entry(n, struct fio_file, hash_list);

		if (!strcmp(f->file_name, name)) {
			assert(f->fd != -1);
			return f;
		}
	}

	dump_hash();
	return NULL;
}

struct fio_file *lookup_file_hash(const char *name)
{
	struct fio_file *f;

	fio_mutex_down(hash_lock);
	f = __lookup_file_hash(name);
	fio_mutex_up(hash_lock);
	return f;
}

struct fio_file *add_file_hash(struct fio_file *f)
{
	struct fio_file *alias;

	if (f->flags & FIO_FILE_HASHED)
		return NULL;

	INIT_LIST_HEAD(&f->hash_list);

	fio_mutex_down(hash_lock);

	alias = __lookup_file_hash(f->file_name);
	if (!alias) {
		f->flags |= FIO_FILE_HASHED;
		list_add_tail(&f->hash_list, &file_hash[hash(f->file_name)]);
	}

	fio_mutex_up(hash_lock);
	return alias;
}

void file_hash_init(void *ptr)
{
	unsigned int i;

	file_hash = ptr;
	for (i = 0; i < HASH_BUCKETS; i++)
		INIT_LIST_HEAD(&file_hash[i]);

	hash_lock = fio_mutex_init(1);
}
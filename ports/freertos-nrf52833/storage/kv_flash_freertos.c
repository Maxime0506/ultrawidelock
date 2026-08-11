/*
 * The port's persistent key-value store, on two pages of internal flash.
 *
 * The part can only clear bits, and can only set them back a whole page at a
 * time, so a value is never overwritten in place: every write appends a record
 * and the newest record for a key wins. When the active page runs out, the live
 * set is copied to the other page and the old one is erased. That is the
 * classic wear-levelled log, and everything below is about surviving a reset in
 * the middle of one.
 *
 * The layout matches the Zephyr oracle's reservation exactly: two 4 KB pages at
 * 0x7e000. That address is pinned there because it holds the reader's private
 * key and the trust anchors, and a board is reflashed without erasing so those
 * survive; moving it would silently orphan them.
 */
#include <woz_freertos_kv.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <woz_freertos_platform.h>

#ifndef WOZ_FREERTOS_KV_BASE
#define WOZ_FREERTOS_KV_BASE 0x7e000u
#endif

#define KV_PAGE_SIZE WOZ_FREERTOS_FLASH_PAGE_SIZE
#define KV_PAGE_COUNT 2u
#define KV_ALIGN WOZ_FREERTOS_FLASH_WRITE_ALIGN

/*
 * The page header is written last, after the page's records are in place, so a
 * reset during compaction leaves the half-built page unclaimed and the old one
 * still active. Nothing is lost by that; a repeat of the compaction is.
 */
#define KV_MAGIC 0x574f5a4bu /* "WOZK" */

struct kv_page_header {
	uint32_t magic;
	uint32_t sequence;
};

#define KV_HEADER_SIZE ((uint32_t)sizeof(struct kv_page_header))

/*
 * A record's state word is written after its payload, so a reset partway
 * through a write leaves the record unclaimed rather than half-believed. The
 * values only ever clear bits, which is the one thing flash allows without an
 * erase: erased, then valid, then deleted.
 */
#define KV_STATE_FREE 0xffffffffu
#define KV_STATE_VALID 0x0000ffffu
#define KV_STATE_DELETED 0x00000000u

struct kv_record_header {
	uint16_t key;
	uint16_t length;
	uint32_t state;
};

#define KV_RECORD_HEADER_SIZE ((uint32_t)sizeof(struct kv_record_header))

static bool s_mounted;
static uint32_t s_active_page;
static uint32_t s_sequence;
static uint32_t s_write_offset; /* page-relative, always aligned */

static uint32_t align_up(uint32_t value)
{
	return (value + (KV_ALIGN - 1u)) & ~(KV_ALIGN - 1u);
}

static uint32_t page_base(uint32_t page)
{
	return WOZ_FREERTOS_KV_BASE + page * KV_PAGE_SIZE;
}

static uint32_t record_size(uint16_t length)
{
	return KV_RECORD_HEADER_SIZE + align_up(length);
}

static int read_at(uint32_t page, uint32_t offset, void *buffer, size_t length)
{
	return woz_freertos_flash_read(page_base(page) + offset, buffer, length);
}

static int write_at(uint32_t page, uint32_t offset, const void *data, size_t length)
{
	return woz_freertos_flash_write(page_base(page) + offset, data, length);
}

/*
 * Writes a payload that is not a whole number of words. The tail is padded with
 * ones, which is what an erased byte already reads as, so the padding carries
 * no information and a reader never sees it.
 */
static int write_padded(uint32_t page, uint32_t offset, const void *data, uint16_t length)
{
	uint8_t tail[KV_ALIGN];
	uint32_t whole = length & ~(KV_ALIGN - 1u);
	uint32_t remainder = length - whole;

	if (whole != 0u && write_at(page, offset, data, whole) != 0) {
		return WOZ_KV_IO;
	}
	if (remainder == 0u) {
		return WOZ_KV_OK;
	}

	memset(tail, 0xff, sizeof(tail));
	memcpy(tail, (const uint8_t *)data + whole, remainder);
	if (write_at(page, offset + whole, tail, sizeof(tail)) != 0) {
		return WOZ_KV_IO;
	}
	return WOZ_KV_OK;
}

static int page_header_read(uint32_t page, struct kv_page_header *header)
{
	if (read_at(page, 0, header, sizeof(*header)) != 0) {
		return WOZ_KV_IO;
	}
	return header->magic == KV_MAGIC ? WOZ_KV_OK : WOZ_KV_NOT_FOUND;
}

/*
 * Walks a page's records. Stops at the first slot that has never been written,
 * which is how the end of the log is recognised: a torn header leaves a length
 * that cannot be trusted to skip past, so nothing beyond it is read.
 */
typedef bool (*kv_visit_fn)(uint16_t key, uint16_t length, uint32_t payload_offset, void *context);

static int page_walk(uint32_t page, kv_visit_fn visit, void *context, uint32_t *end_offset)
{
	uint32_t offset = KV_HEADER_SIZE;

	while (offset + KV_RECORD_HEADER_SIZE <= KV_PAGE_SIZE) {
		struct kv_record_header header;
		uint32_t total;

		if (read_at(page, offset, &header, sizeof(header)) != 0) {
			return WOZ_KV_IO;
		}
		if (header.key == WOZ_KV_KEY_NONE && header.length == 0xffffu &&
		    header.state == KV_STATE_FREE) {
			break;
		}
		/*
		 * A header whose length never landed cannot be stepped over, so
		 * the log ends here whatever follows.
		 */
		if (header.length == 0xffffu || header.length > WOZ_KV_VALUE_MAX) {
			break;
		}

		total = record_size(header.length);
		if (offset + total > KV_PAGE_SIZE) {
			break;
		}
		if (header.state == KV_STATE_VALID && visit != NULL &&
		    !visit(header.key, header.length, offset + KV_RECORD_HEADER_SIZE, context)) {
			offset += total;
			break;
		}
		offset += total;
	}

	if (end_offset != NULL) {
		*end_offset = offset;
	}
	return WOZ_KV_OK;
}

/* Formats one page: erase, then claim it with the next sequence number. */
static int page_format(uint32_t page, uint32_t sequence)
{
	struct kv_page_header header = {KV_MAGIC, sequence};

	if (woz_freertos_flash_erase(page_base(page), KV_PAGE_SIZE) != 0) {
		return WOZ_KV_IO;
	}
	if (write_at(page, 0, &header, sizeof(header)) != 0) {
		return WOZ_KV_IO;
	}
	return WOZ_KV_OK;
}

struct find_context {
	uint16_t key;
	uint16_t length;
	uint32_t offset;
	bool found;
};

/*
 * The newest record for a key wins, so the walk does not stop at the first
 * match: it keeps the last one it sees.
 */
static bool find_visit(uint16_t key, uint16_t length, uint32_t payload_offset, void *context)
{
	struct find_context *find = context;

	if (key == find->key) {
		find->length = length;
		find->offset = payload_offset;
		find->found = true;
	}
	return true;
}

static int find_record(uint16_t key, struct find_context *find)
{
	int rc;

	find->key = key;
	find->found = false;
	rc = page_walk(s_active_page, find_visit, find, NULL);
	if (rc != WOZ_KV_OK) {
		return rc;
	}
	return find->found ? WOZ_KV_OK : WOZ_KV_NOT_FOUND;
}

static int append(uint16_t key, const void *value, uint16_t length)
{
	struct kv_record_header header = {key, length, KV_STATE_FREE};
	uint32_t offset = s_write_offset;
	uint32_t state = KV_STATE_VALID;
	int rc;

	if (offset + record_size(length) > KV_PAGE_SIZE) {
		return WOZ_KV_FULL;
	}

	/*
	 * Header first with a free state, then the payload, then the state.
	 * Only the last write makes the record real, so a reset anywhere before
	 * it leaves a record the walk steps over.
	 */
	if (write_at(s_active_page, offset, &header, sizeof(header)) != 0) {
		return WOZ_KV_IO;
	}
	if (length != 0u) {
		rc = write_padded(s_active_page, offset + KV_RECORD_HEADER_SIZE, value, length);
		if (rc != WOZ_KV_OK) {
			return rc;
		}
	}
	if (write_at(s_active_page, offset + offsetof(struct kv_record_header, state), &state,
		     sizeof(state)) != 0) {
		return WOZ_KV_IO;
	}

	s_write_offset = offset + record_size(length);
	return WOZ_KV_OK;
}

struct copy_context {
	uint32_t source_page;
	uint32_t target_page;
	uint32_t target_offset;
	uint16_t skip_key;
	int rc;
};

/*
 * Copies the live value for a key, but only when the record being visited is
 * the newest one for it. Copying the superseded ones too would still read
 * correctly, because the newest would still be last, but it would carry the
 * churn across and compact again almost immediately: on this part the whole
 * point of compaction is to reclaim that space.
 */
static bool copy_visit(uint16_t key, uint16_t length, uint32_t payload_offset, void *context)
{
	struct copy_context *copy = context;
	struct find_context newest = {key, 0, 0, false};
	struct kv_record_header header = {key, length, KV_STATE_FREE};
	uint32_t state = KV_STATE_VALID;
	uint8_t buffer[WOZ_KV_VALUE_MAX];

	if (key == copy->skip_key) {
		return true;
	}
	if (page_walk(copy->source_page, find_visit, &newest, NULL) != WOZ_KV_OK) {
		copy->rc = WOZ_KV_IO;
		return false;
	}
	if (newest.offset != payload_offset) {
		return true;
	}

	if (copy->target_offset + record_size(length) > KV_PAGE_SIZE) {
		copy->rc = WOZ_KV_FULL;
		return false;
	}
	if (length != 0u && read_at(copy->source_page, payload_offset, buffer, length) != 0) {
		copy->rc = WOZ_KV_IO;
		return false;
	}
	if (write_at(copy->target_page, copy->target_offset, &header, sizeof(header)) != 0) {
		copy->rc = WOZ_KV_IO;
		return false;
	}
	if (length != 0u && write_padded(copy->target_page,
					 copy->target_offset + KV_RECORD_HEADER_SIZE, buffer,
					 length) != WOZ_KV_OK) {
		copy->rc = WOZ_KV_IO;
		return false;
	}
	if (write_at(copy->target_page,
		     copy->target_offset + offsetof(struct kv_record_header, state), &state,
		     sizeof(state)) != 0) {
		copy->rc = WOZ_KV_IO;
		return false;
	}

	copy->target_offset += record_size(length);
	return true;
}

/*
 * Moves the live set to the other page and erases this one. The target's header
 * is written last: until it is, the old page is still the one with a header and
 * a reset simply loses the work, not the data.
 */
static int compact(uint16_t skip_key)
{
	uint32_t target = (s_active_page + 1u) % KV_PAGE_COUNT;
	struct copy_context copy = {s_active_page, target, KV_HEADER_SIZE, skip_key, WOZ_KV_OK};
	struct kv_page_header header = {KV_MAGIC, s_sequence + 1u};
	int rc;

	if (woz_freertos_flash_erase(page_base(target), KV_PAGE_SIZE) != 0) {
		return WOZ_KV_IO;
	}

	rc = page_walk(s_active_page, copy_visit, &copy, NULL);
	if (rc != WOZ_KV_OK) {
		return rc;
	}
	if (copy.rc != WOZ_KV_OK) {
		return copy.rc;
	}

	if (write_at(target, 0, &header, sizeof(header)) != 0) {
		return WOZ_KV_IO;
	}
	if (woz_freertos_flash_erase(page_base(s_active_page), KV_PAGE_SIZE) != 0) {
		return WOZ_KV_IO;
	}

	s_active_page = target;
	s_sequence = header.sequence;
	s_write_offset = copy.target_offset;
	return WOZ_KV_OK;
}

int woz_freertos_kv_init(void)
{
	struct kv_page_header headers[KV_PAGE_COUNT];
	bool valid[KV_PAGE_COUNT];
	uint32_t page;
	int rc;

	if (s_mounted) {
		return WOZ_KV_OK;
	}

	for (page = 0; page < KV_PAGE_COUNT; page++) {
		rc = page_header_read(page, &headers[page]);
		if (rc == WOZ_KV_IO) {
			return WOZ_KV_IO;
		}
		valid[page] = (rc == WOZ_KV_OK);
	}

	if (!valid[0] && !valid[1]) {
		/* Never formatted, or both pages lost. Start clean. */
		rc = page_format(0, 1);
		if (rc != WOZ_KV_OK) {
			return rc;
		}
		if (woz_freertos_flash_erase(page_base(1), KV_PAGE_SIZE) != 0) {
			return WOZ_KV_IO;
		}
		s_active_page = 0;
		s_sequence = 1;
		s_write_offset = KV_HEADER_SIZE;
		s_mounted = true;
		return WOZ_KV_OK;
	}

	if (valid[0] && valid[1]) {
		/*
		 * Both claimed. A compaction was interrupted after the new
		 * header landed but before the old page was erased, so the
		 * higher sequence is the finished one and the other is stale.
		 */
		s_active_page = (headers[1].sequence > headers[0].sequence) ? 1u : 0u;
		if (woz_freertos_flash_erase(page_base((s_active_page + 1u) % KV_PAGE_COUNT),
					     KV_PAGE_SIZE) != 0) {
			return WOZ_KV_IO;
		}
	} else {
		s_active_page = valid[0] ? 0u : 1u;
	}

	s_sequence = headers[s_active_page].sequence;
	rc = page_walk(s_active_page, NULL, NULL, &s_write_offset);
	if (rc != WOZ_KV_OK) {
		return rc;
	}
	s_mounted = true;
	return WOZ_KV_OK;
}

int woz_freertos_kv_get(uint16_t key, void *value, size_t *length)
{
	struct find_context find;
	size_t capacity;
	int rc;

	if (length == NULL || key == WOZ_KV_KEY_NONE) {
		return WOZ_KV_INVALID;
	}
	rc = woz_freertos_kv_init();
	if (rc != WOZ_KV_OK) {
		return rc;
	}

	capacity = *length;
	rc = find_record(key, &find);
	if (rc != WOZ_KV_OK) {
		return rc;
	}

	/* Reported whether or not it fits, so a caller can size a retry. */
	*length = find.length;
	if (find.length > capacity) {
		return WOZ_KV_INVALID;
	}
	if (find.length != 0u &&
	    read_at(s_active_page, find.offset, value, find.length) != 0) {
		return WOZ_KV_IO;
	}
	return WOZ_KV_OK;
}

int woz_freertos_kv_set(uint16_t key, const void *value, size_t length)
{
	int rc;

	if (key == WOZ_KV_KEY_NONE || length > WOZ_KV_VALUE_MAX ||
	    (length != 0u && value == NULL)) {
		return WOZ_KV_INVALID;
	}
	rc = woz_freertos_kv_init();
	if (rc != WOZ_KV_OK) {
		return rc;
	}

	rc = append(key, value, (uint16_t)length);
	if (rc != WOZ_KV_FULL) {
		return rc;
	}

	/*
	 * Out of room. The superseded copies of this key are about to be
	 * replaced, so compaction drops them rather than carrying them over
	 * only to be superseded again on the next line.
	 */
	rc = compact(key);
	if (rc != WOZ_KV_OK) {
		return rc;
	}
	return append(key, value, (uint16_t)length);
}

int woz_freertos_kv_delete(uint16_t key)
{
	struct find_context find;
	uint32_t state = KV_STATE_DELETED;
	uint32_t header_offset;
	unsigned struck = 0;
	int rc;

	if (key == WOZ_KV_KEY_NONE) {
		return WOZ_KV_INVALID;
	}
	rc = woz_freertos_kv_init();
	if (rc != WOZ_KV_OK) {
		return rc;
	}

	/*
	 * Every record for the key has to be struck out, not only the newest:
	 * leaving an older one valid would resurrect a stale value.
	 */
	for (;;) {
		rc = find_record(key, &find);
		if (rc == WOZ_KV_NOT_FOUND) {
			break;
		}
		if (rc != WOZ_KV_OK) {
			return rc;
		}
		header_offset = find.offset - KV_RECORD_HEADER_SIZE;
		if (write_at(s_active_page,
			     header_offset + offsetof(struct kv_record_header, state), &state,
			     sizeof(state)) != 0) {
			return WOZ_KV_IO;
		}
		struck++;
	}

	return struck != 0u ? WOZ_KV_OK : WOZ_KV_NOT_FOUND;
}

int woz_freertos_kv_erase_all(void)
{
	int rc;

	rc = page_format(0, 1);
	if (rc != WOZ_KV_OK) {
		return rc;
	}
	if (woz_freertos_flash_erase(page_base(1), KV_PAGE_SIZE) != 0) {
		return WOZ_KV_IO;
	}

	s_active_page = 0;
	s_sequence = 1;
	s_write_offset = KV_HEADER_SIZE;
	s_mounted = true;
	return WOZ_KV_OK;
}

size_t woz_freertos_kv_free_bytes(void)
{
	if (!s_mounted) {
		return 0;
	}
	return KV_PAGE_SIZE - s_write_offset;
}

// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdarg.h>
/* Exercise device/error paths without requiring privileged loop devices. */
static int test_fstat(int fd, struct stat *st);
static int test_ioctl(int fd, unsigned long request, ...);
#define fstat test_fstat
#define ioctl test_ioctl
#define main avb_verify_main
#include "../avb_verify.c"
#undef main
#undef ioctl
#undef fstat
#undef NDEBUG
#include <assert.h>

static int stat_mode, ioctl_error, ioctl_calls;
static uint64_t device_size;

static int test_fstat(int fd, struct stat *st)
{
	if (stat_mode == 1) {
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFBLK;
		return 0;
	}
	if (stat_mode == 2) {
		errno = EIO;
		return -1;
	}
	return fstat(fd, st);
}

static int test_ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	(void)fd;
	assert(request == BLKGETSIZE64);
	ioctl_calls++;
	if (ioctl_error) {
		errno = EIO;
		return -1;
	}
	va_start(args, request);
	*va_arg(args, uint64_t *) = device_size;
	va_end(args);
	return 0;
}

static void le32(uint8_t *p, uint32_t v)
{
	for (unsigned i = 0; i < 4; i++)
		p[i] = v >> (i * 8);
}

static void test_hints(void)
{
	uint8_t sb[4096] = {0};
	le32(sb + 1024, EROFS_SUPER_MAGIC_V1);
	le32(sb + 1060, 1024);
	for (unsigned shift = 0; shift < 256; shift++) {
		sb[1036] = shift;
		uint64_t expected = shift >= 9 && shift <= 16 ? 1024ULL << shift : 0;
		assert(detect_fs_size(sb, sizeof(sb), INT64_MAX) == expected);
	}
	sb[1036] = 12;
	le32(sb + 1104, 0x80);
	sb[1038] = 1;
	assert(detect_fs_size(sb, sizeof(sb), INT64_MAX) == ((1ULL << 32) + 1024) * 4096);
	assert(detect_fs_size(sb, 1151, INT64_MAX) == 0);
	assert(detect_fs_size(sb, sizeof(sb), 4096) == 0);

	memset(sb, 0, sizeof(sb));
	sb[1080] = 0x53; sb[1081] = 0xef;
	le32(sb + 1028, 4096);
	for (unsigned shift = 0; shift < 128; shift++) {
		le32(sb + 1048, shift);
		uint64_t expected = shift <= 6 ? 4096ULL << (shift + 10) : 0;
		assert(detect_fs_size(sb, sizeof(sb), INT64_MAX) == expected);
	}
	le32(sb + 1048, 2);
	le32(sb + 1360, 1);
	assert(detect_fs_size(sb, sizeof(sb), INT64_MAX) == 4096ULL * 4096);
	le32(sb + 1120, 0x80);
	assert(detect_fs_size(sb, sizeof(sb), INT64_MAX) == ((1ULL << 32) + 4096) * 4096);
	le32(sb + 1360, UINT32_MAX);
	assert(detect_fs_size(sb, sizeof(sb), INT64_MAX) == 0);
	assert(detect_fs_size(sb, 2047, INT64_MAX) == 0);

	memset(sb, 0, sizeof(sb));
	le32(sb, SQUASHFS_MAGIC);
	le32(sb + 12, 131072); sb[22] = 17; sb[28] = 4;
	le32(sb + 40, 12345);
	assert(detect_fs_size(sb, 96, 12345) == 12345);
	assert(detect_fs_size(sb, 95, INT64_MAX) == 0);
	assert(detect_fs_size(sb, 96, 12344) == 0);
	sb[22] = 64;
	assert(detect_fs_size(sb, 96, INT64_MAX) == 0);
	sb[22] = 17; sb[28] = 3;
	assert(detect_fs_size(sb, 96, INT64_MAX) == 0);
}

static int fail_seek(void *cookie, off64_t *offset, int whence)
{
	(void)cookie; (void)offset; (void)whence;
	errno = ESPIPE;
	return -1;
}

int main(void)
{
	uint64_t size;
	char buf[64];
	AvbFooter footer;
	FILE *fp = tmpfile();
	assert(fp);
	assert(ftruncate(fileno(fp), (off_t)(5ULL << 30)) == 0);
	assert(get_image_size(fp, &size) && size == (5ULL << 30));
	assert(read_at(fp, (4ULL << 30) + 123, buf, sizeof(buf)));
	assert(!read_at(fp, UINT64_MAX, buf, 1) && errno == EOVERFLOW);
	assert(!read_at(fp, size - 1, buf, sizeof(buf)) && errno == EIO);
	assert(scan_footer(fp, size - 4096, size + 4096, &footer) == -1);

	stat_mode = 1;
	device_size = 6ULL << 30;
	assert(get_image_size(fp, &size) && size == device_size && ioctl_calls == 1);
	device_size = UINT64_MAX;
	assert(!get_image_size(fp, &size) && errno == EOVERFLOW);
	ioctl_error = 1;
	assert(!get_image_size(fp, &size) && errno == EIO);
	stat_mode = 2;
	assert(!get_image_size(fp, &size) && errno == EIO);
	stat_mode = 0;
	fclose(fp);
	fp = fopen("/dev/null", "rb");
	assert(fp && !get_image_size(fp, &size) && errno == EINVAL);
	fclose(fp);
	fp = fopencookie(NULL, "r", (cookie_io_functions_t){.seek = fail_seek});
	assert(fp && !read_at(fp, 0, buf, 1) && errno == ESPIPE);
	fclose(fp);
	test_hints();
	puts("size, hint, seek, short-read, and simulated BLKGETSIZE64 tests passed");
	return 0;
}

/* Developer tool: inspect an O&E II .bar.
 *   oe2tool list <bar>
 *   oe2tool extract <bar> <id> <out>      (decompressed resource)
 *   oe2tool wav <bar> <id> <out.wav>      (decode a .pmd resource)
 *   oe2tool pcm <bar> <out_dir>           (decode all cmid resources to <crc>-<len>.pcm)
 *   oe2tool mod <mod>                     (check the .mod is the supported build)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "bar.h"
#include "pmd.h"
#include "moddata.h"

static void le16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void le32(FILE *f, unsigned v) { le16(f, v & 0xffff); le16(f, v >> 16); }

static int export_all_pcm(Bar *bar, const char *out_dir)
{
	int count = 0, failures = 0;
	for (int i = 0; i < bar->count; i++) {
		size_t n = 0;
		uint8_t *d = Bar_load(bar, bar->entries[i].id, &n);
		if (!d)
			continue;
		if (n >= 4 && !memcmp(d, "cmid", 4)) {
			int16_t *pcm = NULL;
			int ns = 0;
			if (Pmd_decode(d, n, &pcm, &ns) != 0) {
				fprintf(stderr, "Failed resource %d %s\n", bar->entries[i].id, bar->entries[i].name);
				failures++;
			} else {
				char path[2048];
				snprintf(path, sizeof(path), "%s/%08lx-%lu.pcm",
				         out_dir, (unsigned long)crc32(0L, d, (uInt)n), (unsigned long)n);
				FILE *f = fopen(path, "wb");
				if (!f) {
					fprintf(stderr, "Cannot write %s\n", path);
					free(pcm);
					free(d);
					return 2;
				}
				for (int j = 0; j < ns; j++)
					le16(f, (uint16_t)pcm[j]);
				if (fclose(f) != 0) {
					free(pcm);
					free(d);
					return 2;
				}
				count++;
				free(pcm);
			}
		}
		free(d);
	}
	printf("BREW audio: %d decoded, %d failed\n", count, failures);
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	Bar bar;
	if (argc >= 3 && !strcmp(argv[1], "mod")) {
		int r = ModData_open(&g_mod, argv[2]);
		printf("%s\n", r == 0 ? "ok: supported orcsandelves2.mod" : r == -2 ? "wrong build (size/CRC mismatch)" : "cannot read");
		ModData_close(&g_mod);
		return r ? 1 : 0;
	}
	if (argc < 3) {
		fprintf(stderr, "usage: %s list|extract|wav|pcm <bar> [id|out_dir] [out]\n", argv[0]);
		return 2;
	}
	if (Bar_open(&bar, argv[2]) != 0) {
		fprintf(stderr, "cannot open %s\n", argv[2]);
		return 1;
	}
	if (!strcmp(argv[1], "list")) {
		for (int i = 0; i < bar.count; i++)
			printf("%5d %8u %s\n", bar.entries[i].id, bar.entries[i].size, bar.entries[i].name);
	} else if (!strcmp(argv[1], "pcm") && argc >= 4) {
		int r = export_all_pcm(&bar, argv[3]);
		Bar_close(&bar);
		return r;
	} else if (argc >= 5 && !strcmp(argv[1], "extract")) {
		size_t n;
		uint8_t *d = Bar_load(&bar, atoi(argv[3]), &n);
		FILE *f = d ? fopen(argv[4], "wb") : NULL;
		if (!f) return 1;
		fwrite(d, 1, n, f);
		fclose(f);
		free(d);
	} else if (argc >= 5 && !strcmp(argv[1], "wav")) {
		size_t n;
		int16_t *pcm;
		int ns;
		uint8_t *d = Bar_load(&bar, atoi(argv[3]), &n);
		if (!d || Pmd_decode(d, n, &pcm, &ns) != 0) {
			fprintf(stderr, "decode failed\n");
			return 1;
		}
		FILE *f = fopen(argv[4], "wb");
		fwrite("RIFF", 1, 4, f); le32(f, 36 + ns * 2); fwrite("WAVEfmt ", 1, 8, f);
		le32(f, 16); le16(f, 1); le16(f, 1); le32(f, PMD_SAMPLE_RATE); le32(f, PMD_SAMPLE_RATE * 2);
		le16(f, 2); le16(f, 16); fwrite("data", 1, 4, f); le32(f, ns * 2);
		fwrite(pcm, 2, ns, f);
		fclose(f);
		printf("%d samples (%.2f s)\n", ns, ns / (double)PMD_SAMPLE_RATE);
		free(pcm);
		free(d);
	}
	Bar_close(&bar);
	return 0;
}

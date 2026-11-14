/*
 * Qualcomm CMF ("cmid") wave tracks -> PCM.
 *
 * Every O&E II sound and music resource is a CMF file whose single track carries
 * QCELP-13k audio in 0xF1 "wave" events (draft-atarius-cmf-00). Each event is
 *   FF F1 <u16 len> <u16 codec=0x0044> <u32 total> <u8 seq> <len-7 bytes of QCELP packets>
 * Packets are variable rate: a rate byte, then 0/3/7/16/34 payload bytes.
 * Decoded directly to 8 kHz 16-bit mono PCM via embedded QCELP-13k decoder.
 */
#include "pmd.h"
#include "qcelp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static int payload_len(unsigned rate)
{
	switch (rate) {
	case 0: case 0x0e: return 0;   /* blank, erasure */
	case 1: return 3;              /* eighth */
	case 2: case 5: return 7;      /* quarter */
	case 3: return 16;             /* half */
	case 4: return 34;             /* full */
	default: return -1;
	}
}

int Pmd_decode(const uint8_t *cmf, size_t len, int16_t **pcm, int *samples)
{
	QCELPContext *ctx = NULL;
	size_t p, end;
	int waveVolume = 63;
	float frame[160];
	int cap = 8192;
	int n = 0;
	int16_t *s = NULL;
	int ok = -1;

	*pcm = NULL;
	*samples = 0;
	if (len < 16 || memcmp(cmf, "cmid", 4) != 0)
		return -1;
	for (p = 8; p + 8 <= len && memcmp(cmf + p, "trac", 4) != 0; p++)
		;
	if (p + 8 > len)
		return -1;
	end = p + 8 + be32(cmf + p + 4);
	if (end > len)
		end = len;
	p += 8;

	s = (int16_t *)malloc(cap * sizeof(int16_t));
	if (!s)
		return -1;

	ctx = qcelp_create();
	if (!ctx) {
		free(s);
		return -1;
	}

	while (p + 2 <= end) {
		unsigned b0, cmd;
		p++; /* delta time */
		b0 = cmf[p++];
		if (b0 != 0xFF) {
			p += 2; /* note event; unused by wave tracks */
			continue;
		}
		if (p >= end)
			break;
		cmd = cmf[p++];
		if (cmd == 0xDF)
			break;
		if (cmd >= 0xF1 && cmd <= 0xF4) {
			unsigned l;
			if (p + 2 > end)
				break;
			l = be16(cmf + p);
			if (cmd == 0xF1 && l > 7 && p + 2 + l <= end && be16(cmf + p + 2) == 0x0044) {
				const uint8_t *q = cmf + p + 2 + 7, *qe = cmf + p + 2 + l;
				while (q < qe) {
					int pay = payload_len(*q);
					if (pay < 0 || q + 1 + pay > qe)
						break;
					if (qcelp_decode_packet(ctx, q, 1 + pay, frame) == 0) {
						if (n + 160 > cap) {
							while (cap < n + 160)
								cap *= 2;
							int16_t *ns = (int16_t *)realloc(s, cap * sizeof(int16_t));
							if (!ns)
								goto done;
							s = ns;
						}
						for (int k = 0; k < 160; k++) {
							float v = frame[k];
							v = v > 1.f ? 1.f : v < -1.f ? -1.f : v;
							s[n++] = (int16_t)(v * 32767.f);
						}
					}
					q += 1 + pay;
				}
			}
			p += 2 + l;
		} else {
			if (cmd == 0xE8 && p < end)
				waveVolume = cmf[p] & 0x3f;
			p++; /* one-byte parameter */
		}
	}

	if (waveVolume < 63) {
		for (int k = 0; k < n; k++)
			s[k] = (int16_t)(s[k] * waveVolume / 63);
	}
	ok = 0;

done:
	qcelp_free(ctx);
	if (ok != 0) {
		free(s);
		return -1;
	}
	*pcm = s;
	*samples = n;
	return 0;
}

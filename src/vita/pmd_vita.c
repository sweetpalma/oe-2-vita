/* Pre-decoded QCELP-13k PCM loader for PS Vita and standalone builds.
 * Uses CRC32 and byte length to load 8 kHz 16-bit little-endian PCM clips
 * from app0:data/audio/ (or local data/audio/), avoiding libavcodec. */
#include "pmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

int Pmd_decode(const uint8_t *cmf, size_t len, int16_t **pcm, int *samples) {
  static const char *const prefixes[] = {
      "app0:data/audio",
      "ux0:data/oe2/audio",
      "data/audio",
      "/data/audio",
  };
  unsigned long crc = (unsigned long)crc32(0L, cmf, (uInt)len);
  FILE *f = NULL;

  *pcm = NULL;
  *samples = 0;
  for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%08lx-%lu.pcm", prefixes[i], crc,
             (unsigned long)len);
    f = fopen(path, "rb");
    if (f)
      break;
  }
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long bytes = ftell(f);
  if (bytes <= 0 || (bytes % 2) != 0 || bytes > 64000000L) {
    fclose(f);
    return -1;
  }
  rewind(f);
  uint8_t *raw = malloc((size_t)bytes);
  if (!raw || fread(raw, 1, (size_t)bytes, f) != (size_t)bytes) {
    free(raw);
    fclose(f);
    return -1;
  }
  fclose(f);
  *pcm = (int16_t *)raw;
  *samples = (int)(bytes / 2);
  return 0;
}

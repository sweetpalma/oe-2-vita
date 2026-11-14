/*
 * QCELP-13k (TIA/EIA/IS-733) speech/audio decoder.
 * Standalone decoder extracted from FFmpeg (LGPL v2.1+).
 * Authors: Reynaldo H. Verdejo Pinochet, Colin McQuillan.
 */
#ifndef QCELP_H
#define QCELP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QCELPContext QCELPContext;

QCELPContext *qcelp_create(void);
void qcelp_free(QCELPContext *q);
void qcelp_reset(QCELPContext *q);

/* Decodes one QCELP packet (buf_size = 1, 4, 8, 17, or 35 bytes).
 * Writes 160 float samples to outbuffer (8000 Hz).
 * Returns 0 on success, negative on error. */
int qcelp_decode_packet(QCELPContext *q, const uint8_t *buf, int buf_size, float outbuffer[160]);

#ifdef __cplusplus
}
#endif

#endif /* QCELP_H */

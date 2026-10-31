/* Wii disc image (plain ISO) access: game partition decryption, FST, and group rebuild. */
#ifndef DLT_WIIDISC_H
#define DLT_WIIDISC_H
#include <stdint.h>
#include <stdio.h>
#include "crypto.h"

#define WII_CLUSTER       0x8000
#define WII_CLUSTER_DATA  0x7C00
#define WII_GROUP_CLUSTERS 64
#define WII_GROUP_ENC     (WII_GROUP_CLUSTERS*WII_CLUSTER)       /* 2 MiB on disc */
#define WII_GROUP_DATA    (WII_GROUP_CLUSTERS*WII_CLUSTER_DATA)  /* 1.9375 MiB of payload */

typedef struct {
    char     path[200];
    uint64_t off;      /* offset in decrypted partition data */
    uint32_t size;
    uint32_t entry;    /* FST entry index */
} wii_file;

typedef struct {
    FILE    *f;
    char     game_id[7];
    uint64_t part_off, data_off, data_size, h3_off, tmd_off;
    uint32_t tmd_size;
    aes128_ctx key;
    /* single-cluster decrypt cache */
    int64_t  cache_cl;
    uint8_t  cache[WII_CLUSTER_DATA];
    /* FST */
    uint64_t fst_off; uint32_t fst_size;
    uint8_t *fst;           /* raw FST bytes */
    wii_file *files; int nfiles;
} wii_part;

int  dlt_fseek(FILE *f, uint64_t off);
int  wii_open(wii_part *p, const char *path, char *err, size_t errlen);
void wii_close(wii_part *p);
int  wii_read(wii_part *p, uint64_t off, void *buf, size_t n);        /* decrypted partition data */
const wii_file *wii_find(const wii_part *p, const char *path);
int  wii_read_file(wii_part *p, const wii_file *wf, uint8_t **out);   /* malloc'd */

/* Read one 2 MiB group from file f: decrypted hash blocks (64*0x400) and payload (WII_GROUP_DATA). */
int  wii_read_group(wii_part *p, FILE *f, uint32_t g, uint8_t *hashblocks, uint8_t *data);
/* Recompute H0/H1/H2 into hashblocks (padding kept), encrypt to out (2 MiB); h2 receives the group's H2 table (0xA0). */
void wii_build_group(const aes128_ctx *key, uint8_t *hashblocks, const uint8_t *data, uint8_t *out, uint8_t h2[0xA0]);
/* zero the TMD signature and brute-force a reserved field until SHA1(TMD body)[0]==0 ("fakesign") */
int  wii_fakesign_tmd(uint8_t *tmd, uint32_t tmd_size);
#endif

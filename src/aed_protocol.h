/**
 * @file    aed_protocol.h
 * @brief   AED -> App event packets (same 15 / 20 byte layout as the Manikin)
 *
 * The App is the BLE central for BOTH the AED and the Manikin.
 * AED events (notify on NUS TX), one value per packet, sent only on change:
 *   PTYPE_AED_POWER (0x09): 0 button off, 1 waiting for press, 2 power on
 *   PTYPE_AED_SHOCK (0x0A): 0 button off, 1 waiting for press, 2 in progress, 3 delivered
 *   PTYPE_ANNOUNCE (0x01) : reply to command 'C' or '?'
 */

#pragma once

#include <stdint.h>

#define PACKET_MAGIC_0       0xC9
#define PACKET_MAGIC_1       0xC2
#define PROTOCOL_VER         0x06

#define PTYPE_ANNOUNCE       0x01
#define PTYPE_AED_POWER      0x09
#define PTYPE_AED_SHOCK      0x0A
#define PTYPE_AED_TRIGGER    0x0B   /* GPIO 6 monitor event (value: 1 = triggered) */

#define AED_DEVICE_TYPE      2      /* 1 = Manikin, 2 = AED */

/* PTYPE_AED_POWER values */
#define AED_POWER_OFF        0
#define AED_POWER_WAIT_PRESS 1
#define AED_POWER_ON         2

/* PTYPE_AED_SHOCK values */
#define AED_SHOCK_OFF        0
#define AED_SHOCK_WAIT_PRESS 1
#define AED_SHOCK_IN_PROGRESS 2
#define AED_SHOCK_DELIVERED  3

#pragma pack(push, 1)

typedef struct {
    uint8_t  magic[2];       /* [0..1]   0xC9, 0xC2 */
    uint8_t  version;        /* [2]      0x06 */
    uint8_t  type;           /* [3]      event type */
    uint32_t timestamp;      /* [4..7]   little-endian ms since boot */
    uint16_t id;             /* [8..9]   rolling packet index */
    int8_t   value;          /* [10]     event value */
    uint8_t  deviceID[3];    /* [11..13] last 3 MAC bytes */
    uint8_t  checksum;       /* [14]     XOR of bytes [0..13] */
} AedEventPacket;

typedef struct {
    uint8_t  magic[2];       /* [0..1]   0xC9, 0xC2 */
    uint8_t  version;        /* [2]      0x06 */
    uint8_t  type;           /* [3]      0x01 (ANNOUNCE) */
    uint32_t timestamp;      /* [4..7]   little-endian ms since boot */
    uint16_t id;             /* [8..9]   rolling packet index */
    int8_t   battery;        /* [10]     battery %, -1 = not measured */
    uint8_t  deviceID[3];    /* [11..13] last 3 MAC bytes */
    char     name[4];        /* [14..17] unit id, e.g. "001\0" */
    uint8_t  deviceType;     /* [18]     2 = AED */
    uint8_t  checksum;       /* [19]     XOR of bytes [0..18] */
} AedAnnouncePacket;

#pragma pack(pop)

static inline uint8_t aed_proto_checksum(const uint8_t *buf, uint32_t n)
{
    uint8_t cs = 0;
    for (uint32_t i = 0; i < n; ++i) {
        cs ^= buf[i];
    }
    return cs;
}

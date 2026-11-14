#include "gb_decode.h"
#include <math.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t i16(const uint8_t *p) { return (int16_t)u16(p); }
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t i32(const uint8_t *p) {
    uint32_t u = u32(p);
    return u > 0x7FFFFFFFu ? -(int32_t)(0xFFFFFFFFu - u) - 1 : (int32_t)u;
}
static float half_db(uint8_t v) { return (float)v / 2.0f - 120.0f; }

int gb_decode_imu(const uint8_t *p, size_t n, gb_imu *out) {
    int i;
    if (!p || !out || n < 27 || p[0] != 2 || p[1] != 0x01) return 0;
    out->seq = u16(p + 2);
    out->t_us = u32(p + 4);
    for (i = 0; i < 4; i++) out->quat[i] = (float)i16(p + 8 + 2 * i) / 16384.0f;
    for (i = 0; i < 3; i++) out->acc_mg[i] = i16(p + 16 + 2 * i);
    out->flags = p[22];
    out->quat_valid = (p[22] & 0x01) != 0;
    out->audio_valid = (p[22] & 0x08) != 0;
    out->breath = (p[22] & 0x10) != 0;
    out->rms_db[0] = half_db(p[23]);
    out->rms_db[1] = half_db(p[24]);
    out->peak_db[0] = half_db(p[25]);
    out->peak_db[1] = half_db(p[26]);
    return 1;
}

int gb_decode_co2(const uint8_t *p, size_t n, gb_co2 *out) {
    if (!p || !out || n < 12 || p[0] != 1 || p[1] != 0x02) return 0;
    out->seq = u16(p + 2);
    out->t_ms = u32(p + 4);
    out->ppm = u16(p + 8);
    out->flags = p[10];
    out->age_s = p[11];
    out->valid = (p[10] & 0x01) != 0;
    out->trigger = (p[10] & 0x08) != 0;
    return 1;
}

int gb_decode_nav(const uint8_t *p, size_t n, gb_nav *out) {
    if (!p || !out || n < 22 || p[0] != 1 || p[1] != 0x07) return 0;
    out->seq = u16(p + 2);
    out->t_us = u32(p + 4);
    out->mode = p[8];
    out->flags = p[9];
    out->valid = (p[9] & 0x01) != 0;
    out->settled = (p[9] & 0x02) != 0;
    out->moving = (p[9] & 0x04) != 0;
    out->lat = (double)i32(p + 10) / 1e7;
    out->lon = (double)i32(p + 14) / 1e7;
    out->bearing = (float)i16(p + 18) / 100.0f;
    out->speed = (float)u16(p + 20) / 10.0f;
    return 1;
}

void gb_euler_legacy(const float q[4], float *roll, float *pitch, float *yaw) {
    const double rad2deg = 180.0 / 3.14159265358979323846;
    double qw = q[0], qx = q[2], qy = q[1], qz = -(double)q[3];
    double t2 = 2.0 * (qw * qy - qx * qz);
    if (t2 > 1.0) t2 = 1.0;
    if (t2 < -1.0) t2 = -1.0;
    *roll = (float)(atan2(2.0 * (qw * qx + qy * qz), 1.0 - 2.0 * (qx * qx + qy * qy)) * rad2deg + 0.0);
    *pitch = (float)(asin(t2) * rad2deg + 0.0);
    *yaw = (float)(atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz)) * rad2deg + 0.0);
}

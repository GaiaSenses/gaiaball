#include "gb_decode.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b) CHECK(fabs((double)(a) - (double)(b)) < 1e-4)

static const uint8_t IMU[27] = {0x02,0x01,0x02,0x01,0x0D,0x0C,0x0B,0x0A,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
    0xE8,0x03,0x0C,0xFE,0x97,0x00,0x1B,0xF0,0xC8,0xF0,0xE4};
static const uint8_t IMU_V1[31] = {0x01,0x01,0x02,0x01,0x0D,0x0C,0x0B,0x0A,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x00,0xFE,0xFF,0x03,0x00,0x0B,0x00,0x00,0x30,0xF8,0x00,0x00,0xA6,0xFD};
static const uint8_t CO2[12] = {0x01,0x02,0x02,0x01,0x0D,0x0C,0x0B,0x0A,0xE8,0x03,0x05,0x07};
static const uint8_t NAV_A[23] = {0x01,0x07,0x02,0x01,0x4E,0x61,0xBC,0x00,0x01,0x03,0xB8,0xE3,0x58,0xF2,0x36,0x16,
    0xF3,0xE3,0x94,0x11,0x7B,0x00,0x07};  /* anchorSeq 7 */
static const uint8_t NAV_B[23] = {0x01,0x07,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x02,0x06,0x00,0xE9,0xA4,0x35,0x00,0x2E,
    0xB6,0x94,0xB0,0xB9,0xFF,0xFF,0xFF};  /* anchorSeq 255 */

static void test_imu(void) {
    gb_imu o;
    CHECK(gb_decode_imu(IMU, 27, &o) == 1);
    CHECK(o.seq == 258);
    CHECK(o.t_us == 0x0A0B0C0Du);
    NEAR(o.quat[0], 1.0); NEAR(o.quat[1], 0); NEAR(o.quat[2], 0); NEAR(o.quat[3], 0);
    CHECK(o.acc_mg[0] == 1000 && o.acc_mg[1] == -500 && o.acc_mg[2] == 151);
    CHECK(o.flags == 0x1B);
    CHECK(o.quat_valid == 1 && o.audio_valid == 1 && o.breath == 1);
    NEAR(o.rms_db[0], 0); NEAR(o.rms_db[1], -20);
    NEAR(o.peak_db[0], 0); NEAR(o.peak_db[1], -6);
    CHECK(gb_decode_imu(IMU_V1, 31, &o) == 0);   /* version 1 rejected */
    CHECK(gb_decode_imu(IMU, 26, &o) == 0);
    CHECK(gb_decode_imu(IMU, 0, &o) == 0);
    CHECK(gb_decode_imu(CO2, 12, &o) == 0);
    uint8_t big[48]; memset(big, 0xAA, sizeof big); memcpy(big, IMU, 27);
    CHECK(gb_decode_imu(big, 48, &o) == 1 && o.seq == 258);
    uint8_t t[27]; memcpy(t, IMU, 27); t[1] = 0x07;
    CHECK(gb_decode_imu(t, 27, &o) == 0);
    memcpy(t, IMU, 27); t[22] = 0x0B; t[23] = 0; t[24] = 255;  /* no breath; floor and max */
    CHECK(gb_decode_imu(t, 27, &o) == 1 && o.breath == 0);
    NEAR(o.rms_db[0], -120); NEAR(o.rms_db[1], 7.5);
}

static void test_co2(void) {
    gb_co2 o;
    CHECK(gb_decode_co2(CO2, 12, &o) == 1);
    CHECK(o.seq == 258 && o.t_ms == 0x0A0B0C0Du && o.ppm == 1000);
    CHECK(o.flags == 0x05 && o.age_s == 7 && o.valid == 1 && o.trigger == 0);
    CHECK(gb_decode_co2(CO2, 11, &o) == 0);
    uint8_t b[12]; memcpy(b, CO2, 12); b[10] = 0x0D;
    CHECK(gb_decode_co2(b, 12, &o) == 1 && o.trigger == 1);
    memcpy(b, CO2, 12); b[0] = 2; CHECK(gb_decode_co2(b, 12, &o) == 0);
    CHECK(gb_decode_co2(NAV_A, 23, &o) == 0);
}

static void test_nav(void) {
    gb_nav o;
    CHECK(gb_decode_nav(NAV_A, 23, &o) == 1);
    CHECK(o.seq == 258 && o.t_us == 12345678u && o.mode == 1 && o.flags == 0x03);
    CHECK(o.valid == 1 && o.settled == 1 && o.moving == 0);
    CHECK(fabs(o.lat - -22.905556) < 1e-7 && fabs(o.lon - -47.060833) < 1e-7);
    NEAR(o.bearing, 45); NEAR(o.speed, 12.3);
    CHECK(gb_decode_nav(NAV_B, 23, &o) == 1);
    CHECK(o.seq == 65535 && o.t_us == 4294967295u && o.mode == 2);
    CHECK(o.valid == 0 && o.settled == 1 && o.moving == 1);
    CHECK(o.lat == 90.0 && o.lon == -180.0);
    NEAR(o.bearing, -180); NEAR(o.speed, 6553.5);
    CHECK(gb_decode_nav(NAV_A, 22, &o) == 0);  /* 22 bytes: no anchorSeq, rejected */
    CHECK(gb_decode_nav(NAV_A, 0, &o) == 0);
    uint8_t b[25]; memset(b, 0, sizeof b); memcpy(b, NAV_A, 23);
    CHECK(gb_decode_nav(b, 25, &o) == 1);  /* a longer packet still decodes */
    b[0] = 2; CHECK(gb_decode_nav(b, 23, &o) == 0);
    b[0] = 1; b[1] = 0x06; CHECK(gb_decode_nav(b, 23, &o) == 0);
    CHECK(gb_decode_nav(CO2, 12, &o) == 0);
}

static void test_euler(void) {
    float r, p, y;
    const float id[4] = {1, 0, 0, 0};
    gb_euler_legacy(id, &r, &p, &y);
    NEAR(r, 0); NEAR(p, 0); NEAR(y, 0);
    CHECK(!signbit(r) && !signbit(p) && !signbit(y));   /* -0 normalised */
    const float h = 0.70710678f;
    const float z90[4] = {h, 0, 0, h};
    gb_euler_legacy(z90, &r, &p, &y);
    CHECK(fabs(r) < 1e-3 && fabs(p) < 1e-3 && fabs(y + 90) < 1e-3);
    /* x rotation 90: legacy remap puts it on qy -> pitch */
    const float x90[4] = {h, h, 0, 0};
    gb_euler_legacy(x90, &r, &p, &y);
    CHECK(fabs(p - 90) < 0.1);
    /* asin argument clamp: no NaN with slightly over-unit input */
    const float over[4] = {1.0f, 1.0f, 0, 0};
    gb_euler_legacy(over, &r, &p, &y);
    CHECK(!isnan(p));
}

int main(void) {
    test_imu(); test_co2(); test_nav(); test_euler();
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("all decoder tests passed\n");
    return 0;
}

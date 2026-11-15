/* Transport-independent decoder for the Gaiaball BLE packets (no Pd headers).
 * Little-endian, read byte by byte. Source of truth: firmware/docs/protocol.md */
#ifndef GB_DECODE_H
#define GB_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t seq; uint32_t t_us; float quat[4]; int16_t acc_mg[3]; uint8_t flags;
    int quat_valid, audio_valid, breath; float rms_db[2], peak_db[2];
} gb_imu;

typedef struct {
    uint16_t seq; uint32_t t_ms; uint16_t ppm; uint8_t flags, age_s; int valid, trigger;
} gb_co2;

typedef struct {
    uint16_t seq; uint32_t t_us; uint8_t mode, flags; double lat, lon; float bearing, speed;
    int valid, settled, moving;
} gb_nav;

/* Each returns 1 on success, 0 if too short / wrong version / wrong type.
 * Longer packets are accepted (appended fields ignored). */
int gb_decode_imu(const uint8_t *p, size_t n, gb_imu *out);  /* v2, 0x01, n >= 27 */
int gb_decode_co2(const uint8_t *p, size_t n, gb_co2 *out);  /* v1, 0x02, n >= 12 */
int gb_decode_nav(const uint8_t *p, size_t n, gb_nav *out);  /* v1, 0x07, n >= 22 */

/* Legacy firmware Euler convention (degrees): qw=w, qx=y, qy=x, qz=-z. */
void gb_euler_legacy(const float q[4], float *roll, float *pitch, float *yaw);

#ifdef __cplusplus
}
#endif
#endif

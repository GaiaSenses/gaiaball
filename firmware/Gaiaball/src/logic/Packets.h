#pragma once
#include <stdint.h>

#include "AudioFeatures.h"

// Binary BLE wire protocol: every packet is v1 except the movement (IMU) packet, which
// is v2. Little-endian, packed. See firmware/docs/protocol.md.
namespace gaia {

constexpr uint8_t kProtocolVersion = 1;
// The movement (IMU) packet is the only one that moved to v2: acceleration in mg and
// the microphone levels in half-dB (31 -> 27 B). Every other packet stays at v1.
constexpr uint8_t kImuPacketVersion = 2;

enum class PacketType : uint8_t {
  ImuSample = 0x01,
  Co2Sample = 0x02,
  Diagnostics = 0x03,
  Sensors = 0x04,     // audio levels + potentiometer; shares the diag characteristic
  AudioChunk = 0x05,  // decimated raw audio stream; shares the diag characteristic
  MagChunk = 0x06,    // raw magnetometer capture for calibration; diag characteristic
};

// Packet type of the navigation packet (own characteristic, see NavPacket).
constexpr uint8_t kTypeNav = 0x07;

// ImuPacket.flags
constexpr uint8_t kImuQuatValid = 1u << 0;
constexpr uint8_t kImuNineAxis = 1u << 1;
constexpr uint8_t kImuMoving = 1u << 2;
constexpr uint8_t kImuAudioValid = 1u << 3;  // the appended microphone levels are live
// Breath recognized: set while the detector is past its line (fire .. re-arm), and
// latched so that a recognition shorter than one notify interval still reaches the
// next packet sent.
constexpr uint8_t kImuBreath = 1u << 4;

// Co2Packet.flags
constexpr uint8_t kCo2Valid = 1u << 0;
constexpr uint8_t kCo2Stale = 1u << 1;
constexpr uint8_t kCo2VirtualActive = 1u << 2;
constexpr uint8_t kCo2Trigger = 1u << 3;  // ppm crossed the configured trigger level (with hysteresis)

// NavPacket.flags
constexpr uint8_t kNavValid = 1u << 0;    // pose is live (orientation valid)
constexpr uint8_t kNavSettled = 1u << 1;  // motion state machine says the globe is at rest
constexpr uint8_t kNavMoving = 1u << 2;   // instantaneous: speed above the stop threshold

// DiagPacket.flags
constexpr uint8_t kDiagImuOk = 1u << 0;
constexpr uint8_t kDiagAudioOk = 1u << 1;
constexpr uint8_t kDiagCo2Ok = 1u << 2;

// SensorsPacket.flags
constexpr uint8_t kSensAudioOk = 1u << 0;
constexpr uint8_t kSensAudioEnabled = 1u << 1;
constexpr uint8_t kSensPotControls = 1u << 2;
constexpr uint8_t kSensAudioFiltered = 1u << 3;  // RMS computed after the HP/LP band-pass
constexpr uint8_t kSensMetricPeak = 1u << 4;     // breath metric is the peak (else RMS)
constexpr uint8_t kSensAdaptive = 1u << 5;       // breath detector is adaptive (else absolute)

// AudioChunkPacket.flags
constexpr uint8_t kAudioBpfEnabled = 1u << 0;  // band-pass active (chunk itself is RAW)

// MagChunkPacket.flags
constexpr uint8_t kMagChunkCalibrated = 1u << 0;  // samples already hard/soft-iron corrected

// DiagPacket.ahrsFlags — the fusion pipeline's live state, so the bench can SEE which
// stage is acting instead of inferring it from the orientation.
constexpr uint8_t kAhrsStartup = 1u << 0;        // gain still ramping down
constexpr uint8_t kAhrsOverrange = 1u << 1;      // gyroscope range exceeded, recovering
constexpr uint8_t kAhrsAccelIgnored = 1u << 2;   // acceleration rejection acting now
constexpr uint8_t kAhrsMagIgnored = 1u << 3;     // magnetic rejection acting now
constexpr uint8_t kAhrsBiasAtRest = 1u << 4;     // bias tracker sees the ball at rest
constexpr uint8_t kAhrsBiasAdapting = 1u << 5;   // ... and is refining the estimate
constexpr uint8_t kAhrsMagCalibrated = 1u << 6;  // a hard/soft-iron calibration is loaded
constexpr uint8_t kAhrsMagCapture = 1u << 7;     // raw magnetometer capture streaming

// Quaternion scale: int16 = round(float * 16384) (Q14).
constexpr float kQuatScale = 16384.0f;

struct __attribute__((packed)) ImuPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampUs;
  int16_t quatW, quatX, quatY, quatZ;
  int16_t accX, accY, accZ;  // mg (1 g = 1000 mg), converted from the sensor counts
  uint8_t flags;
  // Appended in v8 (packet grew 23 -> 31 B), shrunk to 4 B in protocol v2 (27 B): the
  // microphone envelope, pooled over the audioWindowMs window (TLV 0x2F, default
  // 50 ms) that ended with this orientation sample. [0] = left, [1] = right.
  //
  // WHY it rides on the orientation packet instead of its own: at 100-200 Hz the cost
  // of a BLE notification is dominated by the packet slot, not by its length (a 27-byte
  // notification occupies 732 us of air time against 700 us for a 23-byte one), and the
  // connection-event budget is the scarce resource. One packet per tick also means the
  // envelope and the orientation always share a timestamp. Valid only when
  // kImuAudioValid is set; otherwise all four bytes read 0 (the -120 dBFS floor).
  uint8_t audioRmsHalfDb[2];   // AES17 dBFS, 0.5 dB steps: dBFS = value / 2 - 120
  uint8_t audioPeakHalfDb[2];  // peak dBFS, same scale (0 dBFS = full-scale sample)
};

struct __attribute__((packed)) Co2Packet {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint16_t ppm;
  uint8_t flags;
  uint8_t ageS;
};

struct __attribute__((packed)) DiagPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t uptimeS;
  uint16_t imuHzX10, notifyHzX10, fifoOverflows, i2sErrors, uartFrameErrors, freeHeapKb,
      minFreeHeapKb;
  uint8_t flags;
  // Appended in v7 (packet grew 23 -> 32 B): the IMU pipeline's own state. Without it
  // the acceleration/magnetic rejection and the bias tracker are invisible from the
  // outside, and a bench run cannot tell "working" from "never triggered".
  uint8_t ahrsFlags;
  uint8_t accelErrDeg;        // inclination disagreement, 0..90 deg (saturating)
  uint8_t magErrDeg;          // heading disagreement, 0..90 deg (saturating)
  int16_t gyroBiasX100[3];    // estimated zero-rate offset, 0.01 dps per LSB
  // Appended in v8 (32 -> 34 B): the connection interval the central actually granted
  // after the firmware's request, in 0.1 ms per LSB (0 = not connected / unknown).
  // Without it there is no way to tell a granted 7.5 ms interval from a refused one,
  // and that single number is what caps every notify rate above ~60 Hz.
  uint16_t connItvlMsX10;
};

// Raw magnetometer capture, notified on the diag characteristic while a calibration
// run is active (command 0x15 / 0x16). The ellipsoid fit that turns these samples into
// a hard-iron offset and a soft-iron matrix runs in the config-app: the firmware would
// need hundreds of samples in RAM and a least-squares solve to do it itself, and the
// browser already has both.
// Samples are int16 in 0.1 uT (+-3276.7 uT, saturating), x/y/z interleaved, already
// remapped to the gyro/accel chip frame.
constexpr uint8_t kMagChunkMaxSamples = 16;

struct __attribute__((packed)) MagChunkPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint8_t count;  // valid samples, 0..kMagChunkMaxSamples
  uint8_t flags;
  int16_t samples[3 * kMagChunkMaxSamples];
};

// Live sensor levels for the config-app charts, notified on the diag characteristic
// at 5 Hz. RMS values are in 24-bit sample counts (INMP441 data, sample >> 8; full
// scale 2^23): dBFS = 20*log10(rms*sqrt(2)/2^23). audioThreshold is the breath
// detector's effective trigger level, in counts of the ACTIVE metric (flags bit 4):
// RMS counts (AES17 scale above) or peak counts (dBFS = 20*log10(peak/2^23)). Absolute
// detector: the line itself; adaptive (flags bit 5): the slow baseline plus the
// threshold. potRaw is the EMA-filtered 12-bit ADC reading (0..4095).
struct __attribute__((packed)) SensorsPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint32_t audioRmsL, audioRmsR;
  uint32_t audioThreshold;
  uint16_t potRaw;
  uint8_t flags;
};

// Decimated RAW audio for the config-app's spectrum analyzer (the FFT runs in the
// frontend). Mono mix (L+R)/2, DC-removed, 16 kHz -> 2 kHz by average-of-8, int16
// (top 16 bits of the 24-bit samples). 96 samples = 48 ms per packet (~21 pkt/s,
// ~4.3 kB/s (208 B × 20.8/s)). hpHz/lpHz echo the configured band-pass so the UI can draw the
// cutoffs without a config read. Needs MTU >= 211.
struct __attribute__((packed)) AudioChunkPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint16_t sampleRateHz;  // 2000
  uint8_t count;          // 96
  uint8_t flags;
  uint16_t hpHz, lpHz;
  int16_t samples[96];
};

// Globe-navigation pose, notified on its own characteristic (19b10006-...). mode: 0 off,
// 1 mirror, 2 joystick. Position is degrees x 1e7 (lat -90..90, lon (-180,180]); bearing
// is the Mapbox convention (degrees clockwise from north of the screen-up direction) x 100
// in (-180,180]; speed is the globe angular speed in deg/s x 10. anchorSeq counts the
// anchors (command 0x17) applied since boot, wrapping at 255. Needs MTU >= 26 (23-byte
// payload + 3-byte ATT header).
struct __attribute__((packed)) NavPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampUs;
  uint8_t mode;
  uint8_t flags;
  int32_t latE7, lonE7;
  int16_t bearingX100;
  uint16_t speedDegSX10;
  uint8_t anchorSeq;
};

static_assert(sizeof(ImuPacket) == 27, "ImuPacket size");
static_assert(sizeof(Co2Packet) == 12, "Co2Packet size");
static_assert(sizeof(DiagPacket) == 34, "DiagPacket size");
static_assert(sizeof(SensorsPacket) == 23, "SensorsPacket size");
static_assert(sizeof(AudioChunkPacket) == 208, "AudioChunkPacket size");
static_assert(sizeof(MagChunkPacket) == 106, "MagChunkPacket size");
static_assert(sizeof(NavPacket) == 23, "NavPacket size");

}  // namespace gaia

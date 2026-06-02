// test_native/test_protocol.cpp
// Native unit tests — runs on host machine (no MCU needed)
// Tests: protocol constants, CRC32, motion detection algorithm

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>

// Include the shared protocol header
#include "protocol.h"

// ============================================================
// CRC32 — copied from storage.h for host-side testing
// ============================================================
static uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

// ============================================================
// Motion Detection — extracted algorithm for testing
// ============================================================
struct MotionResult {
    float score;
    bool  detected;
    int   changedPixels;
};

static MotionResult computeMotion(
    const uint8_t *refFrame, const uint8_t *curFrame,
    int totalPixels, int pixelThreshold, int percentThreshold
) {
    MotionResult r = {0.0f, false, 0};
    for (int i = 0; i < totalPixels; i++) {
        int diff = abs((int)curFrame[i] - (int)refFrame[i]);
        if (diff > pixelThreshold) r.changedPixels++;
    }
    r.score = (float)r.changedPixels * 100.0f / (float)totalPixels;
    r.detected = (r.score >= (float)percentThreshold);
    return r;
}

// Apply EMA update (same as firmware: 95% old + 5% new)
static void emaUpdate(uint8_t *refFrame, const uint8_t *curFrame, int totalPixels) {
    for (int i = 0; i < totalPixels; i++) {
        refFrame[i] = (uint8_t)((refFrame[i] * 243 + curFrame[i] * 13) >> 8);
    }
}

// ============================================================
// Test Cases
// ============================================================

// --- Protocol Constants ---

void test_protocol_version(void) {
    TEST_ASSERT_EQUAL_STRING("2.0.0", FW_VERSION);
}

void test_protocol_network_config(void) {
    TEST_ASSERT_EQUAL_STRING("GigaSenseHub", AP_SSID);
    TEST_ASSERT_EQUAL(80, STREAM_PORT);
    TEST_ASSERT_EQUAL(15, TARGET_FPS);
}

void test_protocol_frame_dimensions(void) {
    TEST_ASSERT_EQUAL(320, FRAME_WIDTH);
    TEST_ASSERT_EQUAL(240, FRAME_HEIGHT);
    TEST_ASSERT_EQUAL(80, MOTION_WIDTH);
    TEST_ASSERT_EQUAL(60, MOTION_HEIGHT);
    // Downscale ratio should be 4x
    TEST_ASSERT_EQUAL(4, FRAME_WIDTH / MOTION_WIDTH);
    TEST_ASSERT_EQUAL(4, FRAME_HEIGHT / MOTION_HEIGHT);
}

void test_protocol_motion_defaults(void) {
    TEST_ASSERT_EQUAL(15, MOTION_THRESHOLD_DEFAULT);
    TEST_ASSERT_EQUAL(5, MOTION_PERCENT_DEFAULT);
}

void test_protocol_max_nodes(void) {
    TEST_ASSERT_EQUAL(2, MAX_NODES);
}

void test_protocol_power_states(void) {
    TEST_ASSERT_EQUAL(0, POWER_STATE_ACTIVE);
    TEST_ASSERT_EQUAL(1, POWER_STATE_LIGHT);
    TEST_ASSERT_EQUAL(2, POWER_STATE_DEEP);
    // Light sleep threshold < deep sleep threshold
    TEST_ASSERT_TRUE(IDLE_LIGHT_SLEEP_MS < IDLE_DEEP_SLEEP_MS);
}

void test_protocol_security(void) {
    TEST_ASSERT_EQUAL_STRING("X-GSH-Auth", AUTH_HEADER);
    TEST_ASSERT_EQUAL_STRING("X-GSH-Nonce", AUTH_NONCE_HEADER);
    // HMAC key must not be empty
    TEST_ASSERT_TRUE(strlen(HMAC_SECRET_KEY) > 0);
}

void test_protocol_endpoints(void) {
    TEST_ASSERT_EQUAL_STRING("/stream", STREAM_PATH);
    TEST_ASSERT_EQUAL_STRING("/motion", MOTION_PATH);
    TEST_ASSERT_EQUAL_STRING("/info", NODE_INFO_PATH);
    TEST_ASSERT_EQUAL_STRING("/ota/status", OTA_STATUS_PATH);
    TEST_ASSERT_EQUAL_STRING("/ota/upload", OTA_UPLOAD_PATH);
}

// --- CRC32 ---

void test_crc32_known_value(void) {
    // CRC32 of "123456789" = 0xCBF43926 (standard test vector)
    const uint8_t data[] = "123456789";
    uint32_t crc = crc32_calc(data, 9);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc);
}

void test_crc32_empty(void) {
    uint32_t crc = crc32_calc(nullptr, 0);
    TEST_ASSERT_EQUAL_HEX32(0x00000000, crc);
}

void test_crc32_single_byte(void) {
    uint8_t data = 0x00;
    uint32_t crc = crc32_calc(&data, 1);
    TEST_ASSERT_NOT_EQUAL(0, crc);
}

void test_crc32_deterministic(void) {
    const uint8_t data[] = "GigaSenseHub";
    uint32_t crc1 = crc32_calc(data, 12);
    uint32_t crc2 = crc32_calc(data, 12);
    TEST_ASSERT_EQUAL_HEX32(crc1, crc2);
}

// --- Motion Detection Algorithm ---

void test_motion_no_change(void) {
    const int pixels = MOTION_WIDTH * MOTION_HEIGHT;
    uint8_t *ref = (uint8_t *)calloc(pixels, 1);
    uint8_t *cur = (uint8_t *)calloc(pixels, 1);
    // Fill both with same value
    memset(ref, 128, pixels);
    memset(cur, 128, pixels);

    MotionResult r = computeMotion(ref, cur, pixels, MOTION_THRESHOLD_DEFAULT, MOTION_PERCENT_DEFAULT);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.score);
    TEST_ASSERT_FALSE(r.detected);
    TEST_ASSERT_EQUAL(0, r.changedPixels);

    free(ref);
    free(cur);
}

void test_motion_full_change(void) {
    const int pixels = MOTION_WIDTH * MOTION_HEIGHT;
    uint8_t *ref = (uint8_t *)calloc(pixels, 1);
    uint8_t *cur = (uint8_t *)calloc(pixels, 1);
    memset(ref, 0, pixels);
    memset(cur, 255, pixels);

    MotionResult r = computeMotion(ref, cur, pixels, MOTION_THRESHOLD_DEFAULT, MOTION_PERCENT_DEFAULT);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, r.score);
    TEST_ASSERT_TRUE(r.detected);
    TEST_ASSERT_EQUAL(pixels, r.changedPixels);

    free(ref);
    free(cur);
}

void test_motion_below_threshold(void) {
    const int pixels = MOTION_WIDTH * MOTION_HEIGHT;
    uint8_t *ref = (uint8_t *)calloc(pixels, 1);
    uint8_t *cur = (uint8_t *)calloc(pixels, 1);
    memset(ref, 100, pixels);
    // Change only 1% of pixels (below 5% threshold)
    memset(cur, 100, pixels);
    int changeCount = pixels / 100;  // 1%
    for (int i = 0; i < changeCount; i++) {
        cur[i] = 200;  // Big change but few pixels
    }

    MotionResult r = computeMotion(ref, cur, pixels, MOTION_THRESHOLD_DEFAULT, MOTION_PERCENT_DEFAULT);
    TEST_ASSERT_TRUE(r.score < 5.0f);
    TEST_ASSERT_FALSE(r.detected);

    free(ref);
    free(cur);
}

void test_motion_above_threshold(void) {
    const int pixels = MOTION_WIDTH * MOTION_HEIGHT;
    uint8_t *ref = (uint8_t *)calloc(pixels, 1);
    uint8_t *cur = (uint8_t *)calloc(pixels, 1);
    memset(ref, 100, pixels);
    // Change 10% of pixels (above 5% threshold)
    memset(cur, 100, pixels);
    int changeCount = pixels / 10;  // 10%
    for (int i = 0; i < changeCount; i++) {
        cur[i] = 200;
    }

    MotionResult r = computeMotion(ref, cur, pixels, MOTION_THRESHOLD_DEFAULT, MOTION_PERCENT_DEFAULT);
    TEST_ASSERT_TRUE(r.score >= 5.0f);
    TEST_ASSERT_TRUE(r.detected);

    free(ref);
    free(cur);
}

void test_motion_pixel_threshold(void) {
    const int pixels = MOTION_WIDTH * MOTION_HEIGHT;
    uint8_t *ref = (uint8_t *)calloc(pixels, 1);
    uint8_t *cur = (uint8_t *)calloc(pixels, 1);
    memset(ref, 100, pixels);
    // Change ALL pixels by 10 (below pixel threshold of 15)
    memset(cur, 110, pixels);

    MotionResult r = computeMotion(ref, cur, pixels, MOTION_THRESHOLD_DEFAULT, MOTION_PERCENT_DEFAULT);
    TEST_ASSERT_EQUAL(0, r.changedPixels);
    TEST_ASSERT_FALSE(r.detected);

    free(ref);
    free(cur);
}

void test_motion_ema_update(void) {
    const int pixels = 10;
    uint8_t ref[10], cur[10];
    memset(ref, 100, pixels);
    memset(cur, 200, pixels);

    emaUpdate(ref, cur, pixels);

    // After EMA: (100 * 243 + 200 * 13) >> 8 = (24300 + 2600) >> 8 = 26900 >> 8 = 105
    for (int i = 0; i < pixels; i++) {
        TEST_ASSERT_EQUAL(105, ref[i]);
    }
}

// ============================================================
// Test Runner
// ============================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Protocol constants
    RUN_TEST(test_protocol_version);
    RUN_TEST(test_protocol_network_config);
    RUN_TEST(test_protocol_frame_dimensions);
    RUN_TEST(test_protocol_motion_defaults);
    RUN_TEST(test_protocol_max_nodes);
    RUN_TEST(test_protocol_power_states);
    RUN_TEST(test_protocol_security);
    RUN_TEST(test_protocol_endpoints);

    // CRC32
    RUN_TEST(test_crc32_known_value);
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_single_byte);
    RUN_TEST(test_crc32_deterministic);

    // Motion detection
    RUN_TEST(test_motion_no_change);
    RUN_TEST(test_motion_full_change);
    RUN_TEST(test_motion_below_threshold);
    RUN_TEST(test_motion_above_threshold);
    RUN_TEST(test_motion_pixel_threshold);
    RUN_TEST(test_motion_ema_update);

    return UNITY_END();
}

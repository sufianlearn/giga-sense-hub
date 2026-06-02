#ifndef STORAGE_H
#define STORAGE_H

// ---------------------------------------------------------------------------
//  storage.h  –  Persistent settings for Arduino GIGA R1 WiFi (STM32H747)
// ---------------------------------------------------------------------------
//
//  Flash layout (STM32H747 – 2 MB internal flash)
//  ================================================
//
//  Bank 1: 0x0800_0000 – 0x080F_FFFF  (1 MB, 8 × 128 KB sectors)
//          Contains bootloader + firmware – DO NOT TOUCH.
//
//  Bank 2: 0x0810_0000 – 0x081F_FFFF  (1 MB, 8 × 128 KB sectors)
//          Sector 0: 0x0810_0000   (128 KB)
//          Sector 1: 0x0812_0000   (128 KB)
//          ...
//          Sector 7: 0x081E_0000   (128 KB)  ← we use this one
//
//  We store a single PersistentSettings struct at the very start of sector 7
//  of bank 2 (0x081E_0000).  The sector is 128 KB; our struct is tiny, so
//  the rest of the sector is unused padding (0xFF after erase).
//
//  Write procedure:  erase entire 128 KB sector → program sizeof(struct).
//  Read procedure :  read sizeof(struct), validate magic + CRC32.
//
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include "FlashIAP.h"
#include "mbedtls/sha256.h"

// ---- Flash geometry -------------------------------------------------------
#define STORAGE_FLASH_ADDR   0x081E0000UL   // Bank 2, sector 7
#define STORAGE_SECTOR_SIZE  (128U * 1024U) // 128 KB

// ---- Magic & version ------------------------------------------------------
#define SETTINGS_MAGIC       0x47494741UL   // "GIGA" in ASCII (little-endian)
#define SETTINGS_VERSION     2U

// ---------------------------------------------------------------------------
//  Settings struct – stored verbatim in flash
// ---------------------------------------------------------------------------
struct PersistentSettings {
    uint32_t magic;              // Must equal SETTINGS_MAGIC
    uint32_t version;            // Struct version for future migration
    char     pin[5];             // 4-digit PIN + null terminator (DEPRECATED — kept for migration)
    uint8_t  pinHash[32];        // SHA-256 hash of the PIN (replaces plaintext)
    int      autoLockSecs;       // Auto-lock timeout in seconds
    int      motionThreshold;    // Motion detection sensitivity (0-100)
    int      targetFps;          // Camera target frame rate
    int      jpegQuality;        // JPEG quality (1-63 for OV767x style)
    bool     doorLocked[3];      // Lock state for up to 3 doors
    uint32_t crc32;              // CRC-32 over all preceding fields
};

// ---------------------------------------------------------------------------
//  Module-internal state (static so each TU gets its own copy, but this
//  header is typically included from exactly one .ino / .cpp)
// ---------------------------------------------------------------------------
namespace _storage_detail {

// Simple CRC-32 (ISO 3309 / ITU-T V.42 polynomial, no lookup table).
// Covers bytes [0 .. len-1].
inline uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

// Compute CRC over every field *except* the trailing crc32 member.
inline uint32_t settingsCrc(const PersistentSettings &s) {
    const size_t payload = offsetof(PersistentSettings, crc32);
    return crc32_calc(reinterpret_cast<const uint8_t *>(&s), payload);
}

static mbed::FlashIAP flash;
static bool           flashReady = false;
static PersistentSettings currentSettings;

} // namespace _storage_detail

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

// Compute SHA-256 hash of a null-terminated PIN string
inline void hashPin(const char *pin, uint8_t outHash[32]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char *>(pin), strlen(pin));
    mbedtls_sha256_finish(&ctx, outHash);
    mbedtls_sha256_free(&ctx);
}

// Compare a plaintext PIN against a stored hash
inline bool verifyPin(const char *pin, const uint8_t storedHash[32]) {
    uint8_t inputHash[32];
    hashPin(pin, inputHash);
    return memcmp(inputHash, storedHash, 32) == 0;
}

// Fill a struct with safe compile-time defaults.
inline void storageDefaults(PersistentSettings &s) {
    memset(&s, 0, sizeof(s));
    s.magic           = SETTINGS_MAGIC;
    s.version         = SETTINGS_VERSION;
    strncpy(s.pin, "", sizeof(s.pin));  // Clear plaintext (deprecated)
    s.pin[0]          = '\0';
    hashPin("1234", s.pinHash);         // Default PIN hashed
    s.autoLockSecs    = 120;
    s.motionThreshold = 50;
    s.targetFps       = 15;
    s.jpegQuality     = 12;
    s.doorLocked[0]   = true;
    s.doorLocked[1]   = true;
    s.doorLocked[2]   = true;
    s.crc32           = _storage_detail::settingsCrc(s);
}

// Read settings from flash.  Returns true if data was valid.
inline bool storageLoad(PersistentSettings &s) {
    using namespace _storage_detail;

    if (!flashReady) {
        flash.init();
        flashReady = true;
    }

    int rc = flash.read(&s, STORAGE_FLASH_ADDR, sizeof(PersistentSettings));
    if (rc != 0) {
        storageDefaults(s);
        return false;
    }

    // Validate magic, version, and CRC
    if (s.magic != SETTINGS_MAGIC || s.version != SETTINGS_VERSION) {
        storageDefaults(s);
        return false;
    }

    uint32_t expected = settingsCrc(s);
    if (s.crc32 != expected) {
        storageDefaults(s);
        return false;
    }

    return true;
}

// Erase the settings sector and write a new struct.  Returns true on success.
inline bool storageSave(const PersistentSettings &s) {
    using namespace _storage_detail;

    if (!flashReady) {
        flash.init();
        flashReady = true;
    }

    // Erase the entire 128 KB sector first (FlashIAP requires erase before
    // program; erased bytes read as 0xFF).
    int rc = flash.erase(STORAGE_FLASH_ADDR, STORAGE_SECTOR_SIZE);
    if (rc != 0) return false;

    // Program the struct at the sector start.  FlashIAP may require the
    // size to be a multiple of the program-page size.  On STM32H7 the
    // minimum program size is 32 bytes (flash word = 256 bits).  We round
    // up to the next multiple.
    uint32_t page = flash.get_page_size();
    if (page == 0) page = 32;  // fallback
    size_t writeLen = sizeof(PersistentSettings);
    size_t remainder = writeLen % page;
    if (remainder != 0) {
        writeLen += page - remainder;
    }

    // Prepare a page-aligned buffer (pad with 0xFF so unused bytes match
    // the erased state).
    uint8_t buf[256];  // 256 >= any realistic padded size for our struct
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &s, sizeof(PersistentSettings));

    rc = flash.program(buf, STORAGE_FLASH_ADDR, writeLen);
    return (rc == 0);
}

// Convenience: initialise FlashIAP, attempt to load settings from flash,
// fall back to defaults on first-ever boot or after corruption.
// After this call, _storage_detail::currentSettings is valid.
inline void storageInit() {
    using namespace _storage_detail;

    flash.init();
    flashReady = true;

    if (!storageLoad(currentSettings)) {
        // First boot or corrupt data – write clean defaults into flash so
        // the next boot finds them immediately.
        storageDefaults(currentSettings);
        storageSave(currentSettings);
    }
}

// Quick accessor – returns a reference to the RAM-cached settings that
// storageInit() populated.  Callers can modify this, then call
//   storageSave(storageSettings());
// to persist changes.
inline PersistentSettings &storageSettings() {
    return _storage_detail::currentSettings;
}

#endif // STORAGE_H

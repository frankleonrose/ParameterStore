#include "Arduino.h"

#if defined(PS_LOGGING_OVERRIDE)
  // Developer has #defined their own PS_LOG_* elsewhere
  #if !defined(PS_LOG_INFO) || !defined(PS_LOG_ERROR) || !defined(PS_LOG_DEBUG)
    #error PS_LOGGING_OVERRIDE defined but missing definition of PS_LOG_INFO, PS_LOG_ERROR, or PS_LOG_DEBUG
  #endif
#elif defined(LOGGING_ARDUINO)
  #include "Logging.h"
  #define PS_LOG_INFO(...)   Log.Info(__VA_ARGS__)
  #define PS_LOG_ERROR(...)  Log.Error(__VA_ARGS__)
  #define PS_LOG_DEBUG(...)  Log.Debug(__VA_ARGS__)
#elif defined(LOGGING_SERIAL)
  #define PS_LOG_INFO(...)   Serial.printf(__VA_ARGS__)
  #define PS_LOG_ERROR(...)  Serial.printf(__VA_ARGS__)
  #define PS_LOG_DEBUG(...)  Serial.printf(__VA_ARGS__)
#elif defined(LOGGING_PRINTF)
  #define PS_LOG_INFO(...)   printf(__VA_ARGS__)
  #define PS_LOG_ERROR(...)  printf(__VA_ARGS__)
  #define PS_LOG_DEBUG(...)  printf(__VA_ARGS__)
#elif defined(LOGGING_DISABLED)
  // Silently compile logging to no-ops
  #define PS_LOG_INFO(...)
  #define PS_LOG_ERROR(...)
  #define PS_LOG_DEBUG(...)
#else
  #warning No logging option specified: LOGGING_ARDUINO, LOGGING_SERIAL, LOGGING_PRINTF, LOGGING_DISABLED, or PS_LOGGING_OVERRIDE
  #define PS_LOG_INFO(...)
  #define PS_LOG_ERROR(...)
  #define PS_LOG_DEBUG(...)
#endif

#if defined(PLATFORM_NATIVE)
#include <cstdint>
#include <cstring>  // memset
#include <string.h> // memcpy
#include <cstdio>  // memset

// Stub Arduino string in program text macro
#define F(s) (s)
#define CR "\r\n"

// Log.Info(__VA_ARGS__)
#define LOG_INFO(...)   printf(__VA_ARGS__)
// Log.Error(__VA_ARGS__)
#define LOG_ERROR(...)  printf(__VA_ARGS__)
// Log.Debug(__VA_ARGS__)
#define LOG_DEBUG(...)  printf(__VA_ARGS__)
// #include <cstdlib>
#if !defined(ASSERT)
#define ASSERT(x) if (!(x)) { PS_LOG_ERROR("Assertion failure: " #x ); }
#endif

#include "NonVolatileStore.h"
// #include "AdafruitBLEStore.h"
// #include "AdafruitFramI2CStore.h"
// #include "AdafruitFramSPIStore.h"

#define PS_INSUFFICIENT_SPACE -2
#define PS_ERROR_NOT_FOUND -1
#define PS_SUCCESS 0

struct HeaderTag;

class ParameterStore {
  NonVolatileStore &_store;
  const uint16_t _size;
public:
  ParameterStore(NonVolatileStore &store);
  bool begin();
  int set(const char *key, const uint8_t *buffer, const uint16_t size);
  int set(const char *key, const char *str);
  int set(const char *key, const uint32_t value);

  int get(const char *key, uint8_t *buffer, const uint16_t size) const;
  int get(const char *key, char *str, uint16_t size) const;
  int get(const char *key, uint32_t *value) const;
private:
  bool recoverPlan(const struct HeaderTag &header);
  uint16_t findFreeSpace(uint16_t unitSize, uint16_t *foundSize) const;
  uint16_t findKey(const uint16_t start, const char *key, const bool checkSize, const uint16_t size) const;
};

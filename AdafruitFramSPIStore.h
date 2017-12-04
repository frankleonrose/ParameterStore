// This class depends on the Adafruit FRAM SPI library
// available here: https://github.com/adafruit/Adafruit_FRAM_SPI
// Before including "AdafruitFramSPIStore.h", your sketch should #include "Adafruit_FRAM_SPI.h"

#if !defined(_PS_ADAFRUIT_FRAM_SPI_STORE_H_)
#define _PS_ADAFRUIT_FRAM_SPI_STORE_H_

#include "NonVolatileStore.h"

class AdafruitFramSPIStore : public NonVolatileStore {
  Adafruit_FRAM_SPI &_fram;
  const uint16_t _offset;

public:
  AdafruitFramSPIStore(Adafruit_FRAM_SPI &fram, uint16_t size, uint16_t offset = 0)
    : NonVolatileStore(size), _fram(fram), _offset(offset) {
  }

  bool begin() {
    if (!_fram.begin()) {
      return false;
    }
    return NonVolatileStore::begin();
  }
protected:
  virtual void readImpl(uint16_t offset, void *addr, uint16_t size) const {
    _fram.read(_offset + offset, (uint8_t *)addr, size);
  }
  virtual void writeImpl(uint16_t offset, const void *bytes, uint16_t size) {
    _fram.writeEnable(true);
    _fram.write(_offset + offset, (uint8_t *)bytes, size);
    _fram.writeEnable(false);
  }
};
#endif
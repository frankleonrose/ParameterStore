#ifndef RAMSTORE_H
#define RAMSTORE_H

#include "NonVolatileStore.h"

template <uint16_t Size>
class RamStore : public NonVolatileStore {
  uint8_t _bytes[Size];
  mutable uint32_t lastOffset = 50000;
  mutable uint32_t count = 0;
  uint32_t _byteWriteCount = 0;

public:
  RamStore()
    : NonVolatileStore(Size) {
  }

  virtual bool begin() {
    return NonVolatileStore::begin();
  }

  virtual void resetStore() {
    NonVolatileStore::resetStore();
  }
protected:
  virtual void readImpl(uint16_t offset, void *buf, uint16_t size) const {
    // PS_LOG_DEBUG(F("readImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
    PS_ASSERT_MSG(count<10, "Reading same offset over and over");
    PS_ASSERT_MSG(offset<Size, "readImpl offset should be within Size");
    PS_ASSERT_MSG((offset+size)<=Size, "readImpl offset+size should be within Size");
    memcpy(buf, _bytes + offset, size);
    // dumpBytes((uint8_t *)buf, size);
  }
  virtual void writeImpl(uint16_t offset, const void *buf, uint16_t size) {
    // PS_LOG_DEBUG(F("Write count %d with fail at %d" CR), _byteWriteCount, _failAfter);
    PS_ASSERT_MSG(offset<Size, "writeImpl offset should be within Size");
    PS_ASSERT_MSG((offset+size)<=Size, "writeImpl offset+size should be within Size");
    // PS_LOG_DEBUG(F("writeImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
    memcpy(_bytes + offset, buf, size);
    // dumpBytes((uint8_t *)buf, size);
  }
};

#endif
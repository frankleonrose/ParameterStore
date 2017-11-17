// #include "Logging.h"
#include <arpa/inet.h>

#if !defined(MIN)
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

class NonVolatileStore {
  const uint16_t _size; // Allocated size (usable space = allocated - sizeof(magic value))
  const uint16_t dataOffset;
public:
  virtual bool begin() {
    if (!isMagicSet()) {
      LOG_INFO(F("Did not find magic number! Clearing storage." CR));
      resetStore();
    }
    return true;
  }
protected:
  NonVolatileStore(uint16_t allocatedSize)
    : _size(allocatedSize),
      dataOffset(sizeof(uint32_t)) {
  }
  bool isMagicSet() {
    #define MAGIC_NUMBER 0xFADE0042
    uint32_t magic_value = 0;
    // Magic number is in first 4 bytes of store.
    readImpl(0, (uint8_t *)&magic_value, sizeof(magic_value));
    magic_value = ntohl(magic_value);
    // LOG_DEBUG(F("Read magic number %x" CR), magic_value);
    return (magic_value==MAGIC_NUMBER);
  }
  virtual void readImpl(uint16_t offset, void *addr, uint16_t size) const =  0;
  virtual void writeImpl(uint16_t offset, const void *bytes, uint16_t size) = 0;
public:
  uint16_t size() const { return _size - dataOffset; } // Returns usable size
  uint8_t readbyte(const uint16_t offset) const {
    ASSERT((dataOffset + offset)<this->_size);
    uint8_t byte;
    readImpl(dataOffset + offset, &byte, sizeof(byte));
    return byte;
  }
  uint32_t readu32(const uint16_t offset) const {
    uint32_t value = 0;
    ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    readImpl(dataOffset + offset, (uint8_t *)&value, sizeof(value));
    return ntohl(value);
  }
  uint16_t readu16(const uint16_t offset) const {
    uint16_t value = 0;
    ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    readImpl(dataOffset + offset, (uint8_t *)&value, sizeof(value));
    return ntohs(value);
  }
  void read(const uint16_t offset, void *addr, const uint16_t size) const {
    ASSERT((dataOffset + offset + size)<=this->_size);
    readImpl(dataOffset + offset, addr, size);
  }
  void writebyte(const uint16_t offset, const uint8_t byte) {
    writeImpl(dataOffset + offset, &byte, sizeof(byte));
  }
  void write(const uint16_t offset, const void *addr, const uint16_t size) {
    ASSERT((dataOffset + offset + size)<=this->_size);
    writeImpl(dataOffset + offset, addr, size);
  }
  void writeu16(const uint16_t offset, const uint16_t value) {
    ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    uint16_t writable = htons(value);
    writeImpl(dataOffset + offset, (uint8_t *)&writable, sizeof(value));
  }
  void writeu32(const uint16_t offset, const uint32_t value) {
    ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    uint32_t writable = htonl(value);
    writeImpl(dataOffset + offset, (uint8_t *)&writable, sizeof(value));
  }
  virtual void resetStore() {
    uint8_t zeroes[100];
    memset(zeroes, 0, sizeof(zeroes));
    for (uint16_t off = 0; off < this->_size; off += sizeof(zeroes)) {
      writeImpl(off, zeroes, MIN(sizeof(zeroes), (this->_size - off)));
    }
    uint32_t magic = htonl(MAGIC_NUMBER);
    writeImpl(0, &magic, sizeof(magic)); // Need to use writeImpl to write at actual 0 offset
  }
};

#ifndef NONVOLATILESTORE_H
#define NONVOLATILESTORE_H

#include "Arduino.h"

#if !defined(MIN)
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#if BYTE_ORDER == BIG_ENDIAN
#if !defined(htons)
#define htons(x) (x)
#endif
#if !defined(ntohs)
#define ntohs(x) (x)
#endif
#if !defined(htonl)
#define htonl(x) (x)
#endif
#if !defined(ntohl)
#define ntohl(x) (x)
#endif
#else
#if !defined(htons)
#define htons(x) ( ((x)<< 8 & 0xFF00) + \
                   ((x)>> 8 & 0x00FF) )
#endif
#if !defined(ntohs)
#define ntohs(x) htons(x)
#endif
#if !defined(htonl)
#define htonl(x) ( ((x)<<24 & 0xFF000000UL) + \
                   ((x)<< 8 & 0x00FF0000UL) + \
                   ((x)>> 8 & 0x0000FF00UL) + \
                   ((x)>>24 & 0x000000FFUL) )
#endif
#if !defined(ntohl)
#define ntohl(x) htonl(x)
#endif
#endif

class NonVolatileStore {
  const uint16_t _size; // Allocated size (usable space = allocated - sizeof(magic value))
  const uint16_t dataOffset;
public:
  virtual bool begin() {
    if (!isMagicSet()) {
      PS_LOG_INFO(F("Did not find magic number! Clearing storage." CR));
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
    // PS_LOG_DEBUG(F("Read magic number %x" CR), magic_value);
    return (magic_value==MAGIC_NUMBER);
  }
  virtual void readImpl(uint16_t offset, void *addr, uint16_t size) const =  0;
  virtual void writeImpl(uint16_t offset, const void *bytes, uint16_t size) = 0;
public:
  uint16_t size() const { return _size - dataOffset; } // Returns usable size
  uint8_t readbyte(const uint16_t offset) const {
    PS_ASSERT((dataOffset + offset)<this->_size);
    uint8_t byte;
    readImpl(dataOffset + offset, &byte, sizeof(byte));
    return byte;
  }
  uint32_t readu32(const uint16_t offset) const {
    uint32_t value = 0;
    PS_ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    readImpl(dataOffset + offset, (uint8_t *)&value, sizeof(value));
    return ntohl(value);
  }
  uint16_t readu16(const uint16_t offset) const {
    uint16_t value = 0;
    PS_ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    readImpl(dataOffset + offset, (uint8_t *)&value, sizeof(value));
    return ntohs(value);
  }
  void read(const uint16_t offset, void *addr, const uint16_t size) const {
    PS_ASSERT((dataOffset + offset + size)<=this->_size);
    readImpl(dataOffset + offset, addr, size);
  }
  void writebyte(const uint16_t offset, const uint8_t byte) {
    writeImpl(dataOffset + offset, &byte, sizeof(byte));
  }
  void write(const uint16_t offset, const void *addr, const uint16_t size) {
    PS_ASSERT((dataOffset + offset + size)<=this->_size);
    writeImpl(dataOffset + offset, addr, size);
  }
  void writeu16(const uint16_t offset, const uint16_t value) {
    PS_ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    uint16_t writable = htons(value);
    writeImpl(dataOffset + offset, (uint8_t *)&writable, sizeof(value));
  }
  void writeu32(const uint16_t offset, const uint32_t value) {
    PS_ASSERT((dataOffset + offset + sizeof(value))<=this->_size);
    uint32_t writable = htonl(value);
    writeImpl(dataOffset + offset, (uint8_t *)&writable, sizeof(value));
  }
  virtual void resetStore() {
    uint8_t zeroes[100];
    memset(zeroes, 0, sizeof(zeroes));
    for (uint16_t off = 0; off < this->_size; off += sizeof(zeroes)) {
      writeImpl(off, zeroes, MIN(sizeof(zeroes), (unsigned)(this->_size - off)));
    }
    uint32_t magic = htonl(MAGIC_NUMBER);
    writeImpl(0, &magic, sizeof(magic)); // Need to use writeImpl to write at actual 0 offset
  }
};

#endif

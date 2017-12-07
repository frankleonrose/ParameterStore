#include "ParameterStore.h"

/*
 * Format:
 * HEADER
 *  4  MAGIC           Everything else is valid
 *  2  FORMAT-VERSION  What is layout of store
 *  2  SIZE            Size of store
 *  8  PLAN            OFFSET/LENGTH/WRITE-CRC/PLAN-CRC where we plan to write.
 *                     If PLAN-CRC is correct, plan is valid.
 *                     If WRITE-CRC matches at location OFFSET+LENGTH,
 *                     that means we wrote successfully.
 *                     Otherwise, we restore that location to free space.
 * ENTRIES
 *  2 SIZE             If free space, actual bytes to next entry.
 *                     If occupied, content size.
 *  8 KEY              Free space is indicated with \0 first char of key.
 *                     Otherwise 'name' followed by 0 or more \0 to fill 8 bytes.
 *  N CONTENT
 *  P PADDING          Extra bytes such that (N+P) % UNIT == 0
 *  4 CRC
 */

const uint16_t FORMAT = 1;
const unsigned int UNIT = 4;
const unsigned int KEYSIZE = 8;
const unsigned int CRCSIZE = sizeof(uint32_t);
const uint32_t CRCSEED = 0xA5A5;

typedef enum FlagTag {
  FlagFree = 0,
  FlagSet = 1,
  FlagFreed = 2, // Interpret size like FlagSet, but entry is free
} FlagType;

// Round up to unit size
static uint16_t unitSize(const uint16_t size) {
  const uint16_t mod = size % UNIT;
  return size + (mod==0 ? 0 : UNIT - mod);
}

uint32_t calcCrc(const uint32_t seed, const uint8_t *buffer, const uint16_t size) {
  // Simple crc
  uint32_t crc = seed;
  for (int i=0; i<size; ++i) {
    crc ^= buffer[i];
    crc <<= 4;
    crc ^= crc >> 24;
  }
  return crc;
}

struct __attribute__ ((packed)) PlanTag {
  uint8_t flag;
  uint8_t unused;
  uint16_t offset;
  uint16_t size;
  uint32_t entry_crc;
  struct {
    uint16_t _size;
    union {
      uint8_t _flag;
      uint16_t _transaction;
    } _status;
  } restore;
  uint32_t plan_crc;

  uint16_t getOffset() const { return ntohs(offset); }
  uint16_t getSize() const { return ntohs(size); }
  uint32_t getEntryCrc() const { return ntohl(entry_crc); }
  void setOffset(uint16_t offset) { this->offset = htons(offset); }
  void setSize(uint16_t size) { this->size = htons(size); }
  void setEntryCrc(uint32_t crc) { this->entry_crc = htonl(crc); }
  uint32_t calcCrc() const {
    return ::calcCrc(CRCSEED, (uint8_t *)this, sizeof(PlanTag)-sizeof(plan_crc));
  }
  void setCrc() {
    plan_crc = htonl(calcCrc());
  }
  bool isCrcValid() const {
    return ntohl(plan_crc)==calcCrc();
  }
  bool isEmpty() const {
    return flag==FlagFree || !isCrcValid();
  }
};
static_assert(18==sizeof(struct PlanTag), "Plan expected to be 18 bytes");

typedef struct HeaderTag {
  uint16_t format;
  uint16_t size;
  struct PlanTag plan;
} Header;
static_assert(22==sizeof(struct HeaderTag), "Header expected to be 22 bytes");

typedef struct EntryTag {
  uint16_t _size;
  union {
    uint8_t _flag;
    uint16_t _transaction;
  } _status;
  char _name[KEYSIZE];

  EntryTag() {
    _size = htons(0);
    _status._flag = FlagFree;
    memset(_name, 0, sizeof(_name));
  }
  EntryTag(uint16_t size) {
    _size = htons(size);
    _status._flag = FlagFree;
    memset(_name, 0, sizeof(_name));
  }
  EntryTag(uint16_t size, const char *key) {
    _size = htons(size);
    _status._transaction = htons(0);
    strncpy(_name, key, sizeof(_name)); // Pads with 0's to width
  }
  uint16_t getSize() const {
    return ntohs(_size);
  }
  bool isFree() const {
    return _status._flag==FlagFree || _status._flag==FlagFreed;
  }
  uint16_t totalBytes() const {
    if (_status._flag==FlagFree) {
      return getSize();
    }
    else {
      // Allocated or once allocated.
      return sizeof(EntryTag) + unitSize(getSize()) + CRCSIZE;
    }
  }
  uint32_t calcCrc() const {
    return ::calcCrc(CRCSEED, (uint8_t *)this, sizeof(EntryTag));
  }
  uint32_t calcCrc(const uint8_t *buffer, const uint16_t size) const {
    uint32_t crc = calcCrc();
    return ::calcCrc(crc, buffer, size);
  }
  static bool readAndCheckCrc(uint32_t matchCrc, NonVolatileStore &store, const uint16_t offset, const uint16_t size, char *key) {
    uint8_t buffer[200];
    uint16_t dataSize = sizeof(EntryTag) + unitSize(size);
    PS_ASSERT(dataSize<=sizeof(buffer));
    store.read(offset, buffer, dataSize);
    EntryTag entry; // Used for sizing.
    strncpy(key, (char *)(buffer + sizeof(entry._size) + sizeof(entry._status)), KEYSIZE);
    uint32_t dataCrc = ::calcCrc(CRCSEED, buffer, sizeof(EntryTag) + size);
    uint32_t readCrc = store.readu32(offset + dataSize);
    return matchCrc==dataCrc && matchCrc==readCrc;
  }
  static void writeFree(NonVolatileStore &store, const uint16_t offset, const uint16_t size) {
    EntryTag entry(size);
    PS_ASSERT(entry._status._flag==FlagFree);
    // Write five bytes size+transaction plus initial name byte '\0' indicating free
    // PS_LOG_DEBUG(F("Writing free to %d with size %d" CR), offset, size);
    store.write(offset, &entry, sizeof(entry._size) + sizeof(entry._status));
  }
  void write(NonVolatileStore &store, const uint16_t offset, const uint8_t *buffer, const uint32_t crc) {
    _status._flag = FlagSet;
    store.write(offset, this, sizeof(*this));
    store.write(offset + sizeof(*this), (void *)buffer, ntohs(_size));
    store.writeu32(offset + sizeof(EntryTag) + unitSize(ntohs(_size)), crc);
  }
} Entry;

static_assert (12==sizeof(Entry), "Entry expected to be 12 bytes");
#define OFFSET(struc, field) (((uint8_t *)&struc.field) - ((uint8_t *)&struc))

ParameterStore::ParameterStore(NonVolatileStore &store)
  : _store(store), _size(unitSize(store.size()))
{
}

bool ParameterStore::begin() {
  PS_ASSERT(sizeof(Header)<_size);
  bool ok = _store.begin();
  if (!ok) {
    PS_LOG_ERROR(F("Underlying store failed begin()" CR));
    return false;
  }
  Header header;
  _store.read(0, &header, sizeof(header));
  uint16_t format = ntohs(header.format);
  if (format==0) {
    // Store was just reset...start from scratch
    PS_LOG_DEBUG(F("Initializing store with format %d and size %d" CR), FORMAT, _size);
    _store.writeu16(OFFSET(header, size), _size);
    Entry::writeFree(_store, sizeof(Header), _size - sizeof(Header));
    // Write format last...if it succeeds, we have valid header
    _store.writeu16(OFFSET(header, format), FORMAT);
  }
  else if (format!=FORMAT) {
    PS_LOG_ERROR(F("Unrecognized store format: %d (0x%x)" CR), format, format);
    return false;
  }
  else {
    uint16_t size = ntohs(header.size);
    if (size!=_size) {
      PS_LOG_ERROR(F("Unknown size requested %d vs store %d" CR), size, _size);
      return false;
    }
  }
  return recoverPlan(header);
}

bool ParameterStore::recoverPlan(const Header &header) {
  // PS_LOG_DEBUG(F("Header plan flag %d" CR), header.plan.flag);

  // If plan invalid or marked used, ignore it.
  if (header.plan.isEmpty()) {
    // PS_LOG_DEBUG(F("No recovery necessary" CR));
    return true;
  }

  // We need to do some work because the last operation was interrupted and left the plan in place
  if (header.plan.flag==FlagSet) {
    // PS_LOG_DEBUG(F("Recovering from interrupted set" CR));
    // We were trying to write. Make sure that the write was completed successfully.
    char key[KEYSIZE];
    if (Entry::readAndCheckCrc(header.plan.getEntryCrc(), _store, header.plan.getOffset(), header.plan.getSize(), key)) {
      // If so, check whether there is another entry that should have been overwritten.
      uint16_t found = findKey(0, key, false, 0);
      if (found==header.plan.getOffset()) {
        // We found current, keep looking beyond found offset...
        found = findKey(found+1, key, false, 0);
      }
      if (found<_size) {
        // Mark that one free.
        Entry entry;
        _store.writebyte(found + OFFSET(entry, _status._flag), FlagFreed);
      }
    }
    else {
      // If not successful write, restore to what it was.
      _store.write(header.plan.getOffset(), &header.plan.restore, sizeof(header.plan.restore));
    }
    // Then mark plan empty.
    _store.writebyte(OFFSET(header, plan.flag), FlagFree);
  }
  else {
    PS_LOG_ERROR(F("Recovery unimplemented" CR));
    return false;
  }
  return true;
}

uint16_t ParameterStore::findFreeSpace(uint16_t neededSize, uint16_t *foundSize /* Hack to return foundSize */) const {
  uint16_t offset = sizeof(Header);
  // Walk through entries looking for free one that is big enough...
  while (offset<_size) {
    Entry entry;
    _store.read(offset, &entry, sizeof(entry._size) + sizeof(entry._status));
    uint16_t size = entry.totalBytes();

    if (entry.isFree() && neededSize<=size) {
      // TODO: Maybe keep going to look for a smaller free entry that works...
      if (foundSize) {
        *foundSize = size;
      }
      break;
    }
    else {
      offset += entry.totalBytes();
    }
  }
  // PS_LOG_DEBUG(F("Free space search for %d responds %d (of %d)" CR), neededSize, offset, _size);
  return offset; // Will be == _size when not found
}

uint16_t ParameterStore::findKey(const uint16_t start, const char *key, const bool checkSize, const uint16_t pSize) const {
  char match[KEYSIZE];
  strncpy(match, key, sizeof(match));
  // PS_LOG_DEBUG(F("Looking for key %s %s size %d" CR), key, (checkSize ? "checking" : "not checking"), pSize);

  uint16_t offset = sizeof(Header);
  // Walk through entries looking for matching key...
  while (offset<_size) {
    Entry entry;
    _store.read(offset, &entry, sizeof(entry));
    const uint16_t size = entry.getSize();
    //PS_LOG_DEBUG(F("Read entry at %d size %d key '%s'" CR), offset, size, entry._name);
    if (offset>=start && !entry.isFree() && 0==memcmp(entry._name, match, sizeof(match))) {
      if (checkSize && size!=pSize) {
        offset = _size; // Indicate not found
      }
      break;
    }
    else {
      offset += entry.totalBytes();
    }
  }
  //PS_LOG_DEBUG(F("Key search for '%s' responds %d (of %d)" CR), key, offset, _size);
  return offset;
}

int ParameterStore::set(const char *key, const uint8_t *buffer, const uint16_t size) {
  const uint16_t prior = findKey(0, key, false /* don't check size */, size);
  const bool existing = (prior < _size);

  const uint16_t length = sizeof(Entry) + unitSize(size) + CRCSIZE;

  // Find free space for storage
  uint16_t foundSize = 0;
  const uint16_t offset = findFreeSpace(length, &foundSize);
  if (offset>=_size) {
    return PS_INSUFFICIENT_SPACE;
  }

  // Write the entry that splits the free space, if necessary.
  const uint16_t extra = foundSize - length;
  if (extra>0) {
    Entry::writeFree(_store, offset+length, extra);
  }

  Entry entry(size, key);
  uint32_t crc = entry.calcCrc(buffer, size);

  // Write the intention to write offset/length/crc/logcrc to log
  Header header;
  header.plan.flag = FlagSet;
  header.plan.unused = 0;
  header.plan.setOffset(offset);
  header.plan.setSize(size);
  header.plan.setEntryCrc(crc);
  // In case of error, need to be able to restore this size/flag we're about to overwrite
  _store.read(offset, &header.plan.restore, sizeof(header.plan.restore));
  header.plan.setCrc();
  // Write all but initial flag.
  _store.write(OFFSET(header, plan.unused), &header.plan.unused, sizeof(header.plan) - 1);
  // Once plan is written, add flag byte.
  _store.writebyte(OFFSET(header, plan), header.plan.flag);

  // Write length, key, buffer, and CRC
  // PS_LOG_DEBUG(F("Set entry for %s responds %d for %d" CR), key, offset, size);
  entry.write(_store, offset, buffer, crc);

  // Remove prior value
  if (existing) {
    _store.writebyte(prior + OFFSET(entry, _status._flag), FlagFreed);
  }

  // Lastly, write 0 in plan length to indicate completion
  _store.writebyte(OFFSET(header, plan.flag), FlagFree);

  return PS_SUCCESS;
}
int ParameterStore::set(const char *key, const char *str) {
  return PS_SUCCESS;
}
int ParameterStore::set(const char *key, const uint32_t value) {
  uint32_t storeValue = htonl(value);
  return set(key, (const uint8_t *)&storeValue, sizeof(storeValue));
}
int ParameterStore::get(const char *key, uint8_t *buffer, const uint16_t size) const {
  uint16_t offset = findKey(0, key, true, size);
  if (offset>=_size) {
    return PS_ERROR_NOT_FOUND;
  }

  _store.read(offset + sizeof(Entry), buffer, size);
  return PS_SUCCESS;
}
int ParameterStore::get(const char *key, char *str, uint16_t size) const {
  return PS_SUCCESS;
}
int ParameterStore::get(const char *key, uint32_t *value) const {
  uint32_t storeValue = 0;
  int ret = get(key, (uint8_t *)&storeValue, sizeof(storeValue));
  if (ret==PS_SUCCESS) {
    *value = ntohl(storeValue);
  }
  return ret;
}

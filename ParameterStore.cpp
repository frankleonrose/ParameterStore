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

// Round up to unit size
static uint16_t unitSize(const uint16_t size) {
  const uint16_t mod = size % UNIT;
  return size + (mod==0 ? 0 : UNIT - mod);
}

typedef struct HeaderTag {
  uint16_t format;
  uint16_t size;
  struct PlanTag {
    uint16_t offset;
    uint16_t size;
    uint32_t param_crc;
    uint32_t plan_crc;
  } plan;
} Header;

typedef struct EntryTag {
  uint16_t _size;
  union {
    uint8_t _flag;
    uint16_t _transaction;
  } _status;
  char _name[KEYSIZE];

  EntryTag() {
    _size = htons(0);
    _status._flag = 0; // Free
    memset(_name, 0, sizeof(_name));
  }
  EntryTag(uint16_t size) {
    _size = htons(size);
    _status._flag = 0; // Free
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
    return _status._flag==0 || _status._flag==1;
  }
  uint16_t totalBytes() const {
    if (_status._flag==0) {
      return getSize();
    }
    else {
      // Allocated or once allocated.
      return sizeof(EntryTag) + unitSize(getSize()) + CRCSIZE;
    }
  }
  static void writeFree(NonVolatileStore &store, uint16_t offset, uint16_t size) {
    EntryTag entry(size);
    ASSERT(entry._status._flag==0);
    // Write five bytes size+transaction plus initial name byte '\0' indicating free
    LOG_DEBUG(F("Writing free to %d with size %d" CR), offset, size);
    store.write(offset, &entry, sizeof(entry._size) + sizeof(entry._status));
  }
  void write(NonVolatileStore &store, uint16_t offset, const uint8_t *buffer, uint32_t crc) {
    _status._flag = 2;
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
  ASSERT(sizeof(Header)<_size);
  bool ok = _store.begin();
  if (!ok) {
    LOG_ERROR(F("Underlying store failed begin()" CR));
    return false;
  }
  Header header;
  _store.read(0, &header, sizeof(header));
  uint16_t format = ntohs(header.format);
  if (format==0) {
    // Store was just reset...start from scratch
    LOG_DEBUG(F("Initializing store with format %d and size %d" CR), FORMAT, _size);
    _store.writeu16(OFFSET(header, size), _size);
    Entry::writeFree(_store, sizeof(Header), _size - sizeof(Header));
    // Write format last...if it succeeds, we have valid header
    _store.writeu16(OFFSET(header, format), FORMAT);
  }
  else if (format!=FORMAT) {
    LOG_ERROR(F("Unrecognized store format: %d (0x%x)" CR), format, format);
    return false;
  }
  else {
    uint16_t size = ntohs(header.size);
    if (size!=_size) {
      LOG_ERROR(F("Unknown size requested %d vs store %d" CR), size, _size);
      return false;
    }
  }
  return recoverPlan(header);
}

bool ParameterStore::recoverPlan(const Header &header) {
  // If plan invalid, ignore. Len==0 or PLAN-CRC invalid.
  uint16_t size = ntohs(header.plan.size);
  if (size==0) {
    // No plan has size 0
    return true;
  }
  LOG_ERROR(F("Unimplemented plan handler" CR));
  return false;
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
  LOG_DEBUG(F("Free space search for %d responds %d (of %d)" CR), neededSize, offset, _size);
  return offset; // Will be == _size when not found
}

uint16_t ParameterStore::findKey(const char *key, bool checkSize, uint16_t pSize) const {
  char match[KEYSIZE];
  strncpy(match, key, sizeof(match));
  LOG_DEBUG(F("Looking for key %s %s size %d" CR), key, (checkSize ? "checking" : "not checking"), pSize);

  uint16_t offset = sizeof(Header);
  // Walk through entries looking for matching key...
  while (offset<_size) {
    Entry entry;
    _store.read(offset, &entry, sizeof(entry));
    const uint16_t size = entry.getSize();
    LOG_DEBUG(F("Read entry at %d size %d key '%s'" CR), offset, size, entry._name);
    if (!entry.isFree() && 0==memcmp(entry._name, match, sizeof(match))) {
      if (checkSize && size!=pSize) {
        offset = _size; // Indicate not found
      }
      break;
    }
    else {
      offset += entry.totalBytes();
    }
  }
  LOG_DEBUG(F("Key search for '%s' responds %d (of %d)" CR), key, offset, _size);
  return offset;
}

int ParameterStore::set(const char *key, const uint8_t *buffer, const uint16_t size) {
  const uint16_t prior = findKey(key, false /* don't check size */, size);
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

  // Find latest version of key
  // If key will roll over,

  uint32_t crc = 0; // TODO calc

  // Write the intention to write offset/length/crc/logcrc to log
  Header header;
  header.plan.offset = htons(offset);
  header.plan.size = htons(size); // TODO what about 0 length writes?
  header.plan.param_crc = htonl(crc);
  header.plan.plan_crc = htonl(0); // TODO
  _store.write(OFFSET(header, plan), &header.plan, sizeof(header.plan));

  // Write length, key, buffer, and CRC
  LOG_DEBUG(F("Set entry for %s responds %d for %d" CR), key, offset, size);
  Entry entry(size, key);
  entry.write(_store, offset, buffer, crc);

  // Remove prior value
  if (existing) {
    _store.write(prior + OFFSET(entry, _status._flag), "\1", 1);
  }

  // Lastly, write 0 in plan length to indicate completion
  _store.writeu16(OFFSET(header, plan.size), 0);

  return PS_SUCCESS;
}
int ParameterStore::set(const char *key, const char *str) {
  return PS_SUCCESS;
}
int ParameterStore::set(const char *key, const uint32_t value) {
  return PS_SUCCESS;
}
int ParameterStore::get(const char *key, uint8_t *buffer, const uint16_t size) const {
  uint16_t offset = findKey(key, true, size);
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
  return PS_SUCCESS;
}

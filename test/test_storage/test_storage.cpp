#include <unity.h>

#ifdef UNIT_TEST

#include <cstdlib> // rand
#include "ParameterStore.h"

void dumpBytes(const uint8_t *buffer, const uint16_t size) {
  for (int i=0; i<size; ++i) {
    PS_LOG_DEBUG(F("%02x "), buffer[i]);
  }
  PS_LOG_DEBUG(F("" CR));
}

template <uint16_t Size>
class TestStore : public NonVolatileStore {
  uint8_t _bytes[Size];
  mutable uint32_t lastOffset = 50000;
  mutable uint32_t count = 0;
  uint32_t _failAfter = 0; // 0 means don't, otherwise don't write any more after nth byte
  // mutable uint32_t _operationCount = 0;
  uint32_t _byteWriteCount = 0;

public:
  TestStore()
    : NonVolatileStore(Size) {
  }

  void setFailAfterWritingBytes(uint32_t failAfter) {
    _failAfter = failAfter;
    _byteWriteCount = 0;
  }

  uint32_t getBytesWritten() {
    return _byteWriteCount;
  }

  virtual bool begin() {
    return NonVolatileStore::begin();
  }

  virtual void resetStore() {
    NonVolatileStore::resetStore();
    setFailAfterWritingBytes(0); // Default is no failures
  }
protected:
  virtual void readImpl(uint16_t offset, void *buf, uint16_t size) const {
    // PS_LOG_DEBUG(F("readImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
    if (offset!=lastOffset) {
      count = 0;
      lastOffset = offset;
    }
    else {
      ++count;
    }
    if (count>=10) {
      PS_LOG_DEBUG(F("Reading from same offset repeatedly: %d size %d %d times" CR), offset, size, count);
    }
    TEST_ASSERT_TRUE_MESSAGE(count<10, "Reading same offset over and over");
    TEST_ASSERT_TRUE_MESSAGE(offset<Size, "readImpl offset should be within Size");
    TEST_ASSERT_TRUE_MESSAGE((offset+size)<=Size, "readImpl offset+size should be within Size");
    memcpy(buf, _bytes + offset, size);
    // dumpBytes((uint8_t *)buf, size);
  }
  virtual void writeImpl(uint16_t offset, const void *buf, uint16_t size) {
    // PS_LOG_DEBUG(F("Write count %d with fail at %d" CR), _byteWriteCount, _failAfter);
    TEST_ASSERT_TRUE_MESSAGE(offset<Size, "writeImpl offset should be within Size");
    TEST_ASSERT_TRUE_MESSAGE((offset+size)<=Size, "writeImpl offset+size should be within Size");
    if (_failAfter) {
      if (_byteWriteCount<_failAfter) {
        uint16_t goodWrite = MIN(size, _failAfter - _byteWriteCount);
        memcpy(_bytes + offset, buf, goodWrite); // Write up to the failure byte
        if (goodWrite<size) {
          // PS_LOG_DEBUG(F("Abbreviated write: %d of %d at offset %d" CR), goodWrite, size, offset-sizeof(uint32_t));
        }
        else {
          // PS_LOG_DEBUG(F("writeImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
        }
      }
      else {
        // PS_LOG_DEBUG(F("Skipped writeImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
      }
    }
    else {
      // PS_LOG_DEBUG(F("writeImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
      memcpy(_bytes + offset, buf, size);
    }
    // dumpBytes((uint8_t *)buf, size);
    _byteWriteCount += size;
  }
};

const int STORE_SIZE = 2000;
TestStore<STORE_SIZE> testStore;
ParameterStore paramStore(testStore);

void setUp(void) {
  testStore.resetStore();
  bool begin = paramStore.begin();
  TEST_ASSERT_TRUE_MESSAGE(begin, "Must start successfully")
}

// void tearDown(void) {
// // clean stuff up here
// }

void test_fetch_absent_value(void) {
  uint8_t buf[100];
  int res = paramStore.get("named", buf, sizeof(buf));
  TEST_ASSERT_EQUAL(PS_ERROR_NOT_FOUND, res);
}

void test_fetch_present_value(void) {
  const char *s = "Hello, World!";
  uint16_t storeSize = strlen(s)+1;
  int res = paramStore.set("named", (uint8_t *)s, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);

  char buf[100];
  res = paramStore.get("named", (uint8_t *)buf, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);
  TEST_ASSERT_EQUAL_STRING(s, buf);
}

void test_fetch_two_values(void) {
  const char *s = "Hello, World!";
  uint16_t storeSize = strlen(s)+1;
  int res = paramStore.set("named1", (uint8_t *)s, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);
  res = paramStore.set("named2", (uint8_t *)s, storeSize/2);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);

  char buf[100];
  res = paramStore.get("named1", (uint8_t *)buf, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);
  TEST_ASSERT_EQUAL_STRING(s, buf);
  res = paramStore.get("named2", (uint8_t *)buf, storeSize/2);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);
  TEST_ASSERT_EQUAL_STRING_LEN(s, buf, storeSize/2);
}

void test_overwrite(void) {
  const char *key = "exists";
  const char *s1 = "Hello, World!";
  uint16_t storeSize = strlen(s1)+1;
  int res = paramStore.set(key, (uint8_t *)s1, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);

  char buf[100];
  res = paramStore.get(key, (uint8_t *)buf, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);
  TEST_ASSERT_EQUAL_STRING(s1, buf);

  const char *s2 = "Hell, whirled";
  TEST_ASSERT_EQUAL(strlen(s1), strlen(s2));
  res = paramStore.set(key, (uint8_t *)s2, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);

  res = paramStore.get(key, (uint8_t *)buf, storeSize);
  TEST_ASSERT_EQUAL(PS_SUCCESS, res);
  TEST_ASSERT_EQUAL_STRING_LEN(s2, buf, storeSize);
}

const uint16_t CYCLES = 100;

class Datum {
  protected:
  char _name[8];
  Datum(const char *name) {
    strncpy(_name, name, sizeof(_name));
  }

  public:
  virtual ~Datum() {}
  virtual Datum *clone() const = 0;
  virtual Datum *randomize() = 0;
  virtual bool store(ParameterStore &store) const = 0;
  virtual bool check(const ParameterStore &store) const = 0;
};

class DatumBytes : public Datum {
  const uint16_t _size;
  uint8_t _bytes[200];
  public:
  DatumBytes(const char *name, const uint8_t *bytes, const uint16_t size)
  : Datum(name), _size(size) {
    ASSERT(size <= sizeof(_bytes));
    memcpy(_bytes, bytes, size);
  }
  virtual Datum *clone() const {
    return new DatumBytes(_name, _bytes, _size);
  }
  virtual Datum *randomize() {
    for (uint16_t i=0; i<_size; ++i) {
      _bytes[i] = rand() % 256;
    }
    return this;
  }
  static DatumBytes *make(const char *name) {
    uint8_t bytes[200];
    const uint16_t size = 1 + rand() % 4;
    DatumBytes *d = new DatumBytes(name, bytes, size);
    d->randomize();
    return d;
  }
  virtual bool store(ParameterStore &store) const {
    //PS_LOG_DEBUG(F("Storing %s with size %d" CR), _name, _size);
    return PS_SUCCESS==store.set(_name, _bytes, _size);
  }
  virtual bool check(const ParameterStore &store) const {
    // PS_LOG_DEBUG(F("Checking %s with size %d" CR), _name, _size);
    uint8_t buffer[_size];
    TEST_ASSERT_TRUE_MESSAGE(_size<=sizeof(buffer), "Buffer should be big enough for size");
    int ok = store.get(_name, buffer, _size);
    if (PS_SUCCESS!=ok) {
      PS_LOG_DEBUG(F("Failed to read" CR));
      return false;
    }
    // dumpBytes(buffer, _size);
    // dumpBytes(_bytes, _size);
    return memcmp(_bytes, buffer, _size)==0;
  }
};

#define ELEMENTS(x) (sizeof(x) / sizeof((x)[0]))

void makeTestEntries(ParameterStore &paramStore, Datum **data, int countData) {
  for (int i=0; i<countData; ++i) {
    char name[10];
    sprintf(name, "name%03d", i);
    Datum *datum;
    switch (rand() % 3) {
      case 0: {datum = DatumBytes::make(name); break;}
      case 1: {datum = DatumBytes::make(name); break;}
      case 2: {datum = DatumBytes::make(name); break;}
    }
    bool ok = datum->store(paramStore);
    TEST_ASSERT_TRUE_MESSAGE(ok, "Stored new value successfully");
    TEST_ASSERT_TRUE_MESSAGE(datum->check(paramStore), "Check value just stored");
    data[i] = datum;
  }
}

void test_multiple_writes(void) {
  Datum *data[20];
  makeTestEntries(paramStore, data, ELEMENTS(data));

  for (int i=0; i<CYCLES; ++i) {
    int di = rand() % ELEMENTS(data);
    Datum *d = data[di];
    TEST_ASSERT_TRUE_MESSAGE(d->check(paramStore), "Check value stored last time");
    d->randomize();
    bool ok = d->store(paramStore);
    TEST_ASSERT_TRUE_MESSAGE(ok, "Stored new value successfully");
    TEST_ASSERT_TRUE_MESSAGE(d->check(paramStore), "Confirmed value just stored");
  }
}

void test_multiple_writes_with_error(void) {
  PS_LOG_DEBUG(F("Initializing byteStore/paramStore" CR));
  TestStore<STORE_SIZE> byteStore;
  ParameterStore paramStore(byteStore);
  bool ok = paramStore.begin();
  TEST_ASSERT_TRUE_MESSAGE(ok, "Began failStore");

  Datum *data[20];
  makeTestEntries(paramStore, data, ELEMENTS(data));

  PS_LOG_DEBUG(F("Starting test cycles" CR));
  for (int i=0; i<CYCLES; ++i) {
    int di = rand() % ELEMENTS(data);
    Datum *d = data[di];
    TEST_ASSERT_TRUE_MESSAGE(d->check(paramStore), "Check value stored last time");

    // Change value....
    Datum *last = d;
    data[di] = d = d->clone()->randomize();

    // Update datum in store. Figure out how many total bytes were written.
    TestStore<2000> prechangeStore = byteStore;
    ok = d->store(paramStore);
    TEST_ASSERT_TRUE_MESSAGE(ok, "Stored new value successfully");
    uint16_t bytesWritten = byteStore.getBytesWritten() - prechangeStore.getBytesWritten();
    // PS_LOG_DEBUG(F("Bytes written: %d" CR), bytesWritten);

    // Repeat update with failure at every single byte written. Ensure that either old or new value is readable.
    bool newValue = false; // Once new value is written, all subsequent writes should also write successfully.
    for (int i = 1; i<bytesWritten; ++i) {
      // PS_LOG_DEBUG(F("Writing limited bytes to: %d" CR), i);
      TestStore<2000> testStore = prechangeStore;

      ParameterStore failStore(testStore);
      ok = failStore.begin();
      TEST_ASSERT_TRUE_MESSAGE(ok, "Began failStore");
      testStore.setFailAfterWritingBytes(i);
      d->store(failStore); // After i bytes, nothing more is written (simulating power failure on device)

      // Power up device with same store.
      testStore.setFailAfterWritingBytes(0); // Disable failures
      ParameterStore recoverStore(testStore);
      ok = recoverStore.begin();
      TEST_ASSERT_TRUE_MESSAGE(ok, "Began recoverStore");

      if (newValue) {
        TEST_ASSERT_TRUE_MESSAGE(d->check(recoverStore), "Stored new value successfully");
      }
      else if (d->check(recoverStore)) {
        // First time with new value
        TEST_ASSERT_FALSE_MESSAGE(last->check(recoverStore), "Last value no longer accessible");
        newValue = true;
      }
      else {
        // At first, only last value is accessible...
        TEST_ASSERT_TRUE_MESSAGE(last->check(recoverStore), "Last value accessible");
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(newValue, "Should have finished wih new value accessible");
  }
}

// void test_led_state_high(void) {
//     digitalWrite(LED_BUILTIN, HIGH);
//     TEST_ASSERT_EQUAL(digitalRead(LED_BUILTIN), HIGH);
// }

// void test_led_state_low(void) {
//     digitalWrite(LED_BUILTIN, LOW);
//     TEST_ASSERT_EQUAL(digitalRead(LED_BUILTIN), LOW);
// }

extern "C"
int main(int argc, char **argv) {
    UNITY_BEGIN();    // IMPORTANT LINE!

    RUN_TEST(test_fetch_absent_value);
    RUN_TEST(test_fetch_present_value);
    RUN_TEST(test_fetch_two_values);
    RUN_TEST(test_overwrite);
    RUN_TEST(test_multiple_writes);
    RUN_TEST(test_multiple_writes_with_error);

    // setup();

    // while (1) {
    //     loop();
    // }
    UNITY_END(); // stop unit testing
    return 0;
}

#endif
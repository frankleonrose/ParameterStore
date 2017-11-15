#include <unity.h>

#ifdef UNIT_TEST

#include <cstdlib> // rand
#include "ParameterStore.h"

template <uint16_t Size>
class TestStore : public NonVolatileStore {
  uint8_t _bytes[Size];
  mutable uint32_t lastOffset = 50000;
  mutable uint32_t count = 0;

public:
  TestStore()
    : NonVolatileStore(Size) {
  }

  virtual bool begin() {
    return NonVolatileStore::begin();
  }
protected:
  virtual uint8_t readbyteImpl(uint16_t offset) const {
    TEST_ASSERT_TRUE_MESSAGE(offset<Size, "readbyteImpl offset should be within Size");
    return _bytes[offset];
  }
  virtual void readImpl(uint16_t offset, void *buf, uint16_t size) const {
    LOG_DEBUG(F("readImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
    if (offset!=lastOffset) {
      count = 0;
      lastOffset = offset;
    }
    else {
      ++count;
    }
    TEST_ASSERT_TRUE_MESSAGE(count<5, "Reading same offset over and over");
    TEST_ASSERT_TRUE_MESSAGE(offset<Size, "readImpl offset should be within Size");
    TEST_ASSERT_TRUE_MESSAGE((offset+size)<=Size, "readImpl offset+size should be within Size");
    memcpy(buf, _bytes + offset, size);
  }
  virtual void writebyteImpl(uint16_t offset, uint8_t value) {
    TEST_ASSERT_TRUE_MESSAGE(offset<Size, "readImpl offset should be within Size");
    _bytes[offset] = value;
  }
  virtual void writeImpl(uint16_t offset, const void *buf, uint16_t size) {
    LOG_DEBUG(F("writeImpl offset %d size %d" CR), offset-sizeof(uint32_t), size);
    TEST_ASSERT_TRUE_MESSAGE(offset<Size, "writeImpl offset should be within Size");
    TEST_ASSERT_TRUE_MESSAGE((offset+size)<=Size, "writeImpl offset+size should be within Size");
    memcpy(_bytes + offset, buf, size);
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

void dumpBytes(const uint8_t *buffer, const uint16_t size) {
  for (int i=0; i<size; ++i) {
    LOG_DEBUG(F("%02x "), buffer[i]);
  }
  LOG_DEBUG(F("" CR));
}

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
    LOG_DEBUG(F("Storing %s with size %d" CR), _name, _size);
    return PS_SUCCESS==store.set(_name, _bytes, _size);
  }
  virtual bool check(const ParameterStore &store) const {
    LOG_DEBUG(F("Checking %s with size %d" CR), _name, _size);
    uint8_t buffer[_size];
    TEST_ASSERT_TRUE_MESSAGE(_size<=sizeof(buffer), "Buffer should be big enough for size");
    int ok = store.get(_name, buffer, _size);
    if (PS_SUCCESS!=ok) {
      LOG_DEBUG(F("Failed to read" CR));
      return false;
    }
    dumpBytes(buffer, _size);
    dumpBytes(_bytes, _size);
    return memcmp(_bytes, buffer, _size)==0;
  }
};

class DatumWithHistory {
  Datum *_datum;
  Datum *_last;
  public:
    DatumWithHistory(Datum *datum)
    : _datum(datum), _last(NULL) {
    }
    void alter() {
      _last = _datum;
      _datum = _datum->clone()->randomize();
    }
    bool store(ParameterStore &store) {
      return _datum->store(store);
    }
    bool check(ParameterStore &store) {
      return (_datum->check(store) || (_last!=NULL && _last->check(store)));
    }
};

#define ELEMENTS(x) (sizeof(x) / sizeof((x)[0]))

void test_multiple_writes(void) {
  Datum *data[20];
  for (int i=0; i<ELEMENTS(data); ++i) {
    char name[20];
    sprintf(name, "name%03d", i);
    Datum *datum;
    switch (rand() % 3) {
      case 0: {datum = DatumBytes::make(name); break;}
      case 1: {datum = DatumBytes::make(name); break;}
      case 2: {datum = DatumBytes::make(name); break;}
    }
    data[i] = datum;
    bool ok = datum->store(paramStore);
    TEST_ASSERT_TRUE_MESSAGE(ok, "Stored new value successfully");
    TEST_ASSERT_TRUE_MESSAGE(datum->check(paramStore), "Check value just stored");
  }

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
  TEST_FAIL_MESSAGE("Unimplemented");
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
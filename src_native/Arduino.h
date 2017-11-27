// Mock Arduino.h used when compiling native platform tests.

#include <cstdint>
#include <cstring>
#include <string.h>
#include <cstdio>

#define F(s) (s)

#define LED_BUILTIN 13

#define LOW   0
#define HIGH  1

#define INPUT   0
#define OUTPUT  1

typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;

void delay(uint16_t msec);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

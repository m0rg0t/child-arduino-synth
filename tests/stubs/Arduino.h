#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
using std::min;
using std::max;
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define OUTPUT 1
#define A0 14
#define F(value) (value)
#define PROGMEM
#define pgm_read_ptr(address) (*(address))
#define pgm_read_byte(address) (*(address))
#define constrain(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
namespace fake {
inline uint32_t now = 0;
inline int knob = 0;
inline int pins[20] = {};
inline int modes[20] = {};
inline std::vector<int> audio;
inline void reset() {
    now = 0; knob = 0;
    std::fill(std::begin(pins), std::end(pins), HIGH);
    std::fill(std::begin(modes), std::end(modes), 0);
    audio.clear();
}
}
inline uint32_t millis() { return fake::now; }
inline void delay(uint32_t ms) { fake::now += ms; }
inline int digitalRead(uint8_t pin) { return fake::pins[pin]; }
inline int analogRead(uint8_t) { return fake::knob; }
inline void pinMode(uint8_t pin, int mode) { fake::modes[pin] = mode; }
inline void tone(uint8_t, unsigned int hz) { fake::audio.push_back(hz); }
inline void noTone(uint8_t) { fake::audio.push_back(-1); }
struct FakeSerial {
    void begin(unsigned long) {}
    template<class T> void print(T) {}
    template<class T> void println(T) {}
};
inline FakeSerial Serial;

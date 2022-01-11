#ifndef MY_BUTTON_H
#define MY_BUTTON_H
#include <Arduino.h>
class DetectBtn {

private:
    byte pin;
    byte state;
    byte laststate;
    byte lastReading;
    unsigned long lastDebounceTime = 0;
    unsigned long debounceDelay = 50;

public:
    DetectBtn (byte pin);
    void init();
    void read();
    bool getState();
    bool isFallingEdge();
    bool isRisingEdge();
};
#endif

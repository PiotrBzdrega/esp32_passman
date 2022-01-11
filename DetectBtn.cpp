#include "DetectBtn.h"
DetectBtn::DetectBtn(byte pin) {
    this->pin = pin;
    lastReading = LOW;
    laststate = LOW;
    init();
}
void DetectBtn::init() {
    pinMode(pin, INPUT_PULLUP);
    read();
}
void DetectBtn::read() {
    // You can handle the debounce of the button directly
    // in the class, so you don't have to think about it
    // elsewhere in your code


    //update last state before state change, to be able to recognize edges on the end of the call
    laststate = state;

    byte newReading = digitalRead(pin);

    if (newReading != lastReading) {
        lastDebounceTime = millis();
    }
    if (millis() - lastDebounceTime > debounceDelay) {
        // Update the 'state' attribute only if debounce is checked
        state = newReading;
    }
    lastReading = newReading;
    
}
bool DetectBtn::getState() {
    return state;
}

bool DetectBtn::isRisingEdge() {    
    return (!state && laststate);
}
bool DetectBtn::isFallingEdge() {
    return (state && !laststate);
}

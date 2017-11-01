#include "Blink.h"

TimedBlink::TimedBlink(int pin, int onDelay, int offDelay)
  : _pin(pin), _onDelay(onDelay), _offDelay(offDelay), _state(false), _durationMs(), _beginOperationMs()
{
  
}


TimedBlink::~TimedBlink() {
  off();
}


void TimedBlink::setState(bool state) {
  _state = state;
  digitalWrite(_pin, !_state);
  _lastUpdate = millis();
}


void TimedBlink::loop() {
  if (!_durationMs)
    return;
  
  unsigned long now = millis();
  if (now - _beginOperationMs >= _durationMs) {
    stop();
    return;
  }

  if (_state && now - _lastUpdate >= _onDelay) {
    off();
    return;
  }

  if (!_state && now - _lastUpdate >= _offDelay) {
    on();
    return;
  }
}

void TimedBlink::on() {
  setState(true);
}

void TimedBlink::off() {
  setState(false);
}

void TimedBlink::toggle() {
  setState(!_state);
}

void TimedBlink::blink(unsigned duration) {
  if (duration < 0)
    return;
  
  _beginOperationMs = millis();
  _durationMs = duration;
  _lastUpdate = _beginOperationMs;
}

void TimedBlink::blink(unsigned duration, int onDelay, int offDelay) {
  _onDelay = onDelay;
  _offDelay = offDelay;
  blink(duration);
}

void TimedBlink::stop() {
  _durationMs = 0;
  off();
}

bool TimedBlink::durationExpired() const {
  return millis() > _beginOperationMs + _durationMs;
}



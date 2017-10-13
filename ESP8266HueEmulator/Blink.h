#include <Arduino.h>

class TimedBlink {
  byte _pin;
  volatile boolean _state;
  int _onDelay;
  int _offDelay;
  
  unsigned long _lastUpdate;
  unsigned long _beginOperationMs, _durationMs;

  void setState(bool state);
  
public:
  TimedBlink(int pin, int onDelay, int offDelay);
  ~TimedBlink();

  void loop();
  void on();
  void off();
  void toggle();
  
  void blink(unsigned duration);
  void blink(unsigned duration, int onDelay, int offDelay);
};

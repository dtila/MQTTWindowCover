
void debug(const char * text);

class TimedBlink {
  byte _pin;
  volatile boolean _state;
  int _onDelay;
  int _offDelay;
  
  unsigned long _lastUpdate;
  unsigned long _beginOperationMs, _durationMs;

  void setState(bool state) {
    _state = state;
    digitalWrite(_pin, !_state);
    _lastUpdate = millis();

    char buff[100] = {};
    sprintf(buff, "Setting led state: %d", _state);
    debug(buff);
  }
  
public:
  TimedBlink(int pin, int onDelay, int offDelay)
    : _pin(pin), _onDelay(onDelay), _offDelay(offDelay), _state(false), _durationMs(), _beginOperationMs()
  {
  }

  ~TimedBlink() {
    digitalWrite(_pin, true); // is off
  }

  void loop() {
    if (!_durationMs)
      return;
    
  	unsigned long now = millis();
    if (now - _beginOperationMs > _durationMs) {
      off();
      _durationMs = 0;
      return;
    }
  
    if (_state) {
      if (now - _lastUpdate >= _onDelay) {
        toggle();
        return;
      }
      
      off();
      return;
    }

    if (!_state && now - _lastUpdate >= _offDelay) {
      toggle();
      return;
    }
  }

  void on() {
    setState(true);
  }

  void off() {
    setState(false);
  }

  void toggle() {
    setState(!_state);
  }

  void blink(unsigned duration) {
  	if (duration < 0)
  		return;
  	
  	_beginOperationMs = millis();
  	_durationMs = duration;
    _lastUpdate = _beginOperationMs;
  }

  void blink(unsigned duration, int onDelay, int offDelay) {
    _onDelay = onDelay;
    _offDelay = offDelay;
    blink(duration);
  }
};

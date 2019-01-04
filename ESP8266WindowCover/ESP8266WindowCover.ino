#define LightServiceDebug RSerial

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <RemoteDebug.h>

#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#include <PubSubClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT
#include <Bounce2.h>

#include "secrets.h"
#include "Blink.h"
#include "LightService.h"

// Able to respond to: ON = 0 position, OFF = position 100

const int POWER_RELAY = 1;
const int GOING_UP_RELAY = 2;
const int LED = 13;
const int COVER_OPEN = 100;
const int COVER_CLOSE = 0;
const int FULL_TIME_MS = 19500;
const int MQTT_SEND_STATUS_MS = 3000;
const char * friendly_name = "Bedroom window cover";
const int MAX_HUE = 254;

class CoverHandler;
class Settings;

const int GO_UP_BUTTON = 1;
const int GO_DOWN_BUTTON = 2;
LightServiceClass LightService(friendly_name);

RemoteDebug RSerial;
CoverHandler *coverHandler = nullptr;
Settings *_settings = nullptr;
char buff[0x100];
TimedBlink activity(LED, 100, 200);

//Bounce upButton = Bounce();
//Bounce downButton = Bounce();

WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer httpServer(80);
bool wifiConnected = false;
ESP8266HTTPUpdateServer httpUpdater;


struct Settings {
  bool IsCalibrated;
  byte Position;
  
  const int Size = sizeof(Settings) + 2;
  
  Settings() : _offset() {
    EEPROM.begin(sizeof(Settings) + 2);
     
    Position = EEPROM.read(_offset);
    IsCalibrated = EEPROM.read(_offset + 1);
  }
  
  void save() {
    if (!IsCalibrated)
      return;
      
    if (RSerial.isActive(RSerial.DEBUG)) {
      RSerial.println("Saving EEPROM");
    }

    EEPROM.write(_offset, Position);
    EEPROM.write(_offset + 1, IsCalibrated);
    
    long start = millis();
    while (millis() - start < 1000) {
      if (EEPROM.commit()) {
              
        if (RSerial.isActive(RSerial.INFO)) {
          RSerial.println("EEPROM saved");
        }
    
        return;
      }
    }

    if (RSerial.isActive(RSerial.ERROR)) {
      RSerial.println("EEPROM was not saved because timeout occured");
    }
  }

  private:
  short _offset;
  int generateOffset() {
    return millis() % 512;
  }
};

class CoverHandler : public LightHandler {
    int _startingPosition, _relayState;
    HueLightInfo _currentInfo;
    unsigned long _operationStartMs, _operationMs, _lastMqttSendMs;

    void _setRelay(int relayState) {
      Serial.flush();
      Serial.write(0xA0);
      Serial.write(0x04);
      Serial.write(relayState);
      Serial.write(0xA1);
      Serial.flush();

      _relayState = relayState;
    }
    
    void setRelay(int relayState) {
      _setRelay(0);

      if (relayState & POWER_RELAY) {
        delay(20);
        _setRelay(relayState & GOING_UP_RELAY ? 1 : 2);
      }

      if (RSerial.isActive(RSerial.DEBUG)) {
        RSerial.printf("Setting relay: %d", relayState);
        RSerial.println();
      }
    }

    int getPositionLenght(unsigned ms) {
      return (COVER_OPEN * ms) / FULL_TIME_MS;
    }

  public:
    CoverHandler() : _startingPosition(), _relayState(), _operationStartMs(), _operationMs(), _lastMqttSendMs() {
      if (!_settings->IsCalibrated || _settings->Position > COVER_OPEN || _settings->Position < COVER_CLOSE) {
        calibrate();
      }
    }
    
    void handleQuery(int lightNumber, HueLightInfo newInfo, aJsonObject* raw) override {
      int newPosition = (float)newInfo.brightness / (float)MAX_HUE * COVER_OPEN;
      if (_currentInfo.on && !newInfo.on) {
        newPosition = COVER_CLOSE;
      }

      if (newInfo.on && newInfo.brightness == COVER_CLOSE) {
        newPosition = COVER_OPEN;
      }

      _currentInfo = newInfo;
      
      setPosition(newPosition);
      if (RSerial.isActive(RSerial.DEBUG)) {
        RSerial.printf("Received: bri: %d, hue: %d, on: %d, pos: %d", newInfo.brightness, newInfo.hue, newInfo.on, newPosition);
        RSerial.println();
      }
    }

    HueLightInfo getInfo(int lightNumber) {
      HueLightInfo info = {};
      info.hue = -1;
      info.saturation = -1;
      info.bulbType = HueBulbType::DIMMABLE_LIGHT;
      info.brightness = (int) ((float)_settings->Position / COVER_OPEN * (float)MAX_HUE);
      info.on = _settings->Position > (COVER_OPEN * 0.05);

      if (RSerial.isActive(RSerial.DEBUG)) {
        RSerial.printf("getInfo(%d) was called: pos: %d, brightness: %d, on: %d", lightNumber, _settings->Position, info.brightness, info.on);
        RSerial.println();
      }
      return info; 
    }

    unsigned long getOperationStartTime() {
      return _operationStartMs;
    }

    void calibrate() {
      if (RSerial.isActive(RSerial.INFO)) {
        RSerial.println("Calibrating the cover ...");
      }
      
      setRelay(POWER_RELAY | GOING_UP_RELAY);
      _operationStartMs = millis();
      _operationMs = FULL_TIME_MS;
      activity.blink(_operationMs, 100, 100);

      _startingPosition = COVER_CLOSE;
      _settings->Position = COVER_OPEN;
      _settings->IsCalibrated = false;
      _settings->save();
    }

    void setPosition(int position) {
      if (position == _settings->Position)
        return;

      if (RSerial.isActive(RSerial.INFO)) {
        RSerial.printf("Setting position: %d", position);
        RSerial.println();
      }
        
      int relayState = POWER_RELAY;
      int remaining = position - _settings->Position; // 10 - 50
      
      if (remaining < 0) {
        relayState = relayState | GOING_UP_RELAY;
      }

      _operationStartMs = millis();
      setRelay(relayState);
      _operationMs = (FULL_TIME_MS * abs(remaining)) / COVER_OPEN;
      
      if (position == COVER_OPEN || position == COVER_CLOSE) {
        _operationMs = _operationMs * 1.15; // we take a margin as a 15% percent when we are in the 
        
        if (RSerial.isActive(RSerial.INFO)) {
          RSerial.printf("Auto-Calibration to %d with duration %d ms because the requested position is in margin", position, _operationMs);
          RSerial.println();
        }
      }
      
      activity.blink(_operationMs, 300, 200);

      _startingPosition = _settings->Position;

      _settings->IsCalibrated = false;
      _settings->save();
    }

    bool isOperating() {
      //return _relayState & POWER_RELAY; // _operationStartMs > 0;
      return _relayState > 0;
    }

    bool isOpening() {
      //return isOperating() && (_relayState & GOING_UP_RELAY) != 0;
      return _relayState & 1 > 0;
    }

    bool isClosing() {
      //return isOperating() && (_relayState & GOING_UP_RELAY) == 0;
      return _relayState & 2 > 0;
    }

    void loop() {
      unsigned now = millis();

      if (isOperating()) {
        unsigned spent = now - _operationStartMs;
        int lenght = getPositionLenght(spent);
        
        if (isOpening())
          lenght *= -1;
        _settings->Position = min(max(_startingPosition + lenght, COVER_CLOSE), COVER_OPEN);

        if (RSerial.isActive(RSerial.INFO)) {
          RSerial.printf("Starting position: %d and setting to %d from lenght %d (spent time: %d)", _startingPosition, _settings->Position, lenght, spent);
          RSerial.println();
        }
        
        if (spent >= _operationMs) { // the operation needs to stop
          stop();
          _settings->IsCalibrated = true;
          
          if (RSerial.isActive(RSerial.INFO)) {
            RSerial.println("The cover operation has stopped");
          }

          _settings->save();
        }
      }

      now = millis();
      int mqtt_send_delay = isOperating() ? 300 : MQTT_SEND_STATUS_MS;
      if (now - _lastMqttSendMs > mqtt_send_delay) {
        sprintf(buff, "%d", _settings->Position);
        mqttClient.publish(mqtt_position_topic, buff);

        const char * state = _settings->Position >= 0 && _settings->Position <= 90 ? "open" : "close";
        mqttClient.publish(mqtt_state_topic, state);
                
        _lastMqttSendMs = now;
      }
    }

    void stop_later() {
      _operationMs = 0;
    }

    void stop() {
      setRelay(0);
      //setRelay(_relayState & ~(_relayState & POWER_RELAY)); // switch back the relay to stop everything
      _operationStartMs = 0;
    }

    void open() {
      setPosition(COVER_OPEN);
    }

    void close() {
      setPosition(COVER_CLOSE);
    }

    String getFriendlyName(int lightNumber) const {
      return friendly_name;
    }
};

void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(19230);
  RSerial.begin(host);
  RSerial.setSerialEnabled(true);

  if (RSerial.isActive(RSerial.INFO)) {
      RSerial.println("Starting");
  }
  
  _settings = new Settings();
  
  if (RSerial.isActive(RSerial.DEBUG)) {
      RSerial.println("Attaching interruptions ...");
  }

  /*pinMode(GO_UP_BUTTON, INPUT_PULLUP);
  upButton.attach(GO_UP_BUTTON);
  upButton.interval(20);

  pinMode(GO_DOWN_BUTTON, INPUT_PULLUP);
  downButton.attach(GO_DOWN_BUTTON);
  downButton.interval(20);*/

  if (RSerial.isActive(RSerial.DEBUG)) {
      RSerial.println("Setting ArdionoOTA ...");
  }
  
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(admin_password);
  ArduinoOTA.begin();
 
  // Sync our clock
  NTP.begin("pool.ntp.org", 0, true);

  // MQTT setup
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);
  
  coverHandler = new CoverHandler();  
  LightService.setLightsAvailable(1);
  LightService.setLightHandler(0, coverHandler);
  LightService.begin(&httpServer);
  
  httpUpdater.setup(&httpServer, "/update", admin_username, admin_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
      
  //setup http firmware update page.
  if (!MDNS.begin(host) && RSerial.isActive(RSerial.ERROR)) {
    RSerial.println("Error setting up MDNS responder!");
  }

  if (RSerial.isActive(RSerial.INFO)) {
    RSerial.println("Initialization completed");
  }
  
  RSerial.setSerialEnabled(false);
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (length > sizeof(buff))
    return;

  memcpy(buff, payload, length);
  buff[length] = NULL;
  String message(buff);
  String mytopic(topic);

  if (RSerial.isActive(RSerial.INFO)) {
    RSerial.printf("Received topic: '%s' (%d): %s", topic, length, message.c_str());
    RSerial.println();
  }
  
  if (mytopic == mqtt_command_topic) {
    if (message.equals("OPEN")) {
      coverHandler->setPosition(COVER_OPEN);
      return;
    }

    if (message.equals("CLOSE")) {
      coverHandler->setPosition(COVER_CLOSE);
      return;
    }

    if (message.equals("STOP")) {
      coverHandler->stop();
      return;
    }

    if (message.equals("CALIBRATE")) {
      coverHandler->calibrate();
      return;
    }

    if (message.length() > 0 && message.length() <= 3) {
      int position = atoi(message.c_str());
      if (position <= COVER_OPEN && position >= COVER_CLOSE) {
        coverHandler->setPosition(position);
        return;
      } else {
        if (RSerial.isActive(RSerial.WARNING))
          RSerial.printf("Received %s position, but ignoring because is out of range", position);
      }
    }
  }
}

void loop() {
  // read the button state
  /*upButton.update();
  downButton.update();

  if (upButton.read()) {
    if (coverHandler->isClosing()) {
      coverHandler->stop();
    } else {
      coverHandler->open();      
    }
  }

  if (downButton.read()) {
    if (coverHandler->isOpening()) {
      coverHandler->stop();
    } else {
      coverHandler->open();      
    }
  }*/
  
  ArduinoOTA.handle();
  LightService.update();
  RSerial.handle();

  bool oldWifiConnected = wifiConnected;

  bool isConnected;
  if (try_connect_wifi(isConnected) && isConnected) {
    
    if (RSerial.isActive(RSerial.WARNING)) {
      RSerial.println("WiFi network changed");
    }

    MDNS.notifyAPChange();
  }

  if (connect_to_mqtt()) {
    mqttClient.publish(mqtt_state_topic, "online");
  }

  activity.loop();
  coverHandler->loop();
  mqttClient.loop();

  if (!coverHandler->isOperating()) { // if we do not do anything, we sleep
    delay(250);
  }
}

bool try_connect_wifi(bool &isConnected) {
  
  isConnected = WiFi.status() == WL_CONNECTED;
  
  if (isConnected) {
    if (!wifiConnected) { // if it was not connected
      if (RSerial.isActive(RSerial.DEBUG)) {
        RSerial.print("Connected to WiFi '");
        RSerial.print(WiFi.SSID());
        RSerial.println("' !");
      }

      wifiConnected = true;
      activity.stop();
      return true;
    }
    
    return false; // we are connected and we were already connected
  }

  if (!activity.durationExpired()) {
    return false;
  }
  
  if (WiFi.status() == WL_DISCONNECTED) {
    wifiConnected = false;
    if (RSerial.isActive(RSerial.DEBUG))
      RSerial.println("Connecting to WIFI ...");

    WiFi.mode(WIFI_STA);
    WiFi.config(IP_ADDRESS, IP_GATEWAY, IP_MASK);
    WiFi.begin(wifi_ssid, wifi_password);
    activity.blink(10 * 1000, 800, 800);
    return true;
  }

  return true;
}

bool connect_to_mqtt() {
  if (mqttClient.connected() || !activity.durationExpired()) {
    return false;
  }

  if (RSerial.isActive(RSerial.DEBUG)) {
    RSerial.print("Connecting to MQTT ...");
  }

  bool connected = false;
  if (mqttClient.connect(host, mqtt_user, mqtt_password)) {
    mqttClient.subscribe(mqtt_command_topic);
    
    if (RSerial.isActive(RSerial.DEBUG)) {
      RSerial.println(" done");
    }
    
    activity.stop();
    connected = true;
  } else {
    if (RSerial.isActive(RSerial.DEBUG)) {
      RSerial.print("failed, rc=");
      RSerial.print(mqttClient.state());
      RSerial.println(" try again in 10 seconds");
    }
  }

  if (!activity.durationExpired()) // we check if this is not stopped
    activity.blink(10 * 1000, 200, 200);
  return connected;
}

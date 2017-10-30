
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

#include "Blink.h"
#include "LightService.h"
#include "SSDP.h"
#include "secrets.h"

// Able to respond to: ON = 0 position, OFF = position 100

const int POWER_RELAY = 1;
const int GOING_UP_RELAY = 2;
const int LED = 13;
const int COVER_OPEN = 100;
const int COVER_CLOSE = 0;
const int FULL_TIME_MS = 19500;
const int MQTT_SEND_STATUS_MS = 3000;
const char * friendly_name = "Bedroom window cover";
const int EEPROM_POSITION_ADDR = 0;
const int MAX_HUE = 254;

class CoverHandler;

const int GO_UP_BUTTON = 1;
const int GO_DOWN_BUTTON = 2;
LightServiceClass LightService(friendly_name);

RemoteDebug RSerial;
CoverHandler *coverHandler = nullptr;
char buff[0x100];
TimedBlink activity(LED, 100, 200);

Bounce upButton = Bounce();
Bounce downButton = Bounce();

WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer httpServer(80);
bool wifiConnected = false;
ESP8266HTTPUpdateServer httpUpdater;


class CoverHandler : public LightHandler {
    int _startingPosition, _position, _relayState;
    HueLightInfo _currentInfo;
    unsigned long _operationStartMs, _operationMs, _lastMqttSendMs;
    
    void setRelay(int relayState) {
      Serial.flush();
      Serial.write(0xA0);
      Serial.write(0x04);
      Serial.write(relayState);
      Serial.write(0xA1);
      Serial.write('\n');
      Serial.flush();

      _relayState = relayState;
      
      if (RSerial.isActive(RSerial.DEBUG))
        RSerial.printf("Setting relay: %d", relayState);
    }

    int getPositionLenght(unsigned ms) {
      return (COVER_OPEN * ms) / FULL_TIME_MS;
    }

  public:
    CoverHandler() : _startingPosition(), _relayState(), _operationStartMs(), _operationMs(), _lastMqttSendMs() {
      _position = EEPROM.read(EEPROM_POSITION_ADDR);
      if (_position > COVER_OPEN || _position < COVER_CLOSE) {
        calibrate();
      }
    }
    
    void handleQuery(int lightNumber, HueLightInfo newInfo, aJsonObject* raw) override {
      int newPosition = (float)newInfo.brightness / (float)MAX_HUE * COVER_OPEN;
      if (_currentInfo.on && !newInfo.on) {
        newPosition = COVER_CLOSE;
      }

      if (newInfo.on && newInfo.brightness == 0) {
        newPosition = COVER_OPEN;
      }

      _currentInfo = newInfo;
      
      setPosition(newPosition);
      if (RSerial.isActive(RSerial.DEBUG))
        RSerial.printf("Received: bri: %d, hue: %d, on: %d, pos: %d", newInfo.brightness, newInfo.hue, newInfo.on, newPosition);
    }

    HueLightInfo getInfo(int lightNumber) {
      HueLightInfo info = {};
      info.hue = -1;
      info.saturation = -1;
      info.bulbType = HueBulbType::DIMMABLE_LIGHT;
      info.brightness = (int) ((float)_position / COVER_OPEN * (float)MAX_HUE);
      info.on = _position > (COVER_OPEN * 0.05);

      if (RSerial.isActive(RSerial.DEBUG))
        RSerial.printf("getInfo(%d) was called: pos: %d, brightness: %d, on: %d", lightNumber, _position, info.brightness, info.on);
      return info; 
    }

    byte getPosition() const {
      return _position;
    }

    unsigned long getOperationStartTime() {
      return _operationStartMs;
    }

    void calibrate() {
      if (RSerial.isActive(RSerial.INFO))
        RSerial.println("Calibrating the cover ...");

      setRelay(POWER_RELAY | GOING_UP_RELAY);
      _operationStartMs = millis();
      _operationMs = FULL_TIME_MS;
      activity.blink(_operationMs);

      _startingPosition = COVER_OPEN;
      _position = COVER_CLOSE;
      EEPROM.write(EEPROM_POSITION_ADDR, _position);
      EEPROM.write(EEPROM_POSITION_ADDR + 1, 0); // this is to reset that we are not calibrating anymore
      EEPROM.commit();
    }

    void setPosition(int position) {
      if (position == _position)
        return;

      if (RSerial.isActive(RSerial.INFO))
        RSerial.printf("Setting position: %d", position);
        
      int relayState = POWER_RELAY;
      int remaining = position - _position; // 10 - 50
      
      if (remaining < 0) {
        relayState = relayState | GOING_UP_RELAY;
      }

      _operationStartMs = millis();
      setRelay(relayState);
      _operationMs = (FULL_TIME_MS * abs(remaining)) / COVER_OPEN;
      
      if (position == COVER_OPEN || position == COVER_CLOSE) {
        _operationMs = _operationMs * 1.15; // we take a margin as a 15% percent when we are in the 
        
        if (RSerial.isActive(RSerial.INFO))
          RSerial.printf("Auto-Calibration to %d with duration %d ms because the requested position is in margin", position, _operationMs);
      }
      
      activity.blink(_operationMs);

      _startingPosition = _position;
      EEPROM.write(EEPROM_POSITION_ADDR + 1, 0); // this is to reset that we are not calibrating anymore
      EEPROM.commit();
    }

    bool isOperating() {
      return _relayState & POWER_RELAY; // _operationStartMs > 0;
    }

    bool isOpening() {
      return isOperating() && _relayState & GOING_UP_RELAY;
    }

    bool isClosing() {
      return isOperating() && _relayState & GOING_UP_RELAY == 0;
    }

    void loop() {
      unsigned now = millis();

      if (isOperating()) {
        unsigned spent = now - _operationStartMs;
        int lenght = getPositionLenght(spent);
        if (isOpening())
          lenght *= -1;
        _position = min(max(_startingPosition + lenght, COVER_CLOSE), COVER_OPEN);

        if (RSerial.isActive(RSerial.INFO))
          RSerial.printf("Setting position to %d from lenght %d", _position, lenght);
        
        if (spent >= _operationMs) { // the operation needs to stop
          stop();
          EEPROM.write(EEPROM_POSITION_ADDR + 1, 1); // we say that we are calibrated
          
          if (RSerial.isActive(RSerial.INFO))
            RSerial.println("The cover operation has stopped");
        }
        
        EEPROM.write(EEPROM_POSITION_ADDR, _position);
        EEPROM.commit();
      }

      now = millis();
      int mqtt_send_delay = isOperating() ? 300 : MQTT_SEND_STATUS_MS;
      if (now - _lastMqttSendMs > mqtt_send_delay) {
        sprintf(buff, "%d", _position);
        mqttClient.publish(mqtt_position_topic, buff);

        const char * state = _position >= 0 && _position <= 90 ? "open" : "close";
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
  EEPROM.begin(64);

  /*pinMode(GO_UP_BUTTON, INPUT_PULLUP);
  upButton.attach(GO_UP_BUTTON);
  upButton.interval(20);

  pinMode(GO_DOWN_BUTTON, INPUT_PULLUP);
  downButton.attach(GO_DOWN_BUTTON);
  downButton.interval(20);*/

  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(admin_password);
  ArduinoOTA.begin();

  RSerial.begin(host); 
  RSerial.setSerialEnabled(true);


  coverHandler = new CoverHandler();  
 
  // Sync our clock
  NTP.begin("pool.ntp.org", 0, true);
  LightService.setLightsAvailable(1);
  LightService.setLightHandler(0, coverHandler);
  LightService.begin(&httpServer);
  
  httpUpdater.setup(&httpServer, "/update", admin_username, admin_password);
  httpServer.begin();
  RSerial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser and login with username '%s' and your password\n", host, admin_username);
  
  // MQTT setup
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);

  //setup http firmware update page.
  if (!MDNS.begin(host)) {
    RSerial.println("Error setting up MDNS responder!");
  }

  MDNS.addService("http", "tcp", 80);
   
  if (RSerial.isActive(RSerial.DEBUG))
      RSerial.println("Cover Handler Started !");

  if (!EEPROM.read(EEPROM_POSITION_ADDR + 1)) {
    if (RSerial.isActive(RSerial.INFO))
      RSerial.println("Reseting the position because it is not calibrated");
    coverHandler->setPosition(0);
    EEPROM.write(EEPROM_POSITION_ADDR + 1, 1);
    EEPROM.commit();
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (length > sizeof(buff))
    return;

  memcpy(buff, payload, length);
  buff[length] = NULL;
  String message(buff);
  String mytopic(topic);

  if (RSerial.isActive(RSerial.INFO))
    RSerial.printf("Received topic: '%s' (%d): %s", topic, length, message.c_str());
  
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

  bool isConnected;
  if (try_connect_wifi(isConnected) && isConnected) {
    wifiConnected = true;
    try_connect_to_mqtt();
  }

  activity.loop();
  coverHandler->loop();
  mqttClient.loop();

  if (!coverHandler->isOperating()) { // if we do not do anything, we sleep
    delay(250);
  }
}

bool try_connect_wifi(bool &isConnected) {
  if (!activity.durationExpired()) {
    return false;
  }

  if (WiFi.status() == WL_DISCONNECTED) {
    wifiConnected = false;
    if (RSerial.isActive(RSerial.DEBUG))
      RSerial.print("Connecting to WIFI ...");

    WiFi.mode(WIFI_STA);    
    WiFi.config(IP_ADDRESS, IP_GATEWAY, IP_MASK);
    WiFi.begin(wifi_ssid, wifi_password);
  }

  isConnected = WiFi.status() == WL_CONNECTED;
  if (isConnected) {
    if (!wifiConnected) { // if it was not connected
      if (RSerial.isActive(RSerial.DEBUG))
        RSerial.println("connected !");
      activity.off();
    }
    
    return true;
  }

  activity.blink(10 * 1000);
  RSerial.println(".");
  return true;
}

bool try_connect_to_mqtt() {
  if (mqttClient.connected() || !activity.durationExpired()) {
    return false;
  }

  if (RSerial.isActive(RSerial.DEBUG))
    RSerial.print("Connecting to MQTT ...");
    
  if (mqttClient.connect(host, mqtt_user, mqtt_password)) {
    mqttClient.subscribe(mqtt_command_topic);
    
    if (RSerial.isActive(RSerial.DEBUG))
      RSerial.println("done");
    
    activity.off();
  } else {
    if (RSerial.isActive(RSerial.DEBUG)) {
      RSerial.print("failed, rc=");
      RSerial.print(mqttClient.state());
      RSerial.println(" try again in 10 seconds");
    }
  }
  
  activity.blink(10 * 1000);
  return true;
}




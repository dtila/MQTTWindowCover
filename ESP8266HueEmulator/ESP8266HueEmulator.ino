
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <EEPROM.h>

#include <TimeLib.h>
#include <NtpClientLib.h>
#include "LightService.h"
#include "Blink.h"

#include <ESP8266WebServer.h>
#include "SSDP.h"
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT

#include "secrets.h"

// Able to respond to: ON = 0 position, OFF = position 100

const int POWER_RELAY = 1;
const int GOING_UP_RELAY = 2;
const int LED = 13;
const int COVER_OPEN = 0;
const int COVER_CLOSE = 100;
const int FULL_TIME_MS = 5000;
const int MQTT_SEND_STATUS_MS = 3000;
const char * friendly_name = "Bedroom window cover";
const int EEPROM_POSITION_ADDR = 0;
const int MAX_HUE = 254;

class CoverHandler;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
LightServiceClass LightService(friendly_name);
CoverHandler *coverHandler = NULL;
char buff[0x100];
bool debugging = 0;
TimedBlink activity(LED, 200, 200);

void debug(const char * text);


class CoverHandler : public LightHandler {
    int _startingPosition, _position, _relayState;
    HueLightInfo _currentInfo;
    unsigned long _operationStartMs, _operatingMs, _lastMqttSendMs;
    
    void setRelay(int relayState) {
      Serial.write(0xA0);
      Serial.write(0x04);
      Serial.write(relayState);
      Serial.write(0xA1);
      Serial.write('\n');
      Serial.flush();

      _relayState = relayState;
      sprintf(buff, "Setting relay: %d", relayState);
      debug(buff);
    }

    int getPositionLenght(unsigned ms) {
      return (COVER_CLOSE * ms) / FULL_TIME_MS;
    }

  public:
    CoverHandler() : _startingPosition(), _relayState(), _operationStartMs(), _operatingMs(), _lastMqttSendMs() {
      _position = EEPROM.read(EEPROM_POSITION_ADDR);
      if (_position < COVER_OPEN || _position > COVER_CLOSE) {
        calibrate();
      }
    }
    
    void handleQuery(int lightNumber, HueLightInfo newInfo, aJsonObject* raw) {
      int newPosition = (float)newInfo.brightness / (float)MAX_HUE * COVER_CLOSE;
      if (_currentInfo.on && !newInfo.on) {
        newPosition = COVER_CLOSE;
      }

      _currentInfo = newInfo;
      
      setPosition(newPosition);
      sprintf(buff, "Received: bri: %d, hue: %d, on: %d, pos: %d", newInfo.brightness, newInfo.hue, newInfo.on, newPosition);
      debug(buff);
    }

    HueLightInfo getInfo(int lightNumber) {
      HueLightInfo info = {};
      info.brightness = (int) ((float)_position / COVER_CLOSE * (float)MAX_HUE);
      info.on = _position > (COVER_CLOSE * 0.95);

      sprintf(buff, "getInfo(%d) was called: pos: %d, brightness: %d, on: %d", lightNumber, _position, info.brightness, info.on);
      debug(buff);
      return info; 
    }

    byte getPosition() const {
      return _position;
    }

    void calibrate() {
      debug("Calibrating the cover ...");

      setRelay(POWER_RELAY | GOING_UP_RELAY);
      _operationStartMs = millis();
      _operatingMs = FULL_TIME_MS;
      activity.blink(_operatingMs);

      _startingPosition = COVER_CLOSE;
      _position = COVER_OPEN;
      EEPROM.write(EEPROM_POSITION_ADDR, _position);
      EEPROM.write(EEPROM_POSITION_ADDR + 1, 0); // this is to reset that we are not calibrating anymore
      EEPROM.commit();
    }

    void setPosition(int position) {
      if (position == _position)
        return;
    
      sprintf(buff, "Setting position: %d", position);
      debug(buff);
        
      int relayState = POWER_RELAY;
      int remaining = position - _position; // 10 - 50
      
      if (remaining < 0) {
        relayState = relayState | GOING_UP_RELAY;
      }

      _operationStartMs = millis();
      setRelay(relayState);
      _operatingMs = (FULL_TIME_MS * abs(remaining)) / COVER_CLOSE;
      
      if (position == COVER_OPEN || position == COVER_CLOSE) {
        _operatingMs = _operatingMs * 1.15; // we take a margin as a 15% percent when we are in the 
        sprintf(buff, "Auto-Calibration to %d with duration %d ms because the requested position is in margin", position, _operatingMs);
        debug(buff);
      }
      
      activity.blink(_operatingMs);

      _startingPosition = _position;
      EEPROM.write(EEPROM_POSITION_ADDR + 1, 0); // this is to reset that we are not calibrating anymore
      EEPROM.commit();
    }

    bool isOperating() {
      return _operationStartMs > 0;
    }

    bool isGoingUp() {
      return _relayState & GOING_UP_RELAY;
    }

    void loop() {
      unsigned now = millis();

      if (isOperating()) {
        unsigned spent = now - _operationStartMs;
        int lenght = getPositionLenght(spent);
        if (isGoingUp())
          lenght *= -1;
        _position = min(max(_startingPosition + lenght, COVER_OPEN), COVER_CLOSE);
  
        if (spent >= _operatingMs) { // the operation needs to stop
          stop();
          EEPROM.write(EEPROM_POSITION_ADDR + 1, 1); // we say that we are calibrated
          debug("The cover operation has stopped");
        }

        sprintf(buff, "Setting position to %d from lenght %d", _position, lenght);
        debug(buff);
        
        EEPROM.write(EEPROM_POSITION_ADDR, _position);
        EEPROM.commit();
      }

      now = millis();
      int mqtt_send_delay = isOperating() ? 300 : MQTT_SEND_STATUS_MS;
      if (now - _lastMqttSendMs > mqtt_send_delay) {
        sprintf(buff, "%d", _position);
        mqttClient.publish(mqtt_state_topic, buff);
        _lastMqttSendMs = now;
      }
    }

    void stop() {
      setRelay(0);
       //setRelay(_relayState & ~(_relayState & POWER_RELAY)); // switch back the relay to stop everything
       _operationStartMs = 0;
    }

    String getFriendlyName(int lightNumber) const {
      return friendly_name;
    }
};

void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(19230);
  EEPROM.begin(512);

  coverHandler = new CoverHandler();  
  ensure_wifi_connection();

  ArduinoOTA.onStart([]() {
    debug("OTA begin update");
  });
  ArduinoOTA.onEnd([]() {
    debug("OTA end update");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    sprintf(buff, "OTA update progress:: %u%%\r", (progress / (total / 100)));    
    debug(buff);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    char * reason = "Unknown";
    if (error == OTA_AUTH_ERROR) reason = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR) reason = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) reason = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) reason = "Receive Failed";
    else if (error == OTA_END_ERROR) reason = "End Failed";

    sprintf(buff, "OTA Error[%u]: (%s)", error, reason);    
    debug(buff);
  });
  
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname("sonoff_bedroom_cover");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();

  // Sync our clock
  NTP.begin("pool.ntp.org", 0, true);
  LightService.setLightsAvailable(1);
  LightService.setLightHandler(0, coverHandler);
  LightService.begin();

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);

  if (!EEPROM.read(EEPROM_POSITION_ADDR + 1)) {
    debug("Reseting the position because it is not calibrated");
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

  sprintf(buff, "Received topic: '%s' (%d): %s", topic, length, message.c_str());
  debug(buff);
  
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

    if (message.equals("DEBUG")) {
      debugging = 1;
      debug("Debugging enabled!");
      return;
    }

    if (message.length() > 0 && message.length() <= 3) {
      int position = atoi(message.c_str());
      if (position >= COVER_OPEN && position <= COVER_CLOSE) {
        coverHandler->setPosition(position);
        return;
      } else {
        sprintf(buff, "Received %s position, but ignoring because is out of range", position);
        debug(buff);
      }
    }
  }
}

void debug(const char* text) {
  /*Serial.write(text);
  Serial.write('\n');
  Serial.flush();*/
  if (debugging)
    mqttClient.publish(mqtt_debug_topic, text);
  
  Serial.println(text);
}

void loop() {
  ArduinoOTA.handle();
  LightService.update();

  ensure_wifi_connection();
  ensure_mqtt_connection();

  activity.loop();
  coverHandler->loop();
  mqttClient.loop();
  
  if (!coverHandler->isOperating()) // if we do not do anything, we sleep
    delay(250);
  
  //Serial.println("loop");
}


void ensure_wifi_connection() {
  if (WiFi.status() == WL_CONNECTED)
    return;
     
  WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(172, 25, 1, 250), IPAddress(172, 25, 1, 1), IPAddress(255, 255, 255, 0));
  WiFi.begin(wifi_ssid, wifi_password);

  activity.on();
  while (WiFi.status() != WL_CONNECTED) {
    debug("Connecting to WIFI ...");
    delay(500);
  }
  activity.off();
  debug("Connected to WIFI !");
}

void ensure_mqtt_connection() {
  if (mqttClient.connected())
    return;

  activity.on();
  while (!mqttClient.connected()) {
    debug("Connecting to MQTT ...");
    if (mqttClient.connect("bedroom_cover", mqtt_user, mqtt_password)) {
      mqttClient.subscribe(mqtt_command_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  activity.off();
  debug("Connected to MQTT !");
}



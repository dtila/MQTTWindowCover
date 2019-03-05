# ESP8266 Window Cover Hue Emulator

This Arduino schetch emulates a Philips Hue bridge running on ESP8266 and expose a  the windows shelter as a Philips Hue bulb. The on/off commands are opening/closing the window cover and the Brightness function is acting as a tilt position.

Please note that currently only the bare minimum functions are implemented to the Hue bridge emulator, but it is enough that most common apps are able to work with. 

### Demo
The window covers they are running for more than a year in my house and I was able to expose via [Home Assistant](https://github.com/home-assistant/home-assistant) to Google Home. See in action below:

<a href="http://www.youtube.com/watch?feature=player_embedded&v=aQU2oiVyFe4
" target="_blank"><img src="http://img.youtube.com/vi/aQU2oiVyFe4/0.jpg" 
alt="Bedroom Covers" width="240" height="180" border="0" /></a>

### Build Status

 [![Build Status](https://travis-ci.org/dtila/MQTTWindowCover.svg)](https://travis-ci.org/dtila/MQTTWindowCover)


To make this work, the sketch advertises its service with the so-called "Simple Service Discovery Protocol" (SSDP) that is also used as discovery protocol of Universal Plug and Play (UPnP).

# Hardware

For now the only hardware that is supported (and tested) is:
- electric cover to have a AC motor that can be connected directly to the 220V power network
- the electric cover needs to have 3 wires (1 common and anoter 2 for direction - OPEN or CLOSE )
- [Sonoff Dual](https://www.itead.cc/sonoff-dual.html) to act as a command gateway

You can have a look to the connection diagram here. During the tests if you have noticed that the OPEN and CLOSE are swapped, just swap the two wires.

![schematic](https://raw.githubusercontent.com/tilutza/MQTTWindowCover/master/Sonoff_Dual_Wiring_instruction.jpg)

# Features

1. The ESP board is advertised in the network as a Philips Hue bridge for controlling the cover
2. A simple HTTP page is served as a status
3. Remote debugging is supported via Telnet
4. OTA updates is supported. The url for this is {ip}/update
5. mDNS is supported
6. MQTT control is supported thought topics:
  1 Command topic: OPEN, CLOSE, STOP, CALIBRATE
  2 State topic: the board is sending if it's ON or OFF
  3 Position topic: the board is sending every time when the position is changed

__Remarks__
The position of the cover is stored in the EEPROM memory. This means that if you are turning of the device, it keeps the previous position in memory.

# Usage

__WARNING: Sonoff is connected directly to the 220V power network. Disconnect the board before to flash the new firmware or when you connect the electric cover.__

* Load the sketch onto your [Sonoff Dual](https://www.itead.cc/sonoff-dual.html) device
* Watch the output of the sketch in a serial console
* Connect to the emulated bridge by using a Hue client app
* Switch on/off or adjust the Brightness

## Integrations
![Home Assistant](https://community.home-assistant.io/uploads/default/original/1X/55d3799ae190b95b9c1eef96230af9ca016e496a.png)

To add the cover in the [Home Assistant](https://github.com/home-assistant/home-assistant) (up to 0.85), just paste this in the configuration file:

```
cover:
  - platform: mqtt
    name: "Bedroom"
    command_topic: "bedroom/cover/set"
    position_topic: "bedroom/cover/position"
    set_position_topic: "bedroom/cover/set"
    payload_open: "OPEN"
    payload_closel: "CLOSE"
    payload_stop: "STOP"
    state_topic: "bedroom/cover/state"
    state_open: "open"
    state_closed: "close"
    tilt_min: 0
    tilt_max: 100
    tilt_invert_state: true
```

For the version above 0.85 you need to use:

```
  - platform: mqtt
    name: "Bedroom"
    command_topic: "bedroom/cover/set"
    position_topic: "bedroom/cover/position"
    set_position_topic: "bedroom/cover/set"
    payload_open: "OPEN"
    payload_close: "CLOSE"
    payload_stop: "STOP"
    state_topic: "bedroom/cover/state"
    state_open: "open"
    state_closed: "close"
```

## Compilation

Before compile the software, you need to perform some additional changes:
1. Modify the ```secrets.h``` file with the manual, host
2. Change the ```FULL_TIME_MS``` value with the full time (in miliseconds) that your cover takes from a full opening to a full close. This can vary depending on the lenght or type of the motor that the cover has.
3. Change the ```friendly_name``` variable with something that matches your preference

To make sure you have all the libraries needed:
```
mkdir -p $HOME/Arduino/libraries/
cd $HOME/Arduino/libraries/
git clone https://github.com/tilutza/aJson.git
git clone https://github.com/PaulStoffregen/Time.git
git clone https://github.com/gmag11/NtpClient.git
git clone https://github.com/thomasfredericks/Bounce2.git
git clone https://github.com/knolleary/pubsubclient.git
git clone https://github.com/tilutza/RemoteDebug.git
```

# Credits

* Philips for providing open Hue APIs that are not restricted for use on Philips-branded hardware (as far as I can see by looking at their liberal [Terms and Conditions of Use](https://github.com/probonopd/ESP8266HueEmulator/wiki/Discovery#terms-and-conditions-of-use))
* probonopd for providing the original Hue Emulator

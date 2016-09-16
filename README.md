Based on https://github.com/corbanmailloux/esp-mqtt-rgb-led/blob/master/mqtt_esp8266_rgb.ino

## Channels

Channel | Remark
XXXXXXXX/light/rgb | Dimmer send current status (ON/OFF)
XXXXXXXX/light/rgb/set | Set brightness (0-255) and RGB values (Format r,g,b) values 0-255 for each channel
XXXXXXXX/light/w1 | Status of W1 channel
XXXXXXXX/light/w1/set | Set W1 ON or OFF and brightness (0-255)
XXXXXXXX/light/w2 | Status of W2 channel
XXXXXXXX/light/w2/set | Set W2 ON or OFF and brightness (0-255)

## Home assistant example configuration

```yaml
light:

- platform: mqtt_json
  name: RGB
  state_topic: "XXXXXXXX/light/rgb"
  command_topic: "XXXXXXXX/light/rgb/set"
  brightness: true
  rgb: true

- platform: mqtt_json
  name: W1
  state_topic: "XXXXXXXX/light/w1"
  command_topic: "XXXXXXXX/light/w1/set"
  brightness: true
  
- platform: mqtt_json
  name: W2
  state_topic: "XXXXXXXX/light/w2"
  command_topic: "XXXXXXXX/light/w2/set"
  brightness: true

```

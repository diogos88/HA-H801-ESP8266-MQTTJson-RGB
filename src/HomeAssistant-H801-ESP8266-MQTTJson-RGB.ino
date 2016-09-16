//
// Alternative firmware for H801 5 channel LED dimmer
// based on https://github.com/open-homeautomation/h801/blob/master/mqtt/mqtt.ino
//
#include "Secrets.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>   // Local WebServer used to serve the configuration portal
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>        // WiFi Configuration Magic
#include <PubSubClient.h>       // MQTT m_client
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#define DEVELOPMENT

#define RGB_LIGHT_BOARD_RED_PIN     15
#define RGB_LIGHT_BOARD_GREEN_PIN   13
#define RGB_LIGHT_BLUE_PIN          12
#define W1_PIN                      14
#define W2_PIN                      4

#define BOARD_GREEN_PIN             1
#define BOARD_RED_PIN               5

#define DEFAULT_TRANSITION_STEPS    2

struct RgbState
{
   byte rPin;
   byte gPin;
   byte bPin;

   bool stateOn;
   byte brightness, r, g, b;

   // fade/transitions
   bool startFade;
   bool isFading;
   unsigned long lastLoop;
   unsigned int transitionSteps;
   int loopCount;
   int stepR, stepG, stepB, stepBrightness;
   int rValue, gValue, bValue, brightnessValue;

   // flash
   bool startFlash;
   bool isFlashing;
   unsigned int flashLength;
   unsigned long flashStartTime;

   void init(byte redPin, byte greenPin, byte bluePin)
   {
      stateOn = false;

      rPin = redPin;
      r = 255;

      gPin = greenPin;
      g = 255;

      bPin = bluePin;
      b = 255;

      brightness = 255;

      startFade = false;
      isFading = false;
      lastLoop = 0;
      transitionSteps = 0;
      loopCount = 0;

      startFlash = false;
      isFlashing = false;
      flashLength = 0;
      flashStartTime = 0;
   }

   //void printValues()
   //{
   //   Serial1.print("stateOn : ");
   //   Serial1.println(stateOn);

   //   Serial1.print("brightness : ");
   //   Serial1.println(brightness);

   //   Serial1.print("r : ");
   //   Serial1.println(r);

   //   Serial1.print("g : ");
   //   Serial1.println(g);

   //   Serial1.print("b : ");
   //   Serial1.println(b);

   //   Serial1.print("startFade : ");
   //   Serial1.println(startFade);

   //   Serial1.print("isFading : ");
   //   Serial1.println(isFading);

   //   Serial1.print("startFlash : ");
   //   Serial1.println(startFlash);

   //   Serial1.print("isFlashing : ");
   //   Serial1.println(isFlashing);
   //}
};

struct WhiteState
{
   byte whitePin;

   bool stateOn;
   byte brightness;

   // fade/transitions
   bool startFade;
   bool isFading;
   unsigned long lastLoop;
   unsigned int transitionSteps;
   int loopCount;
   int stepBrightness;
   int brightnessValue;

   // flash
   bool startFlash;
   bool isFlashing;
   unsigned int flashLength;
   unsigned long flashStartTime;

   void init(byte pin)
   {
      stateOn = false;

      whitePin = pin;

      brightness = 255;

      startFade = false;
      isFading = false;
      lastLoop = 0;
      transitionSteps = 0;
      loopCount = 0;

      startFlash = false;
      isFlashing = false;
      flashLength = 0;
      flashStartTime = 0;
   }
};

WiFiManager m_wifiManager;
WiFiClient m_wifiClient;
PubSubClient m_client(m_wifiClient);
ESP8266WebServer m_httpServer(80);
ESP8266HTTPUpdateServer m_httpUpdater;

// Light
// the payload that represents enabled/disabled state, by default
const char* LIGHT_ON = "ON";
const char* LIGHT_OFF = "OFF";

const char* JSON_LIGHT_BRIGHTNESS = "brightness";
const char* JSON_LIGHT_COLOR = "color";
const char* JSON_LIGHT_COLOR_R = "r";
const char* JSON_LIGHT_COLOR_G = "g";
const char* JSON_LIGHT_COLOR_B = "b";
const char* JSON_LIGHT_STATE = "state";
const char* JSON_LIGHT_FLASH = "flash";
const char* JSON_LIGHT_TRANSITION = "transition";

const int MQTT_BUFFER_SIZE = JSON_OBJECT_SIZE(10);

RgbState m_rgbState;
WhiteState m_white1State;
WhiteState m_white2State;

unsigned int m_loopCount = 0;

void setupWifi()
{
   // reset if necessary
   // m_wifiManager.resetSettings();

   m_wifiManager.setTimeout(3600);

   WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
   m_wifiManager.addParameter(&custom_mqtt_server);

   WiFiManagerParameter custom_password("password", "password for updates", password, 40);
   m_wifiManager.addParameter(&custom_password);

   m_wifiManager.setCustomHeadElement(chip_id);
   m_wifiManager.autoConnect();

   mqtt_server = custom_mqtt_server.getValue();
   password = custom_password.getValue();

   Serial1.println("");

   Serial1.println("WiFi connected");
   Serial1.println("IP address: ");
   Serial1.println(WiFi.localIP());

   Serial1.println("");

   // init the MQTT connection
   m_client.setServer(mqtt_server, mqtt_port);
   m_client.setCallback(callback);

   // replace chip ID in channel names
   memcpy(MQTT_LIGHT_RGB_STATE_TOPIC, chip_id, 8);
   memcpy(MQTT_LIGHT_RGB_COMMAND_TOPIC, chip_id, 8);

   memcpy(MQTT_LIGHT_W1_STATE_TOPIC, chip_id, 8);
   memcpy(MQTT_LIGHT_W1_COMMAND_TOPIC, chip_id, 8);

   memcpy(MQTT_LIGHT_W2_STATE_TOPIC, chip_id, 8);
   memcpy(MQTT_LIGHT_W2_COMMAND_TOPIC, chip_id, 8);

   digitalWrite(BOARD_RED_PIN, 1);

   // OTA
   // do not start OTA server if no password has been set
   if (password != "") {
      MDNS.begin(myhostname);
      m_httpUpdater.setup(&m_httpServer, username, password);
      m_httpServer.begin();
      MDNS.addService("http", "tcp", 80);
   }
}

void publishAllStates()
{
   publishRGBState(m_rgbState, MQTT_LIGHT_RGB_STATE_TOPIC);
   publishWhiteState(m_white1State, MQTT_LIGHT_W1_STATE_TOPIC);
   publishWhiteState(m_white2State, MQTT_LIGHT_W2_STATE_TOPIC);
}

void publishRGBState(struct RgbState& rgbState, char* topic)
{
   StaticJsonBuffer<MQTT_BUFFER_SIZE> jsonBuffer;
   JsonObject& root = jsonBuffer.createObject();

   root[JSON_LIGHT_STATE] = (rgbState.stateOn) ? LIGHT_ON : LIGHT_OFF;
   root[JSON_LIGHT_BRIGHTNESS] = rgbState.brightness;

   JsonObject& colorData = root.createNestedObject(JSON_LIGHT_COLOR);
   colorData[JSON_LIGHT_COLOR_R] = rgbState.r;
   colorData[JSON_LIGHT_COLOR_G] = rgbState.g;
   colorData[JSON_LIGHT_COLOR_B] = rgbState.b;

   char outData[root.measureLength() + 1];;
   root.printTo(outData, sizeof(outData));
   m_client.publish(topic, outData);

   Serial1.print("publishRGBState {pins:");
   Serial1.print(rgbState.rPin);
   Serial1.print(",");
   Serial1.print(rgbState.gPin);
   Serial1.print(",");
   Serial1.print(rgbState.bPin);
   Serial1.print("}: ");
   Serial1.println(outData);
}

void publishWhiteState(struct WhiteState& whiteState, char* topic)
{
   StaticJsonBuffer<MQTT_BUFFER_SIZE> jsonBuffer;
   JsonObject& root = jsonBuffer.createObject();

   root[JSON_LIGHT_STATE] = (whiteState.stateOn) ? LIGHT_ON : LIGHT_OFF;
   root[JSON_LIGHT_BRIGHTNESS] = whiteState.brightness;

   char outData[root.measureLength() + 1];;
   root.printTo(outData, sizeof(outData));
   m_client.publish(topic, outData);

   Serial1.print("publishWhiteState {pin:");
   Serial1.print(whiteState.whitePin);
   Serial1.print("}: ");
   Serial1.println(outData);
}

void setColor(struct RgbState& rgbState, byte brightness, byte r, byte g, byte b)
{
   analogWrite(rgbState.rPin, map(r, 0, 255, 0, brightness));
   analogWrite(rgbState.gPin, map(g, 0, 255, 0, brightness));
   analogWrite(rgbState.bPin, map(b, 0, 255, 0, brightness));
}

void setWhite(struct WhiteState& whiteState, byte brightness, byte w)
{
   analogWrite(whiteState.whitePin, map(w, 0, 255, 0, brightness));
}

int calculateStep(int prevValue, int endValue) {
   int step = endValue - prevValue; // What's the overall gap?
   if (step)                          // If its non-zero, 
      step = 1020 / step;            //   divide by 1020

   return step;
}

int calculateVal(int step, int val, int i) {
   if ((step) && i % step == 0)   // If step is non-zero and its time to change a value,
   {
      if (step > 0)               //   increment the value if step is positive...
         val += 1;
      else if (step < 0)          //   ...or decrement it if step is negative
         val -= 1;
   }

   //// Defensive driving: make sure val stays in the range 0-255
   //if (val > 255)
   //   val = 255;
   //else if (val < 0)
   //   val = 0;

   return constrain(val, 0, 255);
}

// function called to adapt the brightness and the colors of the led
void updateColorState(struct RgbState& rgbState)
{
   if (rgbState.stateOn)
   {
      if (rgbState.isFlashing)
      {
         if (rgbState.startFlash)
         {
            rgbState.startFlash = false;
            rgbState.flashStartTime = millis();
         }

         if ((millis() - rgbState.flashStartTime) <= rgbState.flashLength)
         {
            if ((millis() - rgbState.flashStartTime) % 1000 <= 500)
               setColor(rgbState, rgbState.brightness, rgbState.r, rgbState.g, rgbState.b);
            else
               setColor(rgbState, rgbState.brightness, 0, 0, 0);
         }
         else
         {
            rgbState.isFlashing - false;
            setColor(rgbState, rgbState.brightness, rgbState.r, rgbState.g, rgbState.b);
         }
      }
      else
      {
         if (rgbState.transitionSteps == 0)
         {
            setColor(rgbState, rgbState.brightness, rgbState.r, rgbState.g, rgbState.b);
            rgbState.rValue = rgbState.r;
            rgbState.gValue = rgbState.g;
            rgbState.bValue = rgbState.b;
            rgbState.brightnessValue = rgbState.brightness;
            rgbState.startFade = false;
         }

         if (rgbState.startFade)
         {
            if (rgbState.transitionSteps != 0)
            {
               rgbState.isFading = true;
               rgbState.loopCount = 0;
               rgbState.stepR = calculateStep(rgbState.rValue, rgbState.r);
               rgbState.stepG = calculateStep(rgbState.gValue, rgbState.g);
               rgbState.stepB = calculateStep(rgbState.bValue, rgbState.b);
               rgbState.stepBrightness = calculateStep(rgbState.brightnessValue, rgbState.brightness);
            }
         }

         if (rgbState.isFading)
         {
            rgbState.startFade = false;
            unsigned long now = millis();

            if (now - rgbState.lastLoop > rgbState.transitionSteps)
            {
               rgbState.loopCount++;
               if (rgbState.loopCount <= 1020)
               {
                  rgbState.lastLoop = now;
                  rgbState.rValue = ((rgbState.stepR > 0) ?
                     _min(rgbState.r, (byte)calculateVal(rgbState.stepR, rgbState.rValue, rgbState.loopCount)) :
                     _max(rgbState.r, (byte)calculateVal(rgbState.stepR, rgbState.rValue, rgbState.loopCount)));

                  rgbState.gValue = ((rgbState.stepG > 0) ?
                     _min(rgbState.g, (byte)calculateVal(rgbState.stepG, rgbState.gValue, rgbState.loopCount)) :
                     _max(rgbState.g, (byte)calculateVal(rgbState.stepG, rgbState.gValue, rgbState.loopCount)));

                  rgbState.bValue = ((rgbState.stepB > 0) ?
                     _min(rgbState.b, (byte)calculateVal(rgbState.stepB, rgbState.bValue, rgbState.loopCount)) :
                     _max(rgbState.b, (byte)calculateVal(rgbState.stepB, rgbState.bValue, rgbState.loopCount)));

                  rgbState.brightnessValue = ((rgbState.stepBrightness > 0) ?
                     _min(rgbState.brightness, (byte)calculateVal(rgbState.stepBrightness, rgbState.brightnessValue, rgbState.loopCount)) :
                     _max(rgbState.brightness, (byte)calculateVal(rgbState.stepBrightness, rgbState.brightnessValue, rgbState.loopCount)));

                  setColor(rgbState, rgbState.brightnessValue, rgbState.rValue, rgbState.gValue, rgbState.bValue);
               }
               else
               {
                  rgbState.isFading = false;
                  setColor(rgbState, rgbState.brightness, rgbState.r, rgbState.g, rgbState.b);
               }
            }
         }
         else
         {
            setColor(rgbState, rgbState.brightness, rgbState.r, rgbState.g, rgbState.b);
         }
      }
   }
   else
   {
      setColor(rgbState, rgbState.brightness, 0, 0, 0);
   }
}

void updateWhiteState(struct WhiteState& whiteState)
{
   if (whiteState.stateOn)
   {
      if (whiteState.isFlashing)
      {
         if (whiteState.startFlash)
         {
            whiteState.startFlash = false;
            whiteState.flashStartTime = millis();
         }

         if ((millis() - whiteState.flashStartTime) <= whiteState.flashLength)
         {
            if ((millis() - whiteState.flashStartTime) % 1000 <= 500)
               setWhite(whiteState, whiteState.brightness, 255);
            else
               setWhite(whiteState, whiteState.brightness, 0);
         }
         else
         {
            whiteState.isFlashing - false;
            setWhite(whiteState, whiteState.brightness, 255);
         }
      }
      else
      {
         if (whiteState.startFade)
         {
            if (whiteState.transitionSteps == 0)
            {
               setWhite(whiteState, whiteState.brightness, 255);
               whiteState.brightnessValue = whiteState.brightness;
               whiteState.startFade = false;
            }
            else
            {
               whiteState.isFading = true;
               whiteState.loopCount = 0;
               whiteState.stepBrightness = calculateStep(whiteState.brightnessValue, whiteState.brightness);
            }
         }

         if (whiteState.isFading)
         {
            whiteState.startFade = false;
            unsigned long now = millis();

            if (now - whiteState.lastLoop > whiteState.transitionSteps)
            {
               whiteState.loopCount++;
               if (whiteState.loopCount <= 1020)
               {
                  whiteState.lastLoop = now;

                  whiteState.brightnessValue = ((whiteState.stepBrightness > 0) ?
                     _min(whiteState.brightness, (byte)calculateVal(whiteState.stepBrightness, whiteState.brightnessValue, whiteState.loopCount)) :
                     _max(whiteState.brightness, (byte)calculateVal(whiteState.stepBrightness, whiteState.brightnessValue, whiteState.loopCount)));

                  setWhite(whiteState, whiteState.brightnessValue, 255);
               }
               else
               {
                  whiteState.isFading = false;
                  setWhite(whiteState, whiteState.brightness, 255);
               }
            }
         }
         else
         {
            setWhite(whiteState, whiteState.brightness, 255);
         }
      }
   }
   else
   {
      setWhite(whiteState, whiteState.brightness, 0);
   }
}

void parseRGBState(struct RgbState& rgbState, JsonObject& root)
{
   if (root.containsKey(JSON_LIGHT_STATE))
      rgbState.stateOn = (strcmp(root[JSON_LIGHT_STATE], LIGHT_ON) == 0);

   if (root.containsKey(JSON_LIGHT_COLOR))
   {
      rgbState.r = root[JSON_LIGHT_COLOR][JSON_LIGHT_COLOR_R];
      rgbState.g = root[JSON_LIGHT_COLOR][JSON_LIGHT_COLOR_G];
      rgbState.b = root[JSON_LIGHT_COLOR][JSON_LIGHT_COLOR_B];
   }

   if (root.containsKey(JSON_LIGHT_BRIGHTNESS))
      rgbState.brightness = root[JSON_LIGHT_BRIGHTNESS];

   rgbState.isFlashing = root.containsKey(JSON_LIGHT_FLASH);

   if (rgbState.isFlashing)
   {
      rgbState.flashLength = (int)root[JSON_LIGHT_FLASH] * 1000;
   }
   else
   {
      if (root.containsKey(JSON_LIGHT_TRANSITION))
         rgbState.transitionSteps = root[JSON_LIGHT_TRANSITION];
      else
         rgbState.transitionSteps = DEFAULT_TRANSITION_STEPS;
   }

   rgbState.startFlash = rgbState.isFlashing;
   rgbState.startFade = !rgbState.isFlashing && (rgbState.transitionSteps > 0);
}

void parseWhiteState(struct WhiteState& whiteState, JsonObject& root)
{
   if (root.containsKey(JSON_LIGHT_STATE))
      whiteState.stateOn = (strcmp(root[JSON_LIGHT_STATE], LIGHT_ON) == 0);

   if (root.containsKey(JSON_LIGHT_BRIGHTNESS))
      whiteState.brightness = root[JSON_LIGHT_BRIGHTNESS];

   whiteState.isFlashing = root.containsKey(JSON_LIGHT_FLASH);

   if (whiteState.isFlashing)
   {
      whiteState.flashLength = (int)root[JSON_LIGHT_FLASH] * 1000;
   }
   else
   {
      if (root.containsKey(JSON_LIGHT_TRANSITION))
         whiteState.transitionSteps = root[JSON_LIGHT_TRANSITION];
      else
         whiteState.transitionSteps = DEFAULT_TRANSITION_STEPS;
   }

   whiteState.startFlash = whiteState.isFlashing;
   whiteState.startFade = !whiteState.isFlashing && (whiteState.transitionSteps > 0);
}

void callback(char* topic, byte* payload, unsigned int length)
{
   char inData[length + 1];
   StaticJsonBuffer<MQTT_BUFFER_SIZE> jsonBuffer;

   for (int i = 0; i < length; i++) {
      inData[i] = (char)payload[i];
   }
   inData[length] = '\0';

   Serial1.print("Topic: ");
   Serial1.println(topic);
   Serial1.print("Payload: ");
   Serial1.println(inData);

   JsonObject& root = jsonBuffer.parseObject(inData);

   if (!root.success())
   {
      Serial1.println("Payload cannot be parsed.");
      return;
   }

   if (strcmp(topic, MQTT_LIGHT_RGB_COMMAND_TOPIC) == 0)
   {
      parseRGBState(m_rgbState, root);
      publishRGBState(m_rgbState, MQTT_LIGHT_RGB_STATE_TOPIC);
   }
   else if (strcmp(topic, MQTT_LIGHT_W1_COMMAND_TOPIC) == 0)
   {
      parseWhiteState(m_white1State, root);
      publishWhiteState(m_white1State, MQTT_LIGHT_W1_STATE_TOPIC);
   }
   else if (strcmp(topic, MQTT_LIGHT_W2_COMMAND_TOPIC) == 0)
   {
      parseWhiteState(m_white2State, root);
      publishWhiteState(m_white2State, MQTT_LIGHT_W2_STATE_TOPIC);
   }

   digitalWrite(BOARD_GREEN_PIN, 0);
   delay(1);
   digitalWrite(BOARD_GREEN_PIN, 1);
}

void reconnect()
{
   // Loop until we're reconnected
   while (!m_client.connected()) {
      Serial1.print("Attempting MQTT connection...");
      // Attempt to connect
      if (m_client.connect(chip_id, mqtt_user, mqtt_password))
      {
         Serial1.println("connected");

         m_client.publish(MQTT_UP, chip_id);

         // Once connected, publish an announcement...
         // publish the initial values
         publishAllStates();

         // ... and resubscribe
         m_client.subscribe(MQTT_LIGHT_RGB_COMMAND_TOPIC);
         m_client.subscribe(MQTT_LIGHT_W1_COMMAND_TOPIC);
         m_client.subscribe(MQTT_LIGHT_W2_COMMAND_TOPIC);

      }
      else {
         Serial1.print("failed, rc=");
         Serial1.print(m_client.state());
         Serial1.println(" try again in 5 seconds");
         // Wait 5 seconds before retrying
         delay(5000);
      }
   }
}

void setup()
{
   // Setup console
   Serial1.begin(115200);
   delay(10);
   Serial1.println();
   Serial1.println();

   m_rgbState.init(RGB_LIGHT_BOARD_RED_PIN, RGB_LIGHT_BOARD_GREEN_PIN, RGB_LIGHT_BLUE_PIN);
   pinMode(m_rgbState.rPin, OUTPUT);
   pinMode(m_rgbState.gPin, OUTPUT);
   pinMode(m_rgbState.bPin, OUTPUT);
   updateColorState(m_rgbState);

   m_white1State.init(W1_PIN);
   pinMode(m_white1State.whitePin, OUTPUT);
   updateWhiteState(m_white1State);

   m_white2State.init(W2_PIN);
   pinMode(m_white2State.whitePin, OUTPUT);
   updateWhiteState(m_white2State);

   pinMode(BOARD_GREEN_PIN, OUTPUT);
   pinMode(BOARD_RED_PIN, OUTPUT);
   digitalWrite(BOARD_RED_PIN, 0);
   digitalWrite(BOARD_GREEN_PIN, 1);

   analogWriteRange(255);

   sprintf(chip_id, "%08X", ESP.getChipId());
   sprintf(myhostname, "esp%08X", ESP.getChipId());

   setupWifi();
}

void loop()
{
   // process OTA updates
   m_httpServer.handleClient();

   m_loopCount++;
   if (!m_client.connected()) {
      reconnect();
   }
   m_client.loop();

   updateColorState(m_rgbState);
   updateWhiteState(m_white1State);
   updateWhiteState(m_white2State);

   // Post the full status to MQTT every 65535 cycles. This is roughly once a minute
   // this isn't exact, but it doesn't have to be. Usually, m_clients will store the value
   // internally. This is only used if a m_client starts up again and did not receive
   // previous messages
   delay(1);
   if (m_loopCount == 0) {
      publishAllStates();
   }
}
#pragma once

const char* mqtt_server = "127.0.0.1";
const int mqtt_port = 1883;
const char* mqtt_user = "user";
const char* mqtt_password = "p4ssw0rd";

// Password for update server
const bool enableOTA = false;
const char* username = "admin";
const char* password = "";

// MQTT topics
// state, brightness, rgb
const char* MQTT_UP = "active";
char* MQTT_LIGHT_RGB_STATE_TOPIC = (char*)"XXXXXXXX/light/rgb";
char* MQTT_LIGHT_RGB_COMMAND_TOPIC = (char*)"XXXXXXXX/light/rgb/set";

char* MQTT_LIGHT_W1_STATE_TOPIC = (char*)"XXXXXXXX/light/w1";
char* MQTT_LIGHT_W1_COMMAND_TOPIC = (char*)"XXXXXXXX/light/w1/set";

char* MQTT_LIGHT_W2_STATE_TOPIC = (char*)"XXXXXXXX/light/w2";
char* MQTT_LIGHT_W2_COMMAND_TOPIC = (char*)"XXXXXXXX/light/w2/set";

char* chip_id = (char*)"00000000";
char* myhostname = (char*)"esp00000000";
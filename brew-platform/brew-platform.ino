/**
   Brouwkuyp Arduino Brew Software
   @author Evert Harmeling <evertharmeling@gmail.com>
*/

/**
   @todo
   - Connecting ArduinoClient to RabbitMQ within docker <hostIP:1883>?
   - Warning: ISO C++ forbids converting a string constant to 'char*' [-Wwrite-strings]
      - char* p = "abc"; // valid in C, invalid in C++
      - char* p = (char*)"abc"; // OK
      - char const *p="abc"; // OK
*/

#include "env.h"
#include <SPI.h>
#include <Wire.h>
#include <OneWire.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <elapsedMillis.h>
#include <DallasTemperature.h>

// Network settings
// Ethernet shield mac address
uint8_t mac[]    =                  {  0x90, 0xA2, 0xDA, 0x0F, 0x6D, 0x90 };
// RabbitMQ IP address
uint8_t server[] =                  { 192, 168, 10, 102 };
// Static client IP, should be in the same subnet as the server IP
// Linksys Router is set up to give out IP's via DHCP from 192.168.10.100 upwards
uint8_t ip[]     =                  { 192, 168, 10, 20 };

#define SENSOR_ADDRESS_LENGTH       8
#define SENSOR_RESOLUTION           9

// Temperature probe addresses
uint8_t sensorHLT[SENSOR_ADDRESS_LENGTH] =  { 16, 186, 176, 76, 2, 8, 0, 183 };  // 10bab04c280b7
uint8_t sensorMLT[SENSOR_ADDRESS_LENGTH] =  { 16, 75, 188, 77, 2, 8, 0, 92 };    // 104bbc4d2805c
uint8_t sensorBLT[SENSOR_ADDRESS_LENGTH] =  { 16, 61, 19, 37, 2, 8, 0, 166 };    // 103d1325280a6
uint8_t sensorEXT[SENSOR_ADDRESS_LENGTH] =  { 16, 151, 228, 36, 2, 8, 0, 77 };   // 1097e4242804d
uint8_t sensorEXT2[SENSOR_ADDRESS_LENGTH] = { 16, 232, 3, 37, 2, 8, 0, 245 };    // 10e80325280f5

// *************************************************************************** //
// *** test purposes, use extra sensors to imitate original HLT + MLT sensors
//uint8_t sensorHLT[SENSOR_ADDRESS_LENGTH] =  { 16, 151, 228, 36, 2, 8, 0, 77 };   // 1097e4242804d
//uint8_t sensorMLT[SENSOR_ADDRESS_LENGTH] = { 16, 232, 3, 37, 2, 8, 0, 245 };     // 10e80325280f5
// *** above 2 lines should not be used in production environment!
// *************************************************************************** //

// Should be a unique identifier to be able to identify the client within the whole infrastructure
#define MQTT_CLIENT                 "bp-fr-arduino-client"
#define MQTT_USER                   SECRET_MQTT_USER
#define MQTT_PASS                   SECRET_MQTT_PASS

// Subscribe topics
// topic format: "brewery/<brewery_name>/<unit>/<action>"
#define TOPIC_MLT_SET_TEMP          "brewery/forestroad/mlt/set_temp"
#define TOPIC_PUMP_SET_MODE         "brewery/forestroad/pump/set_mode"
#define TOPIC_PUMP_SET_STATE        "brewery/forestroad/pump/set_state"

// Publish topics
// topic format: "brewery/<brewery_name>/<unit>/<action>"
#define TOPIC_MLT_CURR_TEMP         "brewery/forestroad/mlt/curr_temp"
#define TOPIC_HLT_CURR_TEMP         "brewery/forestroad/hlt/curr_temp"
#define TOPIC_BLT_CURR_TEMP         "brewery/forestroad/blt/curr_temp"
#define TOPIC_EXT_CURR_TEMP         "brewery/forestroad/ext/curr_temp"
#define TOPIC_EXT2_CURR_TEMP        "brewery/forestroad/ext2/curr_temp"
#define TOPIC_PUMP_CURR_MODE        "brewery/forestroad/pump/curr_mode"
#define TOPIC_PUMP_CURR_STATE       "brewery/forestroad/pump/curr_state"

// Constants
#define PUMP_MODE_AUTOMATIC         "automatic"
#define PUMP_MODE_MANUAL            "manual"
#define PUMP_STATE_ON               "on"
#define PUMP_STATE_OFF              "off"

// Declaration pins
#define PIN_SENSOR_TEMPERATURE      2
#define PIN_RELAIS_HLT_ONE          5
#define PIN_RELAIS_HLT_TWO          6
#define PIN_RELAIS_HLT_THREE        7
#define PIN_RELAIS_PUMP             8
#define PIN_RELAIS_MLT              9
//!!! DO NOT USE PINS 10 + 11 (they are already in use by Ethernet Shield)

// Config settings
#define LOOP_INTERVAL               1000  // milliseconds
#define HYSTERESE                   0.5   // degrees celsius
#define PRECISION                   2     // digits behind comma
#define FLOAT_LENGTH                6     // bytes
#define MAX_HLT_TEMPERATURE         80    // degrees celsius
#define HLT_MLT_HEATUP_DIFF         15    // degrees celsius
#define MLT_HEATUP_DIFF             1     // degrees celsius

void callback(char* topic, byte* payload, unsigned int length);

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(PIN_SENSOR_TEMPERATURE);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
EthernetClient ethClient;
PubSubClient mqttClient(server, 1883, callback, ethClient);

// Initiate variables
elapsedMillis loopTime;
elapsedMillis stepTime;
float tempHLT          = NULL;
float tempMLT          = NULL;
float tempBLT          = NULL;
float tempEXT          = NULL;
float setTempMLT       = NULL;
float setTempHLT       = NULL;
boolean heatUpHLT      = false;
boolean heatUpMLT      = false;
char* pumpMode         = PUMP_MODE_AUTOMATIC;
char* pumpState        = PUMP_STATE_OFF;
// hacky workaround to work with the RELAIS_PIN number as array index
// as the highest PIN number is currently 9, we create an array with 10 items and initialize it with 'off' (false) state
int relaisStates[10]    = {
  false, false, false, false, false, false, false, false, false, false
};

/**
    Callback function to parse MQTT events
*/
void callback(char* topic, byte* payload, unsigned int length)
{
  char value[length + 1];
  snprintf(value, length + 1, "%s", payload);

  if (strcmp(topic, TOPIC_MLT_SET_TEMP) == 0) {
    Serial.println("Topic: MLT set temperature; >" + String(value) + "°C<");
    setTempMLT = atof(value);
  } else if (strcmp(topic, TOPIC_PUMP_SET_STATE) == 0) {
    if (strcmp(value, PUMP_STATE_ON) == 0) {
      Serial.println("Topic: PUMP set STATE; mode: >MANUAL<, state: >ON<");
      pumpMode = PUMP_MODE_MANUAL;
      pumpState = PUMP_STATE_ON;
      switchRelais(PIN_RELAIS_PUMP, true);
    } else if (strcmp(value, PUMP_STATE_OFF) == 0) {
      Serial.println("Topic: PUMP set STATE; mode: >MANUAL<, state: >OFF<");
      pumpMode = PUMP_MODE_MANUAL;
      pumpState = PUMP_STATE_OFF;
      switchRelais(PIN_RELAIS_PUMP, false);
    } else {
      Serial.println("Topic: PUMP set STATE; mode: >AUTOMATIC<");
      pumpMode = PUMP_MODE_AUTOMATIC;
    }
  } else if (strcmp(topic, TOPIC_PUMP_SET_MODE) == 0) {
    if (strcmp(value, PUMP_MODE_AUTOMATIC) == 0) {
      Serial.println("Topic: PUMP set mode >AUTOMATIC<");
      pumpMode = PUMP_MODE_AUTOMATIC;
    } else if (strcmp(value, PUMP_MODE_MANUAL) == 0) {
      Serial.println("Topic: PUMP set mode >MANUAL<");
      pumpMode = PUMP_MODE_MANUAL;
    }
  } else {
//    Serial.println("----------");
//    Serial.print("Unknown / not listening to topic: ");
//    Serial.println(topic);
//    Serial.print("value: ");
//    Serial.println(value);
//    Serial.println("----------");
  }
}

void setup()
{
  Serial.begin(9600);
  Ethernet.begin(mac, ip);
  // give the Ethernet shield 500 milliseconds to initialize...
  delay(500);

  sensors.begin();
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" sensors found");

  // initialize Relais
  pinMode(PIN_RELAIS_HLT_ONE,   OUTPUT);
  pinMode(PIN_RELAIS_HLT_TWO,   OUTPUT);
  pinMode(PIN_RELAIS_HLT_THREE, OUTPUT);
  pinMode(PIN_RELAIS_MLT,       OUTPUT);
  pinMode(PIN_RELAIS_PUMP,      OUTPUT);

  // set initial relais state
  switchRelais(PIN_RELAIS_PUMP, false);
  switchRelais(PIN_RELAIS_MLT, false);
}

void loop()
{
  sensors.requestTemperatures();

  if (connectAndSubscribe()) {

    if (loopTime > LOOP_INTERVAL) {
      handleRecipe();
      publishData();

      loopTime = 0;
    }
  }

  mqttClient.loop();
}

/**
    Handles the set variables, gotten from MQTT messages

    Possible optimization for HLT temp, knowing the max MLT temp (last step), so HLT does not have to get warmer
*/
void handleRecipe()
{
  // make sure we have a set temp for the MLT
  if (setTempMLT != NULL) {
    setTempHLT = setTempMLT + HLT_MLT_HEATUP_DIFF;

    if (setTempHLT > MAX_HLT_TEMPERATURE) {
      setTempHLT = MAX_HLT_TEMPERATURE;
    }

    heatUpHLT = handleHysterese(sensors.getTempC(sensorHLT), setTempHLT, heatUpHLT);
    heatUpMLT = handleHysterese(sensors.getTempC(sensorMLT), setTempMLT, heatUpMLT);

    switchRelais(PIN_RELAIS_HLT_ONE,   heatUpHLT);
    switchRelais(PIN_RELAIS_HLT_TWO,   heatUpHLT);
    switchRelais(PIN_RELAIS_HLT_THREE, heatUpHLT);

    if (pumpMode == PUMP_MODE_AUTOMATIC) {
      switchRelais(PIN_RELAIS_PUMP, heatUpMLT);

      if (heatUpMLT) {
        pumpState = PUMP_STATE_ON;
      } else {
        pumpState = PUMP_STATE_OFF;
      }

      // if pump is active and temp diff is more than MLT_HEATUP_DIFF, use extra heater under MLT
      if (heatUpMLT && (setTempMLT - sensors.getTempC(sensorMLT)) > MLT_HEATUP_DIFF) {
        switchRelais(PIN_RELAIS_MLT, true);
      } else {
        switchRelais(PIN_RELAIS_MLT, false);
      }
    } else {
      switchRelais(PIN_RELAIS_MLT, false);
    }
  }
}

/**
    Publishes all the data
*/
void publishData()
{
  publishFloat(TOPIC_MLT_CURR_TEMP, sensors.getTempC(sensorMLT));
  publishFloat(TOPIC_HLT_CURR_TEMP, sensors.getTempC(sensorHLT));
  publishFloat(TOPIC_BLT_CURR_TEMP, sensors.getTempC(sensorBLT));

  publishString(TOPIC_PUMP_CURR_MODE, pumpMode);
  publishString(TOPIC_PUMP_CURR_STATE, pumpState);

  // extra sensors for testing purposes
  publishFloat(TOPIC_EXT_CURR_TEMP, sensors.getTempC(sensorEXT));
  publishFloat(TOPIC_EXT2_CURR_TEMP, sensors.getTempC(sensorEXT2));
}

/******************
   Help functions
 *****************/

/**
    Connects to MQTT server and subscribes to topic
*/
boolean connectAndSubscribe()
{
  if (!mqttClient.connected()) {
    if (mqttClient.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS)) {
      Serial.println("Connected!");
      mqttClient.subscribe("brewery/#");
      Serial.println("Subscribed!");

      return true;
    } else {
      Serial.println("Unable to connect!");
    }
  }
}

/**
    Uniform way of handling the hysterese check, to prevent switching the relais on / off too fast

    @param float currTemp   Current temperature value
    @param float setTemp    Desired temperature
    @param boolean heating  Boolean to hold current heating status
*/
boolean handleHysterese(float currTemp, float setTemp, boolean heating)
{
  float top = setTemp + HYSTERESE;
  float bottom = setTemp - HYSTERESE;

  if (currTemp >= bottom && currTemp <= top) {
  } else if (currTemp < bottom) {
    heating = true;
  } else if (currTemp > top) {
    heating = false;
  }

  return heating;
}

/**
    Proxy method to publish a value to a topic in MQTT

    @param char* topic
    @param char* value
*/
void publishString(char* topic, char* value)
{
  if (mqttClient.connected()) {
    char* jsonString = buildEvent(topic, value);
    mqttClient.publish(topic, jsonString);
    free(jsonString);
  } else if (connectAndSubscribe()) {
    publishString(topic, value);
  }
}

/**
    Publishes a float to MQTT client

    @param char* topic
    @param float value
*/
void publishFloat(char* topic, float value)
{
  if (value != NULL) {
    char *charTemp = NULL;

    convertTemperature(value, &charTemp);
    publishString(topic, charTemp);
    free(charTemp);
  }
}

/**
    Create JSON event data object, this makes it possible to send more information at once.

    @param char* topic
    @param char* value
*/
char* buildEvent(char* topic, char* value)
{
  char * buff = (char *)malloc(75);
  const char *startTag = "{";
  const char *topicTag = "\"topic\":\"";
  const char *valueTag = "\",\"value\":\"";
  const char *endTag = "\"}";

  strcpy(buff, startTag);
  strcat(buff, topicTag);
  strcat(buff, topic);
  strcat(buff, valueTag);
  strcat(buff, value);
  strcat(buff, endTag);

  return buff;
}

/**
    Converts float to char

    @param float temp
    @param char **charTemp
*/
void convertTemperature(float temp, char **charTemp)
{
  *charTemp = (char *)malloc(sizeof(char) * (FLOAT_LENGTH + 1));
  dtostrf(temp, FLOAT_LENGTH, PRECISION, *charTemp);
}

/**
    Switch relais on (true) / off (false)

    @param int relais     The pin of the relais
    @param boolean state  true / false
*/
void switchRelais(int relaisPin, boolean state)
{
  //Serial.println("Switch: " + String(relaisPin) + ", to state: " + String(state) + ", current state: " + String(relaisStates[relaisPin]));
  
  if (state && !relaisStates[relaisPin]) {
    Serial.println("  Relais: " + String(relaisPin) + "; unit turned >ON<");
    digitalWrite(relaisPin, LOW);
    relaisStates[relaisPin] = 1;
  } else if (!state && relaisStates[relaisPin]) {
    Serial.println("  Relais: " + String(relaisPin) + "; unit turned >OFF<");
    digitalWrite(relaisPin, HIGH);
    relaisStates[relaisPin] = 0;
  } else {
    //Serial.println("  Relais: " + String(relaisPin) + "; no change needed");
  }
}

/**
    Trims char array

    @param char* value
*/
char* trim(char* value)
{
  char *s = value - 1;
  char *e = value + strlen (value);

  while (++s < e && *s < 33);
  while (--e > s && *e < 33);
  *(++e) = (char) 0;

  return s;
}

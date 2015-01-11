/**
 * Brouwkuyp Arduino Brew Software
 * @author Evert Harmeling <evertharmeling@gmail.com>
 */
 
/**
 * @todo
 */

#include <SPI.h>
#include <Wire.h>
#include <OneWire.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <elapsedMillis.h>

// Network settings
byte mac[]    =                     {  0x90, 0xA2, 0xDA, 0x0F, 0x6D, 0x90 };
byte server[] =                     { 192, 168, 2, 132 };
//byte server[] =                     { 192, 168, 2, 114 };
byte ip[]     =                     { 192, 168, 2, 150 };

// Temperature probe addresses
#define SENSOR_HLT                  "10bab04c280b7"
#define SENSOR_MLT                  "104bbc4d2805c"
#define SENSOR_BLT                  "103d1325280a6"
#define SENSOR_EXT                  "1097e4242804d"

#define ARDUINO_CLIENT              "brouwkuypArduinoClient"

// Subscribe topics
#define TOPIC_MASHER_SET_TEMP       "brewery/brewhouse01/masher/set_temp"
#define TOPIC_PUMP_SET_MODE         "brewery/brewhouse01/pump/set_mode"
#define TOPIC_PUMP_SET_STATE        "brewery/brewhouse01/pump/set_state"

// Publish topics
#define TOPIC_MASHER_CURR_TEMP      "brewery/brewhouse01/masher/curr_temp"
#define TOPIC_MASHER_MLT_CURR_TEMP  "brewery/brewhouse01/masher/mlt/curr_temp"
#define TOPIC_MASHER_HLT_CURR_TEMP  "brewery/brewhouse01/masher/hlt/curr_temp"
#define TOPIC_BOILER_CURR_TEMP      "brewery/brewhouse01/boiler/curr_temp"
#define TOPIC_EXT_CURR_TEMP         "brewery/brewhouse01/ext/curr_temp"
#define TOPIC_PUMP_CURR_MODE        "brewery/brewhouse01/pump/curr_mode"
#define TOPIC_PUMP_CURR_STATE       "brewery/brewhouse01/pump/curr_state"

// constants
#define PUMP_MODE_AUTOMATIC         "automatic"
#define PUMP_MODE_MANUAL            "manual"
#define PUMP_STATE_ON               "on"
#define PUMP_STATE_OFF              "off"

// Declaration pins
#define PIN_SENSOR_TEMPERATURE      2
#define PIN_RELAIS_HLT_ONE          5
#define PIN_RELAIS_HLT_TWO          6
#define PIN_RELAIS_HLT_THREE        7
// empty place for free relais      8
#define PIN_RELAIS_PUMP             9
#define PIN_RELAIS_MLT              10

// Config settings
#define LOOP_INTERVAL               1000  // milliseconds
#define HYSTERESE                   0.5   // degrees celsius
#define PRECISION                   2     // digits behind comma
#define FLOAT_LENGTH                6     // bytes
#define MAX_HLT_TEMPERATURE         80    // degrees celsius
#define HLT_MLT_HEATUP_DIFF         15    // degrees celsius
#define MLT_HEATUP_DIFF             1     // degrees celsius

void callback(char* topic, byte* payload, unsigned int length);

OneWire ds(PIN_SENSOR_TEMPERATURE);
EthernetClient ethClient;
PubSubClient client(server, 1883, callback, ethClient);

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

/**
 *  Callback function to parse MQTT events
 */
void callback(char* topic, byte* payload, unsigned int length) 
{
    char value[length + 1];
    snprintf(value, length + 1, "%s", payload);
  
    if (strcmp(topic, TOPIC_MASHER_SET_TEMP) == 0) {
        setTempMLT = atof(value);
    } else if (strcmp(topic, TOPIC_PUMP_SET_STATE) == 0) {
        if (strcmp(value, PUMP_STATE_ON) == 0) {
            pumpMode = PUMP_MODE_MANUAL;
            pumpState = PUMP_STATE_ON;
            switchRelais(PIN_RELAIS_PUMP, true);
        } else if (strcmp(value, PUMP_STATE_OFF) == 0) {
            pumpMode = PUMP_MODE_MANUAL;
            pumpState = PUMP_STATE_OFF;
            switchRelais(PIN_RELAIS_PUMP, false);
        } else {
            pumpMode = PUMP_MODE_AUTOMATIC;
        }
    } else if (strcmp(topic, TOPIC_PUMP_SET_MODE) == 0) {
        if (strcmp(value, PUMP_MODE_AUTOMATIC) == 0) {
            pumpMode = PUMP_MODE_AUTOMATIC;
        } else if (strcmp(value, PUMP_MODE_MANUAL) == 0) {
            pumpMode = PUMP_MODE_MANUAL;
        }
    } else {
//        Serial.println("----------");
//        Serial.print("Unknown / not listening to topic: ");
//        Serial.println(topic);
//        Serial.print("value: ");
//        Serial.println(value);
//        Serial.println("----------");
    }
}

void setup()
{
    Serial.begin(9600);
    Ethernet.begin(mac, ip);
  
    // initialize Relais
    pinMode(PIN_RELAIS_HLT_ONE,   OUTPUT);
    pinMode(PIN_RELAIS_HLT_TWO,   OUTPUT);
    pinMode(PIN_RELAIS_HLT_THREE, OUTPUT);
    pinMode(PIN_RELAIS_MLT,       OUTPUT);
    pinMode(PIN_RELAIS_PUMP,      OUTPUT);
}

void loop()
{ 
    readTemperatures();
    
    if (connectAndSubscribe())
    {
        if (loopTime > LOOP_INTERVAL) {
            publishData();
            handleRecipe();
            
            loopTime = 0;
        }
    }
 
    client.loop();
}

/**
 *  Handles the set variables, gotten from MQTT messages
 *
 *  Possible optimization for HLT temp, knowing the max MLT temp (last step), so HLT does not have to get warmer
 */
void handleRecipe() 
{
    // make sure we have a set temp for the MLT
    if (setTempMLT != NULL) {
        setTempHLT = setTempMLT + HLT_MLT_HEATUP_DIFF;
        
        if (setTempHLT > MAX_HLT_TEMPERATURE) {
            setTempHLT = MAX_HLT_TEMPERATURE;
        }
        
        heatUpHLT = handleHysterese(tempHLT, setTempHLT, heatUpHLT);
        heatUpMLT = handleHysterese(tempMLT, setTempMLT, heatUpMLT);
        
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
            if (heatUpMLT && (setTempMLT - tempMLT) > MLT_HEATUP_DIFF) {
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
  * Publishes all the data
  */
void publishData() 
{    
    publishFloat(TOPIC_MASHER_CURR_TEMP, tempMLT);
    publishFloat(TOPIC_MASHER_MLT_CURR_TEMP, tempMLT);
    publishFloat(TOPIC_MASHER_HLT_CURR_TEMP, tempHLT);
    publishFloat(TOPIC_BOILER_CURR_TEMP, tempBLT);
    publishFloat(TOPIC_EXT_CURR_TEMP, tempEXT);
    publishString(TOPIC_PUMP_CURR_MODE, pumpMode);
    publishString(TOPIC_PUMP_CURR_STATE, pumpState);
}

/******************
 * Help functions *
 *****************/

/**
 *  Connects to MQTT server and subscribes to topic
 */
boolean connectAndSubscribe() 
{
    if (!client.connected()) {
        if (client.connect(ARDUINO_CLIENT)) {
            Serial.println("Connected!");
            client.subscribe("brewery/#");
            Serial.println("Subscribed!");
            
            return true;
        } else {
            Serial.println("Unable to connect!");
        }
    }
}

/**
 *  Uniform way of handling the hysterese check, to prevent switching the relais too fast on/off
 *
 *  @param float currTemp   Current temperature value
 *  @param float setTemp    Desired temperature
 *  @param boolean heating  Boolean to hold current heating status
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
 *  Proxy method to publish a value to a topic in MQTT
 *
 *  @param char* topic
 *  @param char* value
 */
void publishString(char* topic, char* value) 
{
    if (client.connected()) {
        client.publish(topic, trim(value));
    } else if (connectAndSubscribe()) {
        publishString(topic, value);
    }
}

/**
 *  Publishes a float to MQTT client
 *
 *  @param char* topic
 *  @param float value
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
 *  Reads the temperature of the sensors, and stores these in `temp` variable and `sensor` variable
 *  @todo optimize sensor search, use predefined ids to get value
 */
boolean readTemperatures() 
{
    byte i;
    byte data[12];
    byte addr[8];
    String sensor = NULL;
  
    if (!ds.search(addr)) {
        ds.reset_search();
        delay(250);
        return false;
    }

    if (OneWire::crc8(addr, 7) != addr[7]) {
        return false;
    }

    ds.reset();
    ds.select(addr);
    for ( i = 0; i < 8; i++) {
        sensor += String(addr[i], HEX);
    }

    ds.write(0x44, 1);
  
    ds.reset();
    ds.select(addr);    
    ds.write(0xBE);

    for ( i = 0; i < 9; i++) {
        data[i] = ds.read();
    }

    int16_t raw = (data[1] << 8) | data[0];
    raw = raw << 3;
    if (data[7] == 0x10) {
        raw = (raw & 0xFFF0) + 12 - data[6];
    } 
    
    float temp = (float) raw / 16.0;
    
    if (sensor == SENSOR_HLT) {
        tempHLT = temp;
    } else if (sensor == SENSOR_MLT) {
        tempMLT = temp;
    } else if (sensor == SENSOR_BLT) {
        tempBLT = temp;
    } else if (sensor == SENSOR_EXT) {
        tempEXT = temp;
        tempHLT = temp; // test purposes because of single probe
        tempMLT = temp; // test purposes because of single probe
        tempBLT = temp; // test purposes because of single probe
    }
    
    return true;
}

/**
 *  Converts float to char
 *
 *  @param float temp
 *  @param char **charTemp
 */
void convertTemperature(float temp, char **charTemp) 
{
    *charTemp = (char *)malloc(sizeof(char) * (FLOAT_LENGTH + 1));
    dtostrf(temp, FLOAT_LENGTH, PRECISION, *charTemp);
}

/**
 *  Switch relais on (true) / off (false)
 *
 *  @param int relais     The pin of the relais
 *  @param boolean state  true / false
 */
void switchRelais(int relais, boolean state) 
{
    if (state) {
//        Serial.println("Set relais " + String(relais) + " ON");
        digitalWrite(relais, LOW);
    } else {
//        Serial.println("Set relais " + String(relais) + " OFF");
        digitalWrite(relais, HIGH);
    }
}

/**
 * Trims char array
 */
char* trim(char* value) 
{
    char *s = value - 1, *e = value + strlen (value);

    while (++s < e && *s < 33);
    while (--e > s && *e < 33);
    *(++e) = (char) 0;

    return s;
}

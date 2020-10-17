/* 
Windvane

   Author :             F. Guiet 
   Creation           : 20200324
   Last modification  : 
  
  Version            : 1.0
  History            : 1.0 - First version
                       1.1 - Replaced AMS117 LDO regulator to HT7333 LDO (1V dropout of AMS117 was too much when powered ESP32 via 5v pin with 3.7v lithium battery) 
                             => the 3.3v pin displayed 2.8v ... when battery was 3.9v :( with HT7333 it's much better...but wareful it can deliver only 250mA max) 
                       1.2 - 2020.04.01 - Read voltage correction. 
                       1.3 - 2020.10.17 - Take "Unknown value into account...otherwise it could drain the battery when reedwitch are ko  
                       1.4 - 2020.10.17 - Switch to PlatformIO, replace ESP32 Dev Kit          
                       
References :   

ESP32 Dev kit : https://randomnerdtutorials.com/getting-started-with-esp32/

Deep sleep mode consumption (ESP32 Devkit) : 7.3mA 
Wake up mode (ESP32 Devkit)                : 63mA

*/
#include <Arduino.h>
#include <ArduinoJson.h>
//Light Mqtt library
#include <PubSubClient.h>
//Wifi library
#include <WiFi.h>

#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems

#define MQTT_SERVER "192.168.1.25"
// WiFi settings
const char* ssid = "DUMBLEDORE";
const char* password = "frederic";

#define MQTT_CLIENT_ID "WindvaneSensor"
#define MAX_RETRY 500

WiFiClient espClient;
PubSubClient client(espClient);

#define DEBUG 0
#define FIRMWARE_VERSION "1.2"

int counter = 0;
int samples = 9;
unsigned long last_millis = 0;
const int get_direction_frequency = 5000; //10s = 10000
char message_buff[200];

String directions[] = { "South", "South-South-East", "South-East","East-South-East", "East","East-North-East", "North-East","North-North-East","North","North-North-West", "North-West","West-North-West","West","West-South-West","South-West","South-South-West" };


// one hour = 3600 s
// half hour = 1800 s
// quarter hour = 900 s
#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  900        /* Time ESP32 will go to sleep (in seconds) */

const int WINDIRSENSOR_PIN = 34;
const int BATTERYSENSOR_PIN = 33;
const int LED_PIN = 2;

DynamicJsonDocument doc(500);
JsonObject winddir = doc.to<JsonObject>();

struct Sensor {
    String Name;    
    String SensorId;
    String Mqtt_topic;
};

#define SENSORS_COUNT 1
Sensor sensors[SENSORS_COUNT];

void debug_message(String message, bool doReturnLine) {
  if (DEBUG) {
    if (doReturnLine) {
      //mySerial.println(message);
      Serial.println(message);
    }
    else {
      //mySerial.println(message);
      Serial.print(message);
    }

    //Be sure everything is printed on screen!
    Serial.flush();
  }
}

void disconnectMqtt() {
  debug_message("Disconnecting from mqtt...", true);
  client.disconnect();
}

void disconnectWifi() {
  debug_message("Disconnecting from wifi...", true);
  WiFi.disconnect();
}

boolean connectToMqtt() {

   client.setServer(MQTT_SERVER, 1883); 

  int retry = 0;
  // Loop until we're reconnected
  while (!client.connected() && retry < MAX_RETRY) {
    debug_message("Attempting MQTT connection...", true);
    
    if (client.connect(MQTT_CLIENT_ID)) {
      debug_message("connected to MQTT Broker...", true);
    } else {
      retry++;
      // Wait 5 seconds before retrying
      delay(500);
      //yield();
    }
  }

  if (retry >= MAX_RETRY) {
    debug_message("MQTT connection failed...", true);  
    return false;
  }

  return true;
}

boolean connectToWifi() {
  
  debug_message("Connecting to WiFi...", true);
  
  int retry = 0;

  WiFi.setHostname(MQTT_CLIENT_ID);
  WiFi.begin(ssid, password);

  debug_message("Begin WiFi...", true);

  
  while (WiFi.status() != WL_CONNECTED && retry < MAX_RETRY) {
    retry++;
    delay(500);
    debug_message(".", false);
  }

  if (WiFi.status() == WL_CONNECTED) {  
     debug_message("WiFi connected", true);  
     // Print the IP address
     if (DEBUG) {
      Serial.println(WiFi.localIP());
     }
     
     return true;
  } else {
    debug_message("WiFi connection failed...", true);   
    return false;
  }  
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : debug_message("Wakeup caused by external signal using RTC_IO", true); break;
    case ESP_SLEEP_WAKEUP_EXT1 : debug_message("Wakeup caused by external signal using RTC_CNTL", true); break;
    case ESP_SLEEP_WAKEUP_TIMER : debug_message("Wakeup caused by timer", true); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : debug_message("Wakeup caused by touchpad", true); break;
    case ESP_SLEEP_WAKEUP_ULP : debug_message("Wakeup caused by ULP program", true); break;
    default : debug_message("Wakeup was not caused by deep sleep", true); break;
  }
}

void InitSensors() {
  
  sensors[0].Name = "Windvane";
  sensors[0].SensorId = "21";
  sensors[0].Mqtt_topic = "guiet/outside/sensor/21";
}

void setup() {
  // put your setup code here, to run once:
  pinMode(WINDIRSENSOR_PIN, INPUT);
  pinMode(BATTERYSENSOR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  //Turn off LED (save some mA)
  digitalWrite(LED_PIN, HIGH);
  
   // Initialize Serial Port
  if (DEBUG) {
    Serial.begin(9600);
    print_wakeup_reason();
  }

  InitSensors();

  last_millis = millis();

  uint32_t brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG); //save WatchDog register
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  debug_message("Setup completed, starting...",true); 
}

float ReadVoltage() {

  //AnalogRead = 850 pour 4.2v

  //R1 = 33kOhm
  //R2 = 7.5kOhm

  float sensorValue = 0.0f;  
  
  delay(100); //Tempo so analog reading will be correct!
  sensorValue = analogRead(BATTERYSENSOR_PIN);
    
  debug_message("Analog Reading : " + String(sensorValue,2), true);

  return (sensorValue * 4.20) / 850;

}

float getDegree(String direction) {
  
  if (direction=="South") {
    return 180;
  }

  if (direction=="South-South-East") {
    return 157.5;
  }

  if (direction=="South-East") {
    return 135;
  }

  if (direction=="East-South-East") {
    return 112.5;
  }

  if (direction=="East") {
    return 90;
  }

  if (direction=="East-North-East") {
    return 67.5;
  }

  if (direction=="North-East") {
    return 45;
  }

  if (direction=="North-North-East") {
    return 22.5;
  }

  if (direction=="North") {
    return 0;
  }

  if (direction=="North-North-West") {
    return 337.5;
  }

  if (direction=="North-West") {
    return 315;
  }

  if (direction=="West-North-West") {
    return 292.5;
  }

  if (direction=="West") {
    return 270;
  }

  if (direction=="West-South-West") {
    return 247.5;
  }

  if (direction=="South-West") {
    return 225;
  }

  if (direction=="South-South-West") {
    return 202.5;
  }

  return -1;
}

String ConvertToJSon(String battery, String dirWin) {
    //Create JSon object
    DynamicJsonDocument  jsonBuffer(200);
    JsonObject root = jsonBuffer.to<JsonObject>();
    
    root["id"] = sensors[0].SensorId;
    root["name"] = sensors[0].Name;
    root["firmware"]  = FIRMWARE_VERSION;
    root["battery"] = battery;    
    root["winddirection"] = dirWin;
    root["degree"] = String(getDegree(dirWin),1);
    
    String result;    
    serializeJson(root, result);

    return result;
}

void SendResult() {

   if (WiFi.status() != WL_CONNECTED) {
    if (!connectToWifi())
      return;
  }  

  if (!client.connected()) {
    if (!connectToMqtt()) {
      return;
    }
  }

  int winner = 0;
  String dirWin = "Unknown";
  for (int i=0;i<16;i++) {
    if (winddir.containsKey(directions[i])) {
      if (winddir[directions[i]].as<int>() > winner) {
        dirWin=directions[i];
        winner=winddir[directions[i]].as<int>();
      }
    }    
  }

  debug_message("Winner is : " + dirWin, true);

  float vin = ReadVoltage();
  String mess = ConvertToJSon(String(vin,2), dirWin);
  debug_message("JSON Sensor : " + mess + ", topic : " +sensors[0].Mqtt_topic, true);
  mess.toCharArray(message_buff, mess.length()+1);
    
  client.publish(sensors[0].Mqtt_topic.c_str(),message_buff);

  disconnectMqtt();
  delay(100);
  disconnectWifi();
  delay(100);
}

String getWindDirection(float voltage) {

  if (voltage >= 465 and voltage <= 520) {
    return directions[0]; //South
  }

  if (voltage >= 400 and voltage <= 460) {
    return directions[1] ; //South-South-East    
  }

  if (voltage >= 2240 and voltage <= 2310) {
    return directions[2] ; //"South-East"    
  }
  
  if (voltage >= 2050 and voltage <= 2110) {
    return directions[3];  //"East-South-East";    
  }

  if (voltage >= 3390 and voltage <= 3480) {
    return directions[4]; //"East";    
  }

  if (voltage >= 3080 and voltage <= 3150) {
    return directions[5]; //"East-North-East";    
  }

  if (voltage >= 3800 and voltage <= 3880) {
    return directions[6]; //"North-East";
  }

  if (voltage >= 3520 and voltage <= 3600) {
    return directions[7]; //"North-North-East";
  }

  if (voltage >= 4000 and voltage <= 4096) {
    return directions[8]; //"North";
  }

  if (voltage >= 2690 and voltage <= 2750) {
    return directions[9]; //"North-North-West";
  }

  if (voltage >= 2770 and voltage <= 2850) {
    return directions[10]; //"North-West";
  }

  if (voltage >= 1900 and voltage <= 1980) {
    return directions[11]; //"West-North-West";
  }

  if (voltage >= 2300 and voltage <= 2400) {
    return directions[12]; //"West";
  }

  if (voltage >= 840 and voltage <= 900) {
    return directions[13]; //"West-South-West";
  }

  if (voltage >= 1010 and voltage <= 1130) {
    return directions[14]; //"South-West";
  }
  
  if (voltage >= 280 and voltage <= 350) {
    return directions[15]; //"South-South-West";
  }

  return "Unknown";
}

void loop() {

  /*ReadVoltage();

  delay(200);

  return;*/
  
  
  //Get Wind direction every get_direction_frequency ms
  if (millis() - last_millis >= get_direction_frequency ) { 

    debug_message("Reading number : " + String(counter), true);  

    float voltage = analogRead(WINDIRSENSOR_PIN);  
    debug_message("Analog Reading : " + String(voltage,2), true);  
    String windDirection = getWindDirection(voltage);
    debug_message("Wind direction : " + windDirection, true);  
    
    //if (windDirection != "Unknown" ) {
      counter++;

      if (winddir.containsKey(windDirection)) {
        winddir[windDirection]=winddir[windDirection].as<int>()+1;
      }
      else {
        winddir[windDirection]=1;
      }
    //}

    last_millis = millis();
  }

  //For testing purpose
  //return;

  if (counter >= samples) {
    SendResult();
    debug_message("Going to sleep...na night", true);

    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    debug_message("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds", true);
    
    esp_deep_sleep_start();
  }
}






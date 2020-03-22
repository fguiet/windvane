const int VOLTAGE_PIN = A0;
#define DEBUG 1

void setup() {
  // put your setup code here, to run once:
  pinMode(VOLTAGE_PIN, INPUT);
  
  Serial.begin(115200);
}

void loop() {
  // put your main code here, to run repeatedly:
  float sensorValue = analogRead(VOLTAGE_PIN);

  debug_message("Analog Reading : " + String(sensorValue,2), true);

  delay(300);
}

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
  }
}

string getWindDirection(float voltage) {

  //516
  if (voltage >= 506 and voltage <= 526) {
    return "East";
  }

  //181
  if (voltage >= 171 and voltage <= 191) {
    return "East-North-East";
  }

  //210
  if (voltage >= 200 and voltage <= 210) {
    return "North-East";
  }
  
  //90
  if (voltage >= 80 and voltage <= 100) {
    return "North-North-East";
  }

  //117
  if (voltage >= 107 and voltage <= 127) {
    return "North";
  }

  //109
  if (voltage >= 107 and voltage <= 127) {
    return "North";
  }
  
}

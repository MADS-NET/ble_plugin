/*
  ____  _     _____   _                             
 | __ )| |   | ____| | | ___   __ _  __ _  ___ _ __ 
 |  _ \| |   |  _|   | |/ _ \ / _` |/ _` |/ _ \ '__|
 | |_) | |___| |___  | | (_) | (_| | (_| |  __/ |   
 |____/|_____|_____| |_|\___/ \__, |\__, |\___|_|   
                              |___/ |___/           
Logs analog inputs to BLE characteristics
*/
#include <ArduinoBLE.h>

#define TIMESTEP 1 // time between readings in milliseconds
#define THRESHOLD 100
#define BASE_UUID 0x6933
#define UUID_A "ef91a0af-"
#define UUID_B "-43cb-8988-46f54772cfc7"
#define ANALOG_CHARS_LEN 6
#define BOOL_CHARS_PORTS {D4, D5, D6, D7, D8}
#define BOOL_CHARS_LEN 5

String uuid_p1("ef91a0af-");
String uuid_p2("-43cb-8988-46f54772cfc7");

BLEService *Service;
BLEUnsignedIntCharacteristic *AnalogChars[ANALOG_CHARS_LEN];
BLEBooleanCharacteristic *BoolChars[BOOL_CHARS_LEN];
BLEUnsignedIntCharacteristic *ThresholdChar;

unsigned int DigitalPorts[] = BOOL_CHARS_PORTS;
unsigned int Threshold = THRESHOLD;
bool BoolValues[BOOL_CHARS_LEN];

const int ledPin = 2;
long previousMillis = 0;

// Make a UUID by incrementing the second part of a standard UUID
String make_uuid(unsigned int i) {
  String out(UUID_A);
  char buf[5];
  sprintf(buf, "%04x", i);
  out.concat(buf);
  out.concat(UUID_B);
  return out;
}


void setup() {
  Serial.begin(9600);    // initialize serial communication

  // PINS
  analogReadResolution(14);
  pinMode(LED_BUILTIN, OUTPUT); // initialize the built-in LED pin to indicate when a central is connected
  pinMode(ledPin, OUTPUT); // initialize the built-in LED pin to indicate when a central is connected
  pinMode(2, INPUT_PULLDOWN);

  // pin2.writeValue(true);

  // BLE
  //initialize ArduinoBLE library
  if (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy failed!");
    while (true);
  }

  // BLE elements
  // Service
  Service = new BLEService(make_uuid(BASE_UUID).c_str());
  BLE.setLocalName("Arduino"); //Setting a name that will appear when scanning for Bluetooth® devices
  BLE.setDeviceName("Arduino"); //Setting a name that will appear when scanning for Bluetooth® devices
  BLE.setAdvertisedService(*Service);

  // Analog characteristics
  for (int i = 0; i < ANALOG_CHARS_LEN; i++) {
    AnalogChars[i] = new BLEUnsignedIntCharacteristic(make_uuid(BASE_UUID + i + 1).c_str(), BLERead);
    Service->addCharacteristic(*AnalogChars[i]);
    AnalogChars[i]->writeValue(0);
  }

  // Digital characteristics
  for (int i = 0; i < BOOL_CHARS_LEN; i++) {
    BoolChars[i] = new BLEBooleanCharacteristic(make_uuid(BASE_UUID + i + 1 + ANALOG_CHARS_LEN).c_str(), BLERead | BLENotify);
    Service->addCharacteristic(*BoolChars[i]);
    BoolChars[i]->writeValue(false);
    pinMode(DigitalPorts[i], INPUT_PULLDOWN);
    BoolValues[i] = false;
  }

  ThresholdChar = new BLEUnsignedIntCharacteristic(make_uuid(BASE_UUID + ANALOG_CHARS_LEN + BOOL_CHARS_LEN + 1).c_str(), BLEWrite | BLERead);
  // ThresholdChar->subscribe();
  Service->addCharacteristic(*ThresholdChar);


  // assign event handlers for connected, disconnected to peripheral
  BLE.setEventHandler(BLEConnected, connectHandler);
  BLE.setEventHandler(BLEDisconnected, disconnectHandler);

  // Instantiating and advertising service
  BLE.addService(*Service);  // adding the service
  Serial.println("Bluetooth® device active, waiting for connections...");
  BLE.advertise(); //start advertising the service
}


void connectHandler(BLEDevice central) {
  // central connected event handler
  Serial.print("Connected event, central: ");
  Serial.print(central.address());
  Serial.print(" - ");
  Serial.print(Threshold);
  Serial.println(central.deviceName());
  digitalWrite(LED_BUILTIN, HIGH);
}

void disconnectHandler(BLEDevice central) {
  // central disconnected event handler
  Serial.print("Disconnected event, central: ");
  Serial.print(central.address());
  Serial.print(" - ");
  Serial.println(central.deviceName());
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  BLEDevice central = BLE.central(); // wait for a Bluetooth® Low Energy central

  if (central) {  // if a central is connected to the peripheral
    while (central.connected()) {
      for (int i = 0; i < ANALOG_CHARS_LEN; i++) {
        auto v = analogRead(i);
        if (v > Threshold) {
          AnalogChars[i]->writeValue(v);
        }
        else {
          AnalogChars[i]->writeValue(0);
        }
      }

      for (int i = 0; i < BOOL_CHARS_LEN; i++) {
        bool v = digitalRead(DigitalPorts[i]);
        if (v != BoolValues[i]) {
          BoolChars[i]->writeValue(v);
          BoolValues[i] = v;
        }
      }

      if (ThresholdChar->written()) {
        Threshold = ThresholdChar->value();
        Serial.print("Threshold set to ");
        Serial.println(Threshold);
      }
    }
  }
}
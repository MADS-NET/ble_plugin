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

#define TIMESTEP 50 // time between readings in milliseconds
#define THRESHOLD 100
BLEService newService("180A"); // creating the service

BLEUnsignedCharCharacteristic analog1("2A57", BLERead); // creating the Analog Value characteristic
BLEUnsignedCharCharacteristic analog2("2A58", BLERead); // creating the Analog Value characteristic
BLEBooleanCharacteristic pin2("2A59", BLERead | BLENotify);

const int ledPin = 2;
long previousMillis = 0;


void setup() {
  Serial.begin(9600);    // initialize serial communication
  while (!Serial);       //starts the program if we open the serial monitor.

  analogReadResolution(14);

  pinMode(LED_BUILTIN, OUTPUT); // initialize the built-in LED pin to indicate when a central is connected
  pinMode(ledPin, OUTPUT); // initialize the built-in LED pin to indicate when a central is connected
  pinMode(2, INPUT_PULLDOWN);

  //initialize ArduinoBLE library
  if (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy failed!");
    while (1);
  }

  BLE.setLocalName("Arduino"); //Setting a name that will appear when scanning for Bluetooth® devices
  BLE.setAdvertisedService(newService);

  newService.addCharacteristic(analog1); //add characteristics to a service
  newService.addCharacteristic(analog2);
  newService.addCharacteristic(pin2);

  BLE.addService(newService);  // adding the service

  analog1.writeValue(0); //set initial value for characteristics
  analog2.writeValue(0);
  pin2.writeValue(true);

  BLE.advertise(); //start advertising the service
  Serial.println(" Bluetooth® device active, waiting for connections...");
}

void loop() {
  BLEDevice central = BLE.central(); // wait for a Bluetooth® Low Energy central

  if (central) {  // if a central is connected to the peripheral
    Serial.print("Connected to central: ");
    
    Serial.println(central.address()); // print the central's BT address
    
    digitalWrite(LED_BUILTIN, HIGH); // turn on the LED to indicate the connection

    // check the battery level every TIMESTEP ms
    // while the central is connected:
    static PinStatus pin2Value = HIGH;
    while (central.connected()) {
      PinStatus v = digitalRead(D2);
      long currentMillis = millis();
      
      // polling happens at TIMESTEP:
      if (currentMillis - previousMillis >= TIMESTEP) { 
        previousMillis = currentMillis;
        int v1 = analogRead(A1);
        int v2 = analogRead(A2);
        if (v1 > THRESHOLD)
          analog1.writeValue(v1);
        else
          analog1.writeValue(0);
        if (v2 > THRESHOLD)
          analog2.writeValue(v2);
        else
          analog2.writeValue(0);
      }

      // this runs at full speed, so the notification is immediate:
      if (pin2Value != v) {
        Serial.println(v ? "ON" : "OFF");
        pin2.writeValue(v);
        pin2Value = v;
      }
    }
    
    digitalWrite(LED_BUILTIN, LOW); // when the central disconnects, turn off the LED
    Serial.print("Disconnected from central: ");
    Serial.println(central.address());
  }
}
/*
  SD card basic file example

  This example shows how to create and destroy an SD card file.
  The circuit. Pin numbers reflect the default CI13XX GPIO software SPI pins:
   SD card attached to SPI bus as follows:
 ** SDO / MISO - MISO (PA2, pin 2)
 ** SDI / MOSI - MOSI (PA4, pin 4)
 ** CLK / SCK  - SCK  (PA5, pin 5)
 ** CS         - SS   (PA3, pin 3)

  created   Nov 2010
  by David A. Mellis
  modified 24 July 2020
  by Tom Igoe

  This example code is in the public domain.
*/
#include <SD.h>

const int chipSelect = SS;
File myFile;

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  // wait for Serial Monitor to connect. Needed for native USB port boards only:
while (!Serial);

  Serial.print("Initializing SD card...");

  if (!SD.begin(chipSelect)) {
    Serial.println("initialization failed. Things to check:");
    Serial.println("1. is a card inserted?");
    Serial.println("2. is your wiring correct?");
    Serial.println("3. did you change the chipSelect pin to match your shield or module?");
    Serial.println("Note: press reset button on the board and reopen this serial monitor after fixing your issue!");
    while (1);
  }
  Serial.println("initialization done.");

  if (SD.exists("example.txt")) {
    Serial.println("example.txt exists.");
  } else {
    Serial.println("example.txt doesn't exist.");
  }

  // open a new file and immediately close it:
  Serial.println("Creating example.txt...");
  myFile = SD.open("example.txt", FILE_WRITE);
  myFile.close();

  // Check to see if the file exists:
  if (SD.exists("example.txt")) {
    Serial.println("example.txt exists.");
  } else {
    Serial.println("example.txt doesn't exist.");
  }

  // delete the file:
  Serial.println("Removing example.txt...");
  SD.remove("example.txt");

  if (SD.exists("example.txt")) {
    Serial.println("example.txt exists.");
  } else {
    Serial.println("example.txt doesn't exist.");
  }
}

void loop() {
  // nothing happens after setup finishes.
}

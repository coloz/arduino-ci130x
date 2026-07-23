/*
  SD card datalogger

  This example shows how to log data from an analog sensor
  to an SD card using the SD library.

  The circuit:
   analog sensor on A0
   SD card attached to SPI bus as follows:
 ** SDO / MISO - MISO (PA2, pin 2)
 ** SDI / MOSI - MOSI (PA4, pin 4)
 ** CLK / SCK  - SCK  (PA5, pin 5)
 ** CS         - SS   (PA3, pin 3)

  created  24 Nov 2010
  modified  24 July 2020
  by Tom Igoe

  This example code is in the public domain.

*/

#include <SPI.h>
#include <SD.h>

const int chipSelect = SS;

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
    Serial.println("Note: press reset button on the board and reopen this Serial Monitor after fixing your issue!");
    while (true);
  }

  Serial.println("initialization done.");
}

void loop() {
  // make a string for assembling the data to log:
  String dataString = "";

  // A0 is available on every CI13XX variant.
  dataString += String(analogRead(A0));

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open("datalog.txt", FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(dataString);
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog.txt");
  }
}

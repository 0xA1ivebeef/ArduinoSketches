
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#define CSN 4
#define CE 5

BLEScan* pBLEScan;

void setup() {
  Serial.begin(115200);
  BLEDevice::init("");  // Initialize BLE
  pBLEScan = BLEDevice::getScan();  // Create BLE scan object
  pBLEScan->setActiveScan(true);    // Active scanning to get more information
  pBLEScan->setInterval(1000);     // Set scan interval
  pBLEScan->setWindow(1000);       // Set scan window
}

void loop() {
  BLEScanResults* foundDevices = pBLEScan->start(5);  // Scan for 5 seconds
  Serial.println("Scan done!");
  Serial.println(foundDevices->getCount());
  pBLEScan->clearResults();  // Clear results to prevent duplicates
  delay(5000);  // Wait before the next scan
}

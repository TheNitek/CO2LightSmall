// ensure this library description is only included once
#pragma once

#include "Arduino.h"
#include <Wire.h>
#include <Print.h>
#include "Stream.h"

class SensorS8
{
  public:
    bool debug = false;

    void begin(Stream *serial, Stream *debugSerial);
    int getCO2();
    int getABCPeriod();
    bool setABCPeriod();

  private:
    Stream *_serial;
    Stream *_debugSerial;
    uint16_t _crc16(byte * buf, uint8_t len);
};
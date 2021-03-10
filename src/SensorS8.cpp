#include "SensorS8.h"

void SensorS8::begin(Stream *serial, Stream *debugSerial) {
  _debugSerial = debugSerial;

  if(debug) {
    _debugSerial->println("Initializing CO2...");
  }
  _serial = serial;

  if(getCO2() == -1){
    if(debug) {
      _debugSerial->println("CO2 Sensor Failed to Initialize");
    }
  }
  else{
    if(debug) {
      _debugSerial->println("CO2 Successfully Initialized");
    }
  }
}

int SensorS8::getCO2() {
  uint8_t retry = 0;
  const byte co2Request[] = {0xFE, 0x04, 0x00, 0x03, 0x00, 0x01, 0xD5, 0xC5};
  byte co2Response[7] = {0};

  while(!(_serial->available())) {
    retry++;
    // keep sending request until we start to get a response
    _serial->write(co2Request, sizeof(co2Request));
    delay(50);
    if (retry > 10) {
      return -1;
    }
  }

  uint8_t timeout = 0; 
  
  while(_serial->available() < 7) {
    timeout++; 
    if (timeout > 10) {
      while(_serial->available())  
        _serial->read();
      break;                    
    }
    delay(50);
  }

  for(uint8_t i=0; i < sizeof(co2Response); i++) {
    int byte = _serial->read();
    if (byte == -1) {
      return -1;
    }
    co2Response[i] = byte;
  }  

  if(co2Response[0] != co2Request[0] || co2Response[1] != co2Request[1]) {
    return -1;
  }

  int high = co2Response[3];                      
  int low = co2Response[4];                       
  unsigned long val = high*256 + low;

  high = co2Response[5];
  low = co2Response[6];
  uint16_t crc = high*256 + low;
  uint16_t crcCalc = _crc16(co2Response, sizeof(co2Response)-2);

  if(crc != crcCalc) {
    if(debug) {
      _debugSerial->printf("CRC mismatch: %04x != %04x\n", crc, crcCalc);
    }

    return -1;
  }

  return val;
}

int SensorS8::getABCPeriod() {
  uint8_t retry = 0;
  const byte abcRequest[] = {0xFE, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xA1, 0xC3};
  byte abcResponse[7] = {0};

  while(!(_serial->available())) {
    retry++;
    // keep sending request until we start to get a response
    _serial->write(abcRequest, sizeof(abcRequest));
    delay(50);
    if(retry > 10) {
      if(debug) {
        _debugSerial->println("Could not send ABC period request");
      }
      return -1;
    }
  }

  uint8_t timeout = 0; 
  
  while(_serial->available() < 7) {
    timeout++; 
    if(timeout > 10) {
      while(_serial->available())  
        _serial->read();
      break;                    
    }
    delay(50);
  }

  for(uint8_t i=0; i < sizeof(abcResponse); i++) {
    int byte = _serial->read();
    if(byte == -1) {
      if(debug) {
        _debugSerial->printf("ABC response ended after %d bytes", i);
      }
      return -1;
    }
    abcResponse[i] = byte;
  }  

  if(abcResponse[0] != abcRequest[0] || abcResponse[1] != abcRequest[1]) {
    return -1;
  }

  int high = abcResponse[3];                      
  int low = abcResponse[4];                       
  unsigned long val = high*256 + low;

  high = abcResponse[5];
  low = abcResponse[6];
  uint16_t crc = high*256 + low;
  uint16_t crcCalc = _crc16(abcResponse, sizeof(abcResponse)-2);

  if(crc != crcCalc) {
    if(debug) {
      _debugSerial->printf("CRC mismatch: %04x != %04x\n", crc, crcCalc);
    }

    return -1;
  }

  return val;
}

bool SensorS8::setABCPeriod() {
  uint8_t retry = 0;
  const byte abcCommand[] = {0xFE, 0x06, 0x00, 0x1F, 0x00, 0xB4, 0xAC, 0x74};
  byte abcResponse[8] = {0};

  while(!(_serial->available())) {
    retry++;
    // keep sending request until we start to get a response
    _serial->write(abcCommand, sizeof(abcCommand));
    delay(50);
    if(retry > 10) {
      if(debug) {
        _debugSerial->println("Could not send ABC period update");
      }
      return -1;
    }
  }

  uint8_t timeout = 0; 
  
  while(_serial->available() < 7) {
    timeout++; 
    if(timeout > 10) {
      while(_serial->available())  
        _serial->read();
      break;                    
    }
    delay(50);
  }

  for(uint8_t i=0; i < sizeof(abcResponse); i++) {
    int byte = _serial->read();
    if(byte == -1) {
      if(debug) {
        _debugSerial->printf("ABC update response ended after %d bytes", i);
      }
      return -1;
    }
    abcResponse[i] = byte;
  }  

  return (memcmp(abcCommand, abcResponse, sizeof(abcCommand)) == 0);
}

uint16_t SensorS8::_crc16(byte * buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
 
  for (uint8_t pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
 
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) { 
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return ((crc & 0xff) << 8) | ((crc & 0xff00) >> 8); // Swap them
}
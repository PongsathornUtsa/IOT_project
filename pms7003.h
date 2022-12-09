//created by pongsathorn utsahawattanasuk 6210554784

#pragma once
#include "Arduino.h"
#define PMS7003_PREAMBLE_1 0x42  // From PMS7003 datasheet
#define PMS7003_PREAMBLE_2 0x4D
#define PMS7003_DATA_LENGTH 31

class pms7003 {
public:
  pms7003(Stream &serial);
  pms7003(Stream &serial, Stream &debug);
  int pm1Value, pm25Value, pm10Value;
  void readSensor();
private:
  int checksum;
  unsigned char pms[32] = {
    0,
  };
  Stream *_serial;
  Stream *_debug;
};
#include "pms7003.h"

// Constructor
pms7003::pms7003(Stream &serial) {
  _serial = &serial;
}

pms7003::pms7003(Stream &serial, Stream &debug) {
  _serial = &serial;
  _debug = &debug;
}

void pms7003::readSensor() {
  checksum = 0;
  /**
     Search preamble for Packet
     Solve trouble caused by delay function
  */
  while (_serial->available() && _serial->read() != PMS7003_PREAMBLE_1 && _serial->peek() != PMS7003_PREAMBLE_2) {
  }
  if (_serial->available() >= PMS7003_DATA_LENGTH) {
    pms[0] = PMS7003_PREAMBLE_1;
    checksum += pms[0];
    for (int j = 1; j < 32; j++) {
      pms[j] = _serial->read();
      if (j < 30)
        checksum += pms[j];
    }
    _serial->flush();
    if (pms[30] != (unsigned char)(checksum >> 8)
        || pms[31] != (unsigned char)(checksum)) {
      _debug->println("Checksum error");
      return;
    }
    if (pms[0] != 0x42 || pms[1] != 0x4d) {
      _debug->println("Packet error");
      return;
    }
    pm1Value = makeWord(pms[10], pms[11]);
    pm25Value = makeWord(pms[12], pms[13]);
    pm10Value = makeWord(pms[14], pms[15]);
  }
}
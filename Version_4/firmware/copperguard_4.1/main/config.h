#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

struct ConfigData {
  // Increased to avoid overflows (was 20)
  char deviceName[32];
  char location[32];

  // Recipients (up to 5 each)
  char statusNumbers[5][16];
  char emergencyNumbers[5][16];

  // Status reporting interval (minutes)
  uint16_t statusIntervalMinutes;

  // Line configuration
  bool lineEnable[4];
  char lineName[4][12];
};

extern ConfigData config;

void loadConfig();
void saveConfig();
void startConfigMode();

#endif

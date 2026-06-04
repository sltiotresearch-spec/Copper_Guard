#include <string.h>

// =====================================================
// Copper Guard
// AT OK + Network OK continuous check
// Incoming SMS configuration
// Send alert SMS when enabled line is CUT
// =====================================================

// ---------------- DEFAULT CONFIG ----------------
// These values can be changed later using SMS:
// +94750279306,Colombo 01 ABC,1001

#define MAX_PHONE_LEN       16
#define MAX_LOCATION_LEN    32
#define GSM_LINE_LEN        90
#define SMS_TEXT_LEN        180

char alertNumber[MAX_PHONE_LEN] = "+94750279306";
char deviceLocation[MAX_LOCATION_LEN] = "Colombo 01 ABC";

// 1 = line enabled
// 0 = line disabled
bool lineEnable[4] = {1, 0, 0, 1};

// ---------------- EEPROM CONFIG STORAGE ----------------
// STM8S103 data EEPROM starts at 0x4000.
// Saved format: magic + version + number + location + line mask + checksum.
#define CG_EEPROM_BASE_ADDR      0x4000
#define CG_CFG_MAGIC1            0x43    // 'C'
#define CG_CFG_MAGIC2            0x47    // 'G'
#define CG_CFG_VERSION           1

#define CG_CFG_OFF_MAGIC1        0
#define CG_CFG_OFF_MAGIC2        1
#define CG_CFG_OFF_VERSION       2
#define CG_CFG_OFF_NUMBER        3
#define CG_CFG_OFF_LOCATION      (CG_CFG_OFF_NUMBER + MAX_PHONE_LEN)
#define CG_CFG_OFF_MASK          (CG_CFG_OFF_LOCATION + MAX_LOCATION_LEN)
#define CG_CFG_OFF_CHECKSUM      (CG_CFG_OFF_MASK + 4)

// STM8 FLASH / EEPROM control registers
#define CG_FLASH_IAPSR           (*(volatile unsigned char *)0x505F)
#define CG_FLASH_DUKR            (*(volatile unsigned char *)0x5064)
#define CG_FLASH_IAPSR_DUL       0x08

// ---------------- LED LOGIC ----------------
// STM8 onboard LED usually:
// LOW  = LED ON
// HIGH = LED OFF
#define LED_ON   LOW
#define LED_OFF  HIGH

// ---------------- MAIN STATES ----------------
#define STATE_GSM_AT_CHECK       0
#define STATE_GSM_NET_CHECK      1
#define STATE_SMS_TEXT_MODE      2
#define STATE_SMS_RECEIVE_MODE   3
#define STATE_RUN                4
#define STATE_SMS_WAIT_PROMPT    5
#define STATE_SMS_WAIT_RESULT    6

byte deviceState = STATE_GSM_AT_CHECK;

// ---------------- HEALTH CHECK STATES ----------------
#define HEALTH_IDLE      0
#define HEALTH_WAIT_AT   1
#define HEALTH_WAIT_NET  2

byte healthState = HEALTH_IDLE;

// ---------------- TIMINGS ----------------
#define AT_RETRY_TIME          3000
#define NET_RETRY_TIME         5000
#define CMD_RETRY_TIME         3000

#define HEALTH_CHECK_TIME      10000
#define HEALTH_REPLY_TIMEOUT   3000

#define SMS_PROMPT_TIMEOUT     8000
#define SMS_SEND_TIMEOUT       20000
#define SMS_MAX_RETRY          2
#define SMS_RETRY_DELAY        15000

unsigned long lastATSendTime = 0;
unsigned long lastNetSendTime = 0;
unsigned long lastCmdSendTime = 0;
unsigned long smsStartTime = 0;

unsigned long lastHealthCheckTime = 0;
unsigned long healthStartTime = 0;
unsigned long lastSmsFailTime = 0;

unsigned long lastLedTime = 0;
bool ledState = false;

// ---------------- GSM STATUS ----------------
bool gsmAtOK = false;
bool gsmNetOK = false;
bool smsTextModeOK = false;
bool smsReceiveModeOK = false;

// ---------------- OPTO STATES ----------------
bool optoState[4];
bool alertSentForLine[4] = {0, 0, 0, 0};

bool smsPending = false;
byte smsRetryCount = 0;
bool smsPromptError = false;

// ---------------- GSM BUFFER ----------------
char gsmLine[GSM_LINE_LEN];
int gsmIndex = 0;

char incomingSender[MAX_PHONE_LEN];
bool waitingSmsBody = false;

// ---------------- SMS BUFFER ----------------
char smsText[SMS_TEXT_LEN];
char smsTargetNumber[MAX_PHONE_LEN];

// =====================================================
// SAFE COPY / APPEND
// =====================================================
void safeCopy(char *dst, const char *src, int maxLen) {
  int i = 0;

  while (src[i] && i < maxLen - 1) {
    dst[i] = src[i];
    i++;
  }

  dst[i] = '\0';
}

void safeAppend(char *dst, const char *src, int maxLen) {
  int d = 0;
  int s = 0;

  while (dst[d]) d++;

  while (src[s] && d < maxLen - 1) {
    dst[d] = src[s];
    d++;
    s++;
  }

  dst[d] = '\0';
}

void trimInPlace(char *s) {
  int start = 0;
  int i;
  int end;

  while (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n') {
    start++;
  }

  if (start > 0) {
    i = 0;
    while (s[start]) {
      s[i] = s[start];
      i++;
      start++;
    }
    s[i] = '\0';
  }

  end = 0;
  while (s[end]) end++;

  if (end == 0) return;

  end--;

  while (end >= 0 && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r' || s[end] == '\n')) {
    s[end] = '\0';
    end--;
  }
}

// =====================================================
// VALIDATION
// =====================================================
bool isValidPhone(const char *num) {
  int len = 0;
  int i;

  if (num[0] != '+') return false;

  while (num[len]) len++;

  if (len < 11 || len >= MAX_PHONE_LEN) return false;

  for (i = 1; i < len; i++) {
    if (num[i] < '0' || num[i] > '9') return false;
  }

  return true;
}

bool isValidLineMask(const char *mask) {
  int i;

  for (i = 0; i < 4; i++) {
    if (mask[i] != '0' && mask[i] != '1') {
      return false;
    }
  }

  if (mask[4] != '\0') return false;

  return true;
}

// =====================================================
// OPTO READ
// Correct logic:
// 1 = CUT
// 0 = OK
// =====================================================
void readOptos() {
  optoState[0] = digitalRead(PC3);  // Line 1
  optoState[1] = digitalRead(PC4);  // Line 2
  optoState[2] = digitalRead(PC5);  // Line 3
  optoState[3] = digitalRead(PC6);  // Line 4
}

// =====================================================
// SMS TEXT BUILD
// =====================================================
void addLineStatus(int lineIndex) {
  if (lineIndex == 0) safeAppend(smsText, "Line 1 - ", SMS_TEXT_LEN);
  if (lineIndex == 1) safeAppend(smsText, "Line 2 - ", SMS_TEXT_LEN);
  if (lineIndex == 2) safeAppend(smsText, "Line 3 - ", SMS_TEXT_LEN);
  if (lineIndex == 3) safeAppend(smsText, "Line 4 - ", SMS_TEXT_LEN);

  if (!lineEnable[lineIndex]) {
    safeAppend(smsText, "Disabled\r\n", SMS_TEXT_LEN);
  } else if (optoState[lineIndex]) {
    safeAppend(smsText, "CUT\r\n", SMS_TEXT_LEN);
  } else {
    safeAppend(smsText, "OK\r\n", SMS_TEXT_LEN);
  }
}

void buildAlertSmsText() {
  smsText[0] = '\0';

  safeAppend(smsText, "-- CopperGuard @ ", SMS_TEXT_LEN);
  safeAppend(smsText, deviceLocation, SMS_TEXT_LEN);
  safeAppend(smsText, " --\r\n\r\n", SMS_TEXT_LEN);

  safeAppend(smsText, "Line cut Detected !\r\n\r\n", SMS_TEXT_LEN);

  addLineStatus(0);
  addLineStatus(1);
  addLineStatus(2);
  addLineStatus(3);
}

void buildConfigOkSmsText() {
  smsText[0] = '\0';

  safeAppend(smsText, "CopperGuard Config Updated\r\n", SMS_TEXT_LEN);
  safeAppend(smsText, "Number: ", SMS_TEXT_LEN);
  safeAppend(smsText, alertNumber, SMS_TEXT_LEN);
  safeAppend(smsText, "\r\nLocation: ", SMS_TEXT_LEN);
  safeAppend(smsText, deviceLocation, SMS_TEXT_LEN);
  safeAppend(smsText, "\r\n", SMS_TEXT_LEN);

  for (int i = 0; i < 4; i++) {
    if (i == 0) safeAppend(smsText, "Line 1 - ", SMS_TEXT_LEN);
    if (i == 1) safeAppend(smsText, "Line 2 - ", SMS_TEXT_LEN);
    if (i == 2) safeAppend(smsText, "Line 3 - ", SMS_TEXT_LEN);
    if (i == 3) safeAppend(smsText, "Line 4 - ", SMS_TEXT_LEN);

    if (lineEnable[i]) {
      safeAppend(smsText, "Enabled", SMS_TEXT_LEN);
    } else {
      safeAppend(smsText, "Disabled", SMS_TEXT_LEN);
    }

    if (i < 3) safeAppend(smsText, "\r\n", SMS_TEXT_LEN);
  }
}

void buildConfigErrorSmsText() {
  smsText[0] = '\0';

  safeAppend(smsText, "CopperGuard Config Error\r\n", SMS_TEXT_LEN);
  safeAppend(smsText, "Use format:\r\n", SMS_TEXT_LEN);
  safeAppend(smsText, "+947XXXXXXXX,Location,1001", SMS_TEXT_LEN);
}

// =====================================================
// LED INDICATION
// =====================================================
void setLed(bool on) {
  ledState = on;

  if (on) {
    digitalWrite(LED_BUILTIN, LED_ON);
  } else {
    digitalWrite(LED_BUILTIN, LED_OFF);
  }
}

void blinkLed(unsigned int blinkTime) {
  if (millis() - lastLedTime >= blinkTime) {
    lastLedTime = millis();

    ledState = !ledState;

    if (ledState) {
      digitalWrite(LED_BUILTIN, LED_ON);
    } else {
      digitalWrite(LED_BUILTIN, LED_OFF);
    }
  }
}

bool anyEnabledLineCut() {
  for (int x = 0; x < 4; x++) {
    if (optoState[x] && lineEnable[x]) {
      return true;
    }
  }

  return false;
}

void updateLedIndicator() {
  // Priority 1: SMS sending = blink every 100 ms
  if (deviceState == STATE_SMS_WAIT_PROMPT || deviceState == STATE_SMS_WAIT_RESULT) {
    blinkLed(100);
    return;
  }

  // Priority 2: any GSM error / not ready = LED ON
  // This covers AT fail, SIM/network fail, SMS mode fail, and startup/recovery states.
  if (!gsmAtOK || !gsmNetOK || !smsTextModeOK || !smsReceiveModeOK || deviceState != STATE_RUN) {
    setLed(true);
    return;
  }

  // Priority 3: any enabled line is cut = blink every 200 ms
  if (anyEnabledLineCut()) {
    blinkLed(200);
    return;
  }

  // Normal state = LED OFF
  setLed(false);
}

// =====================================================
// GSM COMMANDS
// =====================================================
void sendATCommand() {
  Serial_print_s("AT\r\n");
  lastATSendTime = millis();
}

void sendNetworkCheckCommand() {
  Serial_print_s("AT+CREG?\r\n");
  lastNetSendTime = millis();
}

void sendTextModeCommand() {
  Serial_print_s("AT+CMGF=1\r\n");
  lastCmdSendTime = millis();
}

void sendReceiveModeCommand() {
  Serial_print_s("AT+CNMI=2,2,0,0,0\r\n");
  lastCmdSendTime = millis();
}

void startSmsSend(const char *number) {
  safeCopy(smsTargetNumber, number, MAX_PHONE_LEN);

  Serial_print_s("AT+CMGS=\"");
  Serial_print_s(smsTargetNumber);
  Serial_print_s("\"\r\n");

  smsStartTime = millis();
}

void sendSmsBody() {
  Serial_print_s(smsText);
  Serial_write(26);   // Ctrl + Z

  smsStartTime = millis();
}

// =====================================================
// GSM LINE READ
// =====================================================
bool readGsmLine() {
  char c;

  while (Serial_available()) {
    c = Serial_read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      gsmLine[gsmIndex] = '\0';

      if (gsmIndex > 0) {
        gsmIndex = 0;
        return true;
      }

      gsmIndex = 0;
    } else {
      if (gsmIndex < GSM_LINE_LEN - 1) {
        gsmLine[gsmIndex] = c;
        gsmIndex++;
      }
    }
  }

  return false;
}

bool readSmsPrompt() {
  char c;

  smsPromptError = false;

  while (Serial_available()) {
    c = Serial_read();

    if (c == '>') {
      gsmIndex = 0;
      return true;
    }

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      gsmLine[gsmIndex] = '\0';

      if (gsmIndex > 0) {
        trimInPlace(gsmLine);

        if (strcmp(gsmLine, "ERROR") == 0 || strstr(gsmLine, "+CMS ERROR") != 0 || strstr(gsmLine, "+CME ERROR") != 0) {
          smsPromptError = true;
        }
      }

      gsmIndex = 0;
    } else {
      if (gsmIndex < GSM_LINE_LEN - 1) {
        gsmLine[gsmIndex] = c;
        gsmIndex++;
      }
    }
  }

  return false;
}

// =====================================================
// GSM RESPONSE CHECKS
// =====================================================
bool lineIsOK() {
  if (strcmp(gsmLine, "OK") == 0) {
    return true;
  }

  return false;
}

bool lineIsNetworkOK() {
  if (strstr(gsmLine, "+CREG:") != 0) {
    if (strstr(gsmLine, ",1") != 0) return true;
    if (strstr(gsmLine, ",5") != 0) return true;
  }

  return false;
}

bool lineIsNetworkNotOK() {
  if (strstr(gsmLine, "+CREG:") != 0) {
    if (strstr(gsmLine, ",0") != 0) return true;
    if (strstr(gsmLine, ",2") != 0) return true;
    if (strstr(gsmLine, ",3") != 0) return true;
    if (strstr(gsmLine, ",4") != 0) return true;
  }

  return false;
}

bool lineIsSmsSentOK() {
  if (strstr(gsmLine, "+CMGS:") != 0) return true;
  if (strcmp(gsmLine, "OK") == 0) return true;

  return false;
}

bool lineIsSmsError() {
  if (strcmp(gsmLine, "ERROR") == 0) return true;
  if (strstr(gsmLine, "+CMS ERROR") != 0) return true;
  if (strstr(gsmLine, "+CME ERROR") != 0) return true;

  return false;
}

// =====================================================
// INCOMING SMS CONFIGURATION
// =====================================================
bool extractQuotedNumber(const char *line, char *out) {
  int firstQ = -1;
  int secondQ = -1;
  int i;
  int j;

  for (i = 0; line[i]; i++) {
    if (line[i] == '"') {
      if (firstQ < 0) {
        firstQ = i;
      } else {
        secondQ = i;
        break;
      }
    }
  }

  if (firstQ < 0 || secondQ < 0 || secondQ <= firstQ + 1) {
    out[0] = '\0';
    return false;
  }

  j = 0;

  for (i = firstQ + 1; i < secondQ && j < MAX_PHONE_LEN - 1; i++) {
    out[j] = line[i];
    j++;
  }

  out[j] = '\0';

  return true;
}

void resetAlertFlags() {
  for (int i = 0; i < 4; i++) {
    alertSentForLine[i] = false;
  }
}

// =====================================================
// EEPROM CONFIG SAVE / LOAD
// =====================================================
unsigned char eepromReadByte(unsigned int offset) {
  return *((volatile unsigned char *)(CG_EEPROM_BASE_ADDR + offset));
}

bool eepromUnlock() {
  unsigned long startTime;

  if (CG_FLASH_IAPSR & CG_FLASH_IAPSR_DUL) {
    return true;
  }

  startTime = millis();

  CG_FLASH_DUKR = 0xAE;
  CG_FLASH_DUKR = 0x56;

  while (!(CG_FLASH_IAPSR & CG_FLASH_IAPSR_DUL)) {
    if (millis() - startTime > 100) {
      return false;
    }
  }

  return true;
}

void eepromLock() {
  CG_FLASH_IAPSR &= (unsigned char)(~CG_FLASH_IAPSR_DUL);
}

void eepromWriteByte(unsigned int offset, unsigned char value) {
  volatile unsigned char *addr;

  if (eepromReadByte(offset) == value) {
    return;
  }

  addr = (volatile unsigned char *)(CG_EEPROM_BASE_ADDR + offset);
  *addr = value;

  // Small wait for STM8 EEPROM byte programming.
  delay(5);
}

unsigned char calcConfigChecksum(const char *number, const char *location, const char *mask) {
  unsigned int sum = 0;
  int i;

  sum += CG_CFG_VERSION;

  for (i = 0; number[i]; i++) {
    sum += (unsigned char)number[i];
  }

  for (i = 0; location[i]; i++) {
    sum += (unsigned char)location[i];
  }

  for (i = 0; i < 4; i++) {
    sum += (unsigned char)mask[i];
  }

  return (unsigned char)(sum & 0xFF);
}

void buildLineMask(char *mask) {
  mask[0] = lineEnable[0] ? '1' : '0';
  mask[1] = lineEnable[1] ? '1' : '0';
  mask[2] = lineEnable[2] ? '1' : '0';
  mask[3] = lineEnable[3] ? '1' : '0';
  mask[4] = '\0';
}

void saveConfigToEEPROM() {
  char mask[5];
  unsigned char checksum;
  int i;

  buildLineMask(mask);
  checksum = calcConfigChecksum(alertNumber, deviceLocation, mask);

  if (!eepromUnlock()) {
    return;
  }

  eepromWriteByte(CG_CFG_OFF_MAGIC1, CG_CFG_MAGIC1);
  eepromWriteByte(CG_CFG_OFF_MAGIC2, CG_CFG_MAGIC2);
  eepromWriteByte(CG_CFG_OFF_VERSION, CG_CFG_VERSION);

  for (i = 0; i < MAX_PHONE_LEN; i++) {
    eepromWriteByte(CG_CFG_OFF_NUMBER + i, (unsigned char)alertNumber[i]);
  }

  for (i = 0; i < MAX_LOCATION_LEN; i++) {
    eepromWriteByte(CG_CFG_OFF_LOCATION + i, (unsigned char)deviceLocation[i]);
  }

  for (i = 0; i < 4; i++) {
    eepromWriteByte(CG_CFG_OFF_MASK + i, (unsigned char)mask[i]);
  }

  eepromWriteByte(CG_CFG_OFF_CHECKSUM, checksum);

  eepromLock();
}

bool loadConfigFromEEPROM() {
  char savedNumber[MAX_PHONE_LEN];
  char savedLocation[MAX_LOCATION_LEN];
  char savedMask[5];
  unsigned char savedChecksum;
  unsigned char calcChecksum;
  int i;

  if (eepromReadByte(CG_CFG_OFF_MAGIC1) != CG_CFG_MAGIC1) return false;
  if (eepromReadByte(CG_CFG_OFF_MAGIC2) != CG_CFG_MAGIC2) return false;
  if (eepromReadByte(CG_CFG_OFF_VERSION) != CG_CFG_VERSION) return false;

  for (i = 0; i < MAX_PHONE_LEN; i++) {
    savedNumber[i] = (char)eepromReadByte(CG_CFG_OFF_NUMBER + i);
  }
  savedNumber[MAX_PHONE_LEN - 1] = '\0';

  for (i = 0; i < MAX_LOCATION_LEN; i++) {
    savedLocation[i] = (char)eepromReadByte(CG_CFG_OFF_LOCATION + i);
  }
  savedLocation[MAX_LOCATION_LEN - 1] = '\0';

  for (i = 0; i < 4; i++) {
    savedMask[i] = (char)eepromReadByte(CG_CFG_OFF_MASK + i);
  }
  savedMask[4] = '\0';

  savedChecksum = eepromReadByte(CG_CFG_OFF_CHECKSUM);
  calcChecksum = calcConfigChecksum(savedNumber, savedLocation, savedMask);

  if (savedChecksum != calcChecksum) return false;
  if (!isValidPhone(savedNumber)) return false;
  if (savedLocation[0] == '\0') return false;
  if (!isValidLineMask(savedMask)) return false;

  safeCopy(alertNumber, savedNumber, MAX_PHONE_LEN);
  safeCopy(deviceLocation, savedLocation, MAX_LOCATION_LEN);

  lineEnable[0] = savedMask[0] == '1';
  lineEnable[1] = savedMask[1] == '1';
  lineEnable[2] = savedMask[2] == '1';
  lineEnable[3] = savedMask[3] == '1';

  resetAlertFlags();

  return true;
}

bool applyConfigSms(char *body) {
  char *comma1;
  char *comma2;

  char *newNumber;
  char *newLocation;
  char *newMask;

  trimInPlace(body);

  comma1 = strchr(body, ',');
  if (comma1 == 0) return false;

  comma2 = strchr(comma1 + 1, ',');
  if (comma2 == 0) return false;

  comma1[0] = '\0';
  comma2[0] = '\0';

  newNumber = body;
  newLocation = comma1 + 1;
  newMask = comma2 + 1;

  trimInPlace(newNumber);
  trimInPlace(newLocation);
  trimInPlace(newMask);

  if (!isValidPhone(newNumber)) return false;
  if (newLocation[0] == '\0') return false;
  if (!isValidLineMask(newMask)) return false;

  safeCopy(alertNumber, newNumber, MAX_PHONE_LEN);
  safeCopy(deviceLocation, newLocation, MAX_LOCATION_LEN);

  lineEnable[0] = newMask[0] == '1';
  lineEnable[1] = newMask[1] == '1';
  lineEnable[2] = newMask[2] == '1';
  lineEnable[3] = newMask[3] == '1';

  resetAlertFlags();
  saveConfigToEEPROM();

  return true;
}

bool handleIncomingSmsLine() {
  trimInPlace(gsmLine);

  if (gsmLine[0] == '\0') {
    return true;
  }

  // Incoming SMS header
  if (strncmp(gsmLine, "+CMT:", 5) == 0) {
    extractQuotedNumber(gsmLine, incomingSender);
    waitingSmsBody = true;
    return true;
  }

  // Incoming SMS body
  if (waitingSmsBody) {
    waitingSmsBody = false;

    if (applyConfigSms(gsmLine)) {
      buildConfigOkSmsText();

      // Send confirmation to the sender of the configuration SMS
      safeCopy(smsTargetNumber, incomingSender, MAX_PHONE_LEN);
      smsRetryCount = 0;
      smsPending = true;
    } else {
      buildConfigErrorSmsText();

      // Send error message to sender
      safeCopy(smsTargetNumber, incomingSender, MAX_PHONE_LEN);
      smsRetryCount = 0;
      smsPending = true;
    }

    return true;
  }

  return false;
}

// =====================================================
// CONTINUOUS GSM HEALTH CHECK
// =====================================================
void serviceGsmHealthCheck() {
  if (healthState == HEALTH_IDLE) {
    if (millis() - lastHealthCheckTime >= HEALTH_CHECK_TIME) {
      Serial_print_s("AT\r\n");

      healthState = HEALTH_WAIT_AT;
      healthStartTime = millis();
      lastHealthCheckTime = millis();
    }

    return;
  }

  if (healthState == HEALTH_WAIT_AT) {
    if (readGsmLine()) {
      if (handleIncomingSmsLine()) return;

      if (lineIsOK()) {
        gsmAtOK = true;

        Serial_print_s("AT+CREG?\r\n");

        healthState = HEALTH_WAIT_NET;
        healthStartTime = millis();
        return;
      }
    }

    if (millis() - healthStartTime >= HEALTH_REPLY_TIMEOUT) {
      gsmAtOK = false;
      gsmNetOK = false;
      smsTextModeOK = false;
      smsReceiveModeOK = false;

      healthState = HEALTH_IDLE;
      deviceState = STATE_GSM_AT_CHECK;
    }

    return;
  }

  if (healthState == HEALTH_WAIT_NET) {
    if (readGsmLine()) {
      if (handleIncomingSmsLine()) return;

      if (lineIsNetworkOK()) {
        gsmNetOK = true;
        healthState = HEALTH_IDLE;
        return;
      }

      if (lineIsNetworkNotOK()) {
        gsmNetOK = false;
        smsTextModeOK = false;
        smsReceiveModeOK = false;

        healthState = HEALTH_IDLE;
        deviceState = STATE_GSM_NET_CHECK;
        return;
      }
    }

    if (millis() - healthStartTime >= HEALTH_REPLY_TIMEOUT) {
      gsmNetOK = false;
      smsTextModeOK = false;
      smsReceiveModeOK = false;

      healthState = HEALTH_IDLE;
      deviceState = STATE_GSM_NET_CHECK;
    }

    return;
  }
}

// =====================================================
// LINE CUT CHECK
// =====================================================
void checkLineCuts() {
  bool needSms = false;

  readOptos();

  for (int i = 0; i < 4; i++) {
    if (!lineEnable[i]) {
      alertSentForLine[i] = false;
      continue;
    }

    if (!optoState[i]) {
      alertSentForLine[i] = false;
      continue;
    }

    if (optoState[i] && !alertSentForLine[i]) {
      needSms = true;
    }
  }

  if (needSms && !smsPending) {
    if (millis() - lastSmsFailTime >= SMS_RETRY_DELAY) {
      buildAlertSmsText();
      safeCopy(smsTargetNumber, alertNumber, MAX_PHONE_LEN);
      smsRetryCount = 0;
      smsPending = true;
    }
  }
}

// =====================================================
// TRY SEND PENDING SMS
// =====================================================
void servicePendingSms() {
  if (!smsPending) return;

  if (!gsmAtOK || !gsmNetOK || !smsTextModeOK || !smsReceiveModeOK) {
    return;
  }

  if (healthState != HEALTH_IDLE) {
    return;
  }

  startSmsSend(smsTargetNumber);
  deviceState = STATE_SMS_WAIT_PROMPT;
}

void handleSmsFailure() {
  gsmAtOK = false;
  gsmNetOK = false;
  smsTextModeOK = false;
  smsReceiveModeOK = false;

  lastSmsFailTime = millis();

  smsRetryCount++;

  if (smsRetryCount >= SMS_MAX_RETRY) {
    smsPending = false;
    smsRetryCount = 0;
  }

  deviceState = STATE_GSM_AT_CHECK;
}

void markCurrentCutsAsAlerted() {
  readOptos();

  for (int i = 0; i < 4; i++) {
    if (lineEnable[i] && optoState[i]) {
      alertSentForLine[i] = true;
    }
  }
}

// =====================================================
// STATE MACHINE
// =====================================================
void serviceStateMachine() {
  switch (deviceState) {

    case STATE_GSM_AT_CHECK:

      if (millis() - lastATSendTime >= AT_RETRY_TIME) {
        sendATCommand();
      }

      if (readGsmLine()) {
        if (handleIncomingSmsLine()) break;

        if (lineIsOK()) {
          gsmAtOK = true;

          sendNetworkCheckCommand();
          deviceState = STATE_GSM_NET_CHECK;
        }
      }

      break;

    case STATE_GSM_NET_CHECK:

      if (millis() - lastNetSendTime >= NET_RETRY_TIME) {
        sendNetworkCheckCommand();
      }

      if (readGsmLine()) {
        if (handleIncomingSmsLine()) break;

        if (lineIsNetworkOK()) {
          gsmNetOK = true;

          sendTextModeCommand();
          deviceState = STATE_SMS_TEXT_MODE;
        }
      }

      break;

    case STATE_SMS_TEXT_MODE:

      if (millis() - lastCmdSendTime >= CMD_RETRY_TIME) {
        sendTextModeCommand();
      }

      if (readGsmLine()) {
        if (handleIncomingSmsLine()) break;

        if (lineIsOK()) {
          smsTextModeOK = true;

          sendReceiveModeCommand();
          deviceState = STATE_SMS_RECEIVE_MODE;
        }
      }

      break;

    case STATE_SMS_RECEIVE_MODE:

      if (millis() - lastCmdSendTime >= CMD_RETRY_TIME) {
        sendReceiveModeCommand();
      }

      if (readGsmLine()) {
        if (handleIncomingSmsLine()) break;

        if (lineIsOK()) {
          smsReceiveModeOK = true;

          lastHealthCheckTime = millis();
          healthState = HEALTH_IDLE;

          deviceState = STATE_RUN;
        }
      }

      break;

    case STATE_RUN:

      serviceGsmHealthCheck();

      if (deviceState != STATE_RUN) {
        break;
      }

      // Also catch incoming SMS while idle
      if (readGsmLine()) {
        handleIncomingSmsLine();
      }

      checkLineCuts();
      servicePendingSms();

      break;

    case STATE_SMS_WAIT_PROMPT:

      if (readSmsPrompt()) {
        sendSmsBody();
        deviceState = STATE_SMS_WAIT_RESULT;
      }

      if (smsPromptError) {
        handleSmsFailure();
        break;
      }

      if (millis() - smsStartTime >= SMS_PROMPT_TIMEOUT) {
        handleSmsFailure();
      }

      break;

    case STATE_SMS_WAIT_RESULT:

      if (readGsmLine()) {
        if (handleIncomingSmsLine()) break;

        if (lineIsSmsSentOK()) {
          // If the sent SMS was an alert, mark current cuts as alerted.
          // If it was config confirmation, this is harmless.
          markCurrentCutsAsAlerted();

          smsPending = false;
          smsRetryCount = 0;

          lastHealthCheckTime = millis();
          deviceState = STATE_RUN;
        }

        if (lineIsSmsError()) {
          handleSmsFailure();
          break;
        }
      }

      if (millis() - smsStartTime >= SMS_SEND_TIMEOUT) {
        handleSmsFailure();
      }

      break;
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial_begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_ON);

  pinMode(PC3, INPUT_PULLUP);
  pinMode(PC4, INPUT_PULLUP);
  pinMode(PC5, INPUT_PULLUP);
  pinMode(PC6, INPUT_PULLUP);

  // Load saved SMS configuration from EEPROM.
  // If EEPROM is empty or corrupted, the default values above are used.
  loadConfigFromEEPROM();

  incomingSender[0] = '\0';
  smsTargetNumber[0] = '\0';

  delay(3000);

  sendATCommand();
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  serviceStateMachine();
  updateLedIndicator();
}
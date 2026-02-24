#include "gsm.h"

// Slow, stable GSM helpers.
// Assumes the GSM module is connected to the ESP's hardware Serial.

static const unsigned long CMD_TIMEOUT_MS    = 12000;
static const unsigned long NET_TIMEOUT_MS    = 20000;
static const unsigned long SMS_TIMEOUT_MS    = 35000;
static const unsigned long PROMPT_TIMEOUT_MS = 15000;

// Drain any pending junk from the UART RX buffer.
// On sudden MCU reboots the GSM module may keep running and can leave
// partial lines / unsolicited messages in the RX buffer. A timed drain is
// more reliable than a single "while(available)" loop.
static void flushIn(unsigned long quietMs = 60, unsigned long maxMs = 600) {
  unsigned long start = millis();
  unsigned long last  = start;

  while ((unsigned long)(millis() - start) < maxMs) {
    bool got = false;
    while (Serial.available()) {
      got = true;
      (void)Serial.read();
      last = millis();
    }

    // Stop once we've seen a "quiet" period.
    if (!got && (unsigned long)(millis() - last) >= quietMs) break;

    delay(5);
    yield();
  }
}

// Wait for any of the needles to appear in a sliding window.
// Returns 1/2/3 depending on which needle matched, 0 on timeout.
static int waitForAny(const char* n1, const char* n2, const char* n3, unsigned long timeoutMs) {
  unsigned long start = millis();
  char win[64];
  size_t w = 0;
  memset(win, 0, sizeof(win));

  while ((unsigned long)(millis() - start) < timeoutMs) {
    while (Serial.available()) {
      char c = (char)Serial.read();

      if (w < sizeof(win) - 1) {
        win[w++] = c;
        win[w] = '\0';
      } else {
        memmove(win, win + 1, sizeof(win) - 2);
        win[sizeof(win) - 2] = c;
        win[sizeof(win) - 1] = '\0';
      }

      if (n1 && strstr(win, n1)) return 1;
      if (n2 && strstr(win, n2)) return 2;
      if (n3 && strstr(win, n3)) return 3;
    }
    delay(10);
    yield();
  }
  return 0;
}

// Send an AT command and wait for OK / ERROR.
static bool cmdOK(const char* cmd, unsigned long timeoutMs) {
  flushIn();
  Serial.print(cmd);
  Serial.print("\r");
  delay(250); // pacing
  int r = waitForAny("OK\r", "ERROR", "+CME ERROR", timeoutMs);
  return (r == 1);
}

// Read full response until OK/ERROR into buffer (best effort).
static bool cmdRead(const char* cmd, char* out, size_t outLen, unsigned long timeoutMs) {
  if (!out || outLen == 0) return false;
  out[0] = '\0';

  flushIn();
  Serial.print(cmd);
  Serial.print("\r");
  delay(250);

  unsigned long start = millis();
  size_t idx = 0;

  while ((unsigned long)(millis() - start) < timeoutMs) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (idx + 1 < outLen) {
        out[idx++] = c;
        out[idx] = '\0';
      }
      // Stop conditions
      if (strstr(out, "\r\nOK\r\n") || strstr(out, "OK\r") || strstr(out, "\nOK\n")) {
        return true;
      }
      if (strstr(out, "ERROR") || strstr(out, "+CME ERROR") || strstr(out, "+CMS ERROR")) {
        return false;
      }
    }
    delay(10);
    yield();
  }
  return false;
}

// Parse +CREG response for registration status (1=home, 5=roaming).
static bool parseCREGRegistered(const char* resp) {
  if (!resp) return false;
  const char* p = strstr(resp, "+CREG:");
  if (!p) return false;
  const char* comma = strchr(p, ',');
  if (!comma) return false;
  comma++;
  while (*comma == ' ') comma++;
  const int stat = (*comma >= '0' && *comma <= '9') ? (*comma - '0') : -1;
  return (stat == 1 || stat == 5);
}

bool gsm_check_at() {
  // Retry + drain in between to recover from garbage / leftover URCs.
  for (uint8_t i = 0; i < 3; i++) {
    if (cmdOK("AT", CMD_TIMEOUT_MS)) return true;
    flushIn(100, 900);
    delay(200);
    yield();
  }
  return false;
}

bool gsm_check_net() {
  char resp[200];
  for (uint8_t attempt = 0; attempt < 6; ++attempt) {
    bool ok = cmdRead("AT+CREG?", resp, sizeof(resp), NET_TIMEOUT_MS);
    if (ok && parseCREGRegistered(resp)) return true;
    delay(1500);
    yield();
  }
  return false;
}

bool gsm_get_csq(int* rssi, int* ber) {
  if (rssi) *rssi = 99;
  if (ber) *ber = 99;

  char resp[180];
  if (!cmdRead("AT+CSQ", resp, sizeof(resp), CMD_TIMEOUT_MS)) return false;

  const char* p = strstr(resp, "+CSQ:");
  if (!p) return false;
  p = strchr(p, ':');
  if (!p) return false;
  p++;
  while (*p == ' ') p++;

  int a = 99, b = 99;
  a = atoi(p);
  const char* c = strchr(p, ',');
  if (c) b = atoi(c + 1);

  if (rssi) *rssi = a;
  if (ber) *ber = b;
  return true;
}

bool init_gsm() {
  // Make sure we start from a clean UART window.
  flushIn(150, 1500);

  // Minimal self-test: AT OK + Network OK
  if (!gsm_check_at()) return false;

  delay(400);

  // Reduce UART clutter (echo off). Ignore failures.
  (void)cmdOK("ATE0", CMD_TIMEOUT_MS);
  delay(200);

  // Text mode + basic SMS storage setup (best-effort; ignore failures)
  (void)cmdOK("AT+CMGF=1", CMD_TIMEOUT_MS);
  delay(200);
  (void)cmdOK("AT+CPMS=\"SM\",\"SM\",\"SM\"", CMD_TIMEOUT_MS);
  delay(200);
  // Enable "new SMS" indications (best-effort)
  (void)cmdOK("AT+CNMI=2,1,0,0,0", CMD_TIMEOUT_MS);
  delay(200);

  return gsm_check_net();
}

bool send_sms(const char* number, const char* text) {
  if (!number || !text) return false;

  // Always re-check modem is responsive (cheap and stabilizes "stale" UART states)
  if (!gsm_check_at()) return false;
  delay(250);

  // Text mode
  if (!cmdOK("AT+CMGF=1", CMD_TIMEOUT_MS)) return false;
  delay(250);

  // Start CMGS
  flushIn();
  Serial.print("AT+CMGS=\"");
  Serial.print(number);
  Serial.print("\"\r");
  delay(350);

  // Wait for prompt
  int p = waitForAny(">", "ERROR", "+CMS ERROR", PROMPT_TIMEOUT_MS);
  if (p != 1) return false;
  delay(150);

  // Send body slowly
  for (const char* q = text; *q; ++q) {
    Serial.write(*q);
    delay(2);
    yield();
  }

  // Ctrl+Z
  Serial.write(0x1A);

  // Wait for final OK or error
  int r = waitForAny("OK\r", "ERROR", "+CMS ERROR", SMS_TIMEOUT_MS);
  return (r == 1);
}

void make_call(const char* number) {
  if (!number) return;

  // Ensure modem responds
  (void)gsm_check_at();
  delay(250);

  Serial.print("ATD");
  Serial.print(number);
  Serial.print(";\r");

  // Ring time (slow + steady)
  unsigned long start = millis();
  while ((unsigned long)(millis() - start) < 15000UL) {
    yield();
    delay(50);
  }

  Serial.print("ATH\r");
  delay(250);
}

// ================= INCOMING SMS =================
// Strategy: periodically list unread messages and pull the first one.

static void trimInPlace(char* s) {
  if (!s) return;
  // left trim
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\r' || s[i] == '\n' || s[i] == '\t') i++;
  if (i) memmove(s, s + i, strlen(s + i) + 1);
  // right trim
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t')) {
    s[n - 1] = '\0';
    n--;
  }
}

bool gsm_fetch_unread_sms(SmsMessage* out) {
  if (!out) return false;
  out->from[0] = '\0';
  out->text[0] = '\0';

  // Quick modem check
  if (!gsm_check_at()) return false;
  delay(150);

  // Ensure text mode (best effort)
  (void)cmdOK("AT+CMGF=1", CMD_TIMEOUT_MS);
  delay(150);

  // List unread
  char resp[800];
  if (!cmdRead("AT+CMGL=\"REC UNREAD\"", resp, sizeof(resp), NET_TIMEOUT_MS)) {
    return false;
  }

  const char* p = strstr(resp, "+CMGL:");
  if (!p) return false;

  int idx = atoi(p + 6);
  if (idx <= 0) return false;

  // Extract sender number (3rd quote start .. 4th quote end)
  const char* q = p;
  int quoteCount = 0;
  const char* numStart = nullptr;
  const char* numEnd   = nullptr;

  while (*q && quoteCount < 4) {
    if (*q == '"') {
      quoteCount++;
      if (quoteCount == 3) numStart = q + 1; // start of number
      if (quoteCount == 4) numEnd = q;       // end of number
    }
    q++;
  }

  if (!numStart || !numEnd || numEnd <= numStart) return false;

  size_t numLen = (size_t)(numEnd - numStart);
  if (numLen >= sizeof(out->from)) numLen = sizeof(out->from) - 1;
  memcpy(out->from, numStart, numLen);
  out->from[numLen] = '\0';
  trimInPlace(out->from);

  // Message body: first line after the header line
  const char* headerEnd = strchr(p, '\n');
  if (!headerEnd) return false;
  headerEnd++; // after '\n'

  const char* bodyEnd = strpbrk(headerEnd, "\r\n");
  size_t bodyLen = bodyEnd ? (size_t)(bodyEnd - headerEnd) : strlen(headerEnd);
  if (bodyLen >= sizeof(out->text)) bodyLen = sizeof(out->text) - 1;
  memcpy(out->text, headerEnd, bodyLen);
  out->text[bodyLen] = '\0';
  trimInPlace(out->text);

  // Delete message to avoid SIM storage filling up
  char delCmd[24];
  snprintf(delCmd, sizeof(delCmd), "AT+CMGD=%d", idx);
  (void)cmdOK(delCmd, CMD_TIMEOUT_MS);
  delay(150);

  return true;
}

#include "config.h"
#include "oled.h"
#include "gsm.h"

#include <ESP8266WiFi.h>
#include <ctype.h>

// ================= GPIO =================
#define CONFIG_BTN      0
#define OPTO1_PIN       12
#define OPTO2_PIN       13
#define OPTO3_PIN       14
#define OPTO4_PIN       16

// ================= TIMINGS (slow + steady) =================
static const unsigned long BOOT_COOLDOWN_MS        = 10000UL;     // 10s
static const unsigned long UI_REFRESH_MS           = 500UL;
static const unsigned long STATUS_RETRY_MS         = 20000UL;     // if status SMS fails
static const unsigned long GSM_RETEST_MS           = 30000UL;     // if GSM not ready
static const unsigned long BETWEEN_STATUS_SMS_MS   = 3000UL;
static const unsigned long BETWEEN_EMERG_SMS_MS    = 5000UL;
static const unsigned long BETWEEN_EMERG_CALL_MS   = 1500UL;
static const uint8_t       EMERG_REBOOT_GRACE_SEC  = 10;          // wait before calling (survives reboot via RTC)

// SMS command polling (unread list)
static const unsigned long SMS_POLL_MS             = 5000UL;

// ================= LINE STATE =================
bool line_EN[4];
bool line_ST[4];
bool last_line_ST[4];

bool gsmReady = false;
bool armed = true;

unsigned long nextUiMs = 0;
unsigned long nextSmsPollMs = 0;

// Status scheduler
unsigned long nextStatusDueMs = 0;
unsigned long nextGsmRetestMs = 0;

// ================= RTC BOOT STATE =================
// Plan: skip cooldown + self-tests after sudden reboots.
static const uint32_t RTC_MAGIC = 0xC0A1B008;

struct RtcState {
  uint32_t magic;
  uint32_t crc;
  uint8_t  testsPassed;  // 1 once AT+NET tests pass
  uint8_t  armed;        // 1 = armed, 0 = disarmed (requires ARM SMS)
  uint8_t  lastCutMask;  // used to avoid repeating the same alert after reboot
  uint8_t  alertStage;   // 0 = idle, 1 = SMS done/assumed -> countdown -> CALL
  uint8_t  pendingCutMask;
  uint8_t  callCountdownSec; // decremented every second; survives reboot
  uint16_t reserved;
  uint32_t seq;
};

static RtcState rtc;

// Emergency call countdown tick (runtime only)
static unsigned long nextCallTickMs = 0;

static uint32_t fnv1a(const uint8_t* data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

static uint32_t rtcCalcCrc(const RtcState& s) {
  RtcState tmp = s;
  tmp.crc = 0;
  return fnv1a((const uint8_t*)&tmp, sizeof(tmp));
}

static bool rtcLoad(RtcState& out) {
  RtcState tmp;
  if (!ESP.rtcUserMemoryRead(0, (uint32_t*)&tmp, sizeof(tmp))) return false;
  if (tmp.magic != RTC_MAGIC) return false;
  uint32_t saved = tmp.crc;
  tmp.crc = 0;
  if (fnv1a((const uint8_t*)&tmp, sizeof(tmp)) != saved) return false;
  out = tmp;
  out.crc = saved;
  return true;
}

static void rtcSave(RtcState& st) {
  st.magic = RTC_MAGIC;
  st.crc = 0;
  st.crc = rtcCalcCrc(st);
  (void)ESP.rtcUserMemoryWrite(0, (uint32_t*)&st, sizeof(st));
}

// ================= UTIL =================
static bool isValidNumber(const char* n) {
  return (n && strlen(n) > 5 && n[0] != '\xFF');
}

static void stableWaitMs(unsigned long ms) {
  unsigned long t = millis();
  while ((unsigned long)(millis() - t) < ms) {
    yield();
    delay(10);
  }
}

static bool timeReached(unsigned long now, unsigned long target) {
  return (long)(now - target) >= 0;
}

static void lineLabel(uint8_t i, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';
  if (i >= 4) return;
  if (config.lineName[i][0] != '\0' && config.lineName[i][0] != '\xFF') {
    strncpy(out, config.lineName[i], outLen - 1);
    out[outLen - 1] = '\0';
  } else {
    snprintf(out, outLen, "L%u", (unsigned)(i + 1));
  }
}

static void buildCutsList(uint8_t cutMask, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';
  for (uint8_t i = 0; i < 4; i++) {
    if (!(cutMask & (1u << i))) continue;
    char lab[16];
    lineLabel(i, lab, sizeof(lab));
    if (out[0] != '\0') strncat(out, ",", outLen - strlen(out) - 1);
    strncat(out, lab, outLen - strlen(out) - 1);
  }
  if (out[0] == '\0') strncpy(out, "-", outLen - 1);
}

static void toUpperTrim(char* s) {
  if (!s) return;
  // trim left
  while (*s && isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
  // trim right
  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) { s[n - 1] = '\0'; n--; }
  // upper
  for (size_t i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
}

// Normalize numbers and compare last 9 digits (robust for +94 / 0 prefixes)
static bool sameNumberLoose(const char* a, const char* b) {
  if (!a || !b) return false;

  char da[24]; char db[24];
  size_t ia = 0, ib = 0;

  for (size_t i = 0; a[i] && ia + 1 < sizeof(da); i++) if (isdigit((unsigned char)a[i])) da[ia++] = a[i];
  for (size_t i = 0; b[i] && ib + 1 < sizeof(db); i++) if (isdigit((unsigned char)b[i])) db[ib++] = b[i];
  da[ia] = '\0'; db[ib] = '\0';

  if (ia < 7 || ib < 7) return false;

  const char* ta = da + (ia > 9 ? ia - 9 : 0);
  const char* tb = db + (ib > 9 ? ib - 9 : 0);
  return strcmp(ta, tb) == 0;
}

static bool isAuthorizedSender(const char* sender) {
  if (!sender || sender[0] == '\0') return false;

  for (int i = 0; i < 5; i++) {
    if (isValidNumber(config.statusNumbers[i]) && sameNumberLoose(sender, config.statusNumbers[i])) return true;
    if (isValidNumber(config.emergencyNumbers[i]) && sameNumberLoose(sender, config.emergencyNumbers[i])) return true;
  }
  return false;
}

// ================= CONFIG BUTTON (hold 2s) =================
static uint32_t cfgPressStartMs = 0;
static bool configModeRequested = false;

static void pollConfigButton() {
  if (configModeRequested) return;

  if (digitalRead(CONFIG_BTN) == LOW) {
    if (cfgPressStartMs == 0) cfgPressStartMs = millis();
    else if ((uint32_t)(millis() - cfgPressStartMs) >= 2000) {
      configModeRequested = true;
    }
  } else {
    cfgPressStartMs = 0;
  }
}

// ================= BOOT: COOLDOWN + SELF TEST =================
static void bootCooldownOnce() {
  unsigned long start = millis();
  while (!timeReached(millis(), start + BOOT_COOLDOWN_MS)) {
    unsigned long left = (start + BOOT_COOLDOWN_MS) - millis();
    unsigned long sec = (left + 999UL) / 1000UL;

    draw_boot_screen("COOLDOWN", false, false, (int)sec);
    stableWaitMs(250);
  }
}

static bool bootSelfTest_AT_NET() {
  bool atOk = gsm_check_at();
  draw_boot_screen("SELF TEST", atOk, false, -1);
  stableWaitMs(250);

  bool netOk = false;
  bool ok = false;
  if (atOk) {
    ok = init_gsm();   // includes NET check + SMS basic setup
    netOk = ok;
  }

  draw_boot_screen("SELF TEST", atOk, netOk, -1);
  stableWaitMs(800);

  return ok;
}

// ================= MESSAGING =================
static bool sendToStatusRecipients(const char* msg) {
  bool allOk = true;
  for (int i = 0; i < 5; i++) {
    if (!isValidNumber(config.statusNumbers[i])) continue;
    bool ok = send_sms(config.statusNumbers[i], msg);
    allOk = allOk && ok;
    stableWaitMs(BETWEEN_STATUS_SMS_MS);
  }
  return allOk;
}

// Emergency flow is split into 2 stages:
//  1) SMS to emergency recipients
//  2) CALL after a grace period (so if the device reboots due to current spike, it still calls)
static bool sendEmergencySmsOnly(const char* msg) {
  bool attempted = false;
  bool anySmsOk = false;

  for (int i = 0; i < 5; i++) {
    if (!isValidNumber(config.emergencyNumbers[i])) continue;
    attempted = true;
    bool ok = send_sms(config.emergencyNumbers[i], msg);
    anySmsOk = anySmsOk || ok;
    // If modem is dead, don't hammer it endlessly.
    if (!ok && !anySmsOk) return false;
    stableWaitMs(BETWEEN_EMERG_SMS_MS);
  }

  // If there are no emergency numbers, we still consider the stage "done".
  return attempted ? anySmsOk : true;
}

static void callEmergencyNumbers() {
  for (int i = 0; i < 5; i++) {
    if (!isValidNumber(config.emergencyNumbers[i])) continue;
    make_call(config.emergencyNumbers[i]);
    stableWaitMs(BETWEEN_EMERG_CALL_MS);
  }
}

static void buildEmergencyMessage(uint8_t cutMask, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';

  // Keep it short (SMS length)
  char cuts[64];
  buildCutsList(cutMask, cuts, sizeof(cuts));

  snprintf(out, outLen,
           "CopperGuard ALERT\nDev:%s\nLoc:%s\nCUT:%s",
           config.deviceName, config.location, cuts);
}

static void buildStatusMessage(char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';

  // One-line-per-line to keep SMS readable
  char l1[16], l2[16], l3[16], l4[16];
  lineLabel(0, l1, sizeof(l1));
  lineLabel(1, l2, sizeof(l2));
  lineLabel(2, l3, sizeof(l3));
  lineLabel(3, l4, sizeof(l4));

  snprintf(out, outLen,
           "CopperGuard STATUS\nDev:%s\nLoc:%s\nARM:%s\n"
           "%s:%s %s\n"
           "%s:%s %s\n"
           "%s:%s %s\n"
           "%s:%s %s",
           config.deviceName, config.location, (armed ? "ON" : "OFF"),
           l1, (line_EN[0] ? "EN" : "OFF"), (line_EN[0] ? (line_ST[0] ? "OK" : "CUT") : ""),
           l2, (line_EN[1] ? "EN" : "OFF"), (line_EN[1] ? (line_ST[1] ? "OK" : "CUT") : ""),
           l3, (line_EN[2] ? "EN" : "OFF"), (line_EN[2] ? (line_ST[2] ? "OK" : "CUT") : ""),
           l4, (line_EN[3] ? "EN" : "OFF"), (line_EN[3] ? (line_ST[3] ? "OK" : "CUT") : ""));
}

static void buildPingReply(char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';

  uint8_t cutMask = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (!line_EN[i]) continue;
    if (!line_ST[i]) cutMask |= (1u << i);
  }

  char cuts[48];
  buildCutsList(cutMask, cuts, sizeof(cuts));

  int rssi = 99, ber = 99;
  (void)gsm_get_csq(&rssi, &ber);

  snprintf(out, outLen,
           "CG PING OK\nDev:%s\nLoc:%s\nARM:%s\nCUT:%s\nCSQ:%d",
           config.deviceName, config.location, (armed ? "ON" : "OFF"), cuts, rssi);
}

// ================= LINE READING =================
static void readLines() {
  line_ST[0] = !digitalRead(OPTO1_PIN);
  line_ST[1] = !digitalRead(OPTO2_PIN);
  line_ST[2] = !digitalRead(OPTO3_PIN);
  line_ST[3] = !digitalRead(OPTO4_PIN);
}

static uint8_t currentCutMask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (!line_EN[i]) continue;
    if (!line_ST[i]) m |= (1u << i);
  }
  return m;
}

// ================= SMS COMMANDS =================
static void handleIncomingSms(const SmsMessage& sms) {
  if (!isAuthorizedSender(sms.from)) {
    // Silent ignore for security (avoid revealing device existence)
    return;
  }

  char cmd[64];
  strncpy(cmd, sms.text, sizeof(cmd) - 1);
  cmd[sizeof(cmd) - 1] = '\0';
  toUpperTrim(cmd);

  if (strcmp(cmd, "PING") == 0) {
    char reply[220];
    buildPingReply(reply, sizeof(reply));
    (void)send_sms(sms.from, reply);
    return;
  }

  if (strcmp(cmd, "ARM") == 0) {
    uint8_t cutMask = currentCutMask();
    if (cutMask != 0) {
      char cuts[48];
      buildCutsList(cutMask, cuts, sizeof(cuts));

      char reply[160];
      snprintf(reply, sizeof(reply), "CG ARM FAIL\nLines still CUT:%s", cuts);
      (void)send_sms(sms.from, reply);
      return;
    }

    armed = true;
    rtc.armed = 1;

    // Cancel any pending call countdown from a previous incident
    rtc.alertStage = 0;
    rtc.pendingCutMask = 0;
    rtc.callCountdownSec = 0;
    rtc.lastCutMask = 0;

    rtc.seq++;
    rtcSave(rtc);

    (void)send_sms(sms.from, "CG ARMED OK");
    return;
  }

  // Unknown command
  (void)send_sms(sms.from, "CG CMD?\nUse: PING or ARM");
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);
  init_oled();

  // Normal mode: keep WiFi off (reduces current spikes)
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  draw_boot_screen("BOOT", false, false, -1);

  pinMode(CONFIG_BTN, INPUT_PULLUP);
  pinMode(OPTO1_PIN, INPUT);
  pinMode(OPTO2_PIN, INPUT);
  pinMode(OPTO3_PIN, INPUT);
  pinMode(OPTO4_PIN, INPUT);
  stableWaitMs(300);

  loadConfig();
  for (int i = 0; i < 4; i++) {
    line_EN[i] = config.lineEnable[i];
    last_line_ST[i] = true;
  }
  stableWaitMs(200);

  // Load RTC state (or init)
  if (!rtcLoad(rtc)) {
    memset(&rtc, 0, sizeof(rtc));
    rtc.magic = RTC_MAGIC;
    rtc.testsPassed = 0;
    rtc.armed = 1;          // default armed after power-up
    rtc.lastCutMask = 0;
    rtc.alertStage = 0;
    rtc.pendingCutMask = 0;
    rtc.callCountdownSec = 0;
    rtc.seq = 0;
    rtcSave(rtc);
  }

  // If an alert is mid-flow (SMS assumed sent -> countdown -> call), keep device disarmed
  // and resume the countdown across reboots.
  if (rtc.alertStage != 0) {
    rtc.armed = 0;
    rtcSave(rtc);
  }

  armed = (rtc.armed != 0);
  nextCallTickMs = millis() + 1000UL;

  // If user holds config button during boot for 2s, enter config mode
  // without touching the GSM module.
  if (digitalRead(CONFIG_BTN) == LOW) {
    unsigned long t0 = millis();
    while (digitalRead(CONFIG_BTN) == LOW && (unsigned long)(millis() - t0) < 2000UL) {
      yield();
      delay(10);
    }
    if (digitalRead(CONFIG_BTN) == LOW) {
      config_oled();
      stableWaitMs(500);
      startConfigMode(); // never returns
    }
  }

  // Step 1 + 2 + 3: cooldown + self-test only once per power cycle
  if (!rtc.testsPassed) {
    bootCooldownOnce();

    gsmReady = bootSelfTest_AT_NET();
    if (gsmReady) {
      rtc.testsPassed = 1;
      rtc.seq++;
      rtcSave(rtc);
    } else {
      // Show error for a while, then reboot
      draw_boot_screen("GSM FAIL", false, false, -1);
      stableWaitMs(60000UL);
      ESP.restart();
    }
  } else {
    // Skip cooldown + tests on sudden reboot
    gsmReady = true; // assume modem still powered and OK
  }

  // Draw home UI
  readLines();
  draw_banner();
  draw_frame(line_EN, line_ST, gsmReady, gsmReady, armed);

  // Schedulers
  unsigned long now = millis();
  nextUiMs = now + UI_REFRESH_MS;
  nextSmsPollMs = now + 2000UL;

  // Periodic status schedule
  if (config.statusIntervalMinutes > 0) {
    nextStatusDueMs = now + (unsigned long)config.statusIntervalMinutes * 60UL * 1000UL;
  } else {
    nextStatusDueMs = 0;
  }
  nextGsmRetestMs = now + GSM_RETEST_MS;
}

// ================= LOOP =================
void loop() {
  // Config button hold 2s -> stop everything and host config page (no GSM actions)
  pollConfigButton();
  if (configModeRequested) {
    config_oled();
    stableWaitMs(500);
    startConfigMode(); // never returns
  }

  unsigned long now = millis();

  // Read sensors first (needed for emergency check)
  readLines();

  // Incoming SMS commands (PING / ARM)
  if (gsmReady && timeReached(now, nextSmsPollMs)) {
    SmsMessage sms;
    if (gsm_fetch_unread_sms(&sms)) {
      handleIncomingSms(sms);
    }
    nextSmsPollMs = now + SMS_POLL_MS;
  }

  // If GSM was assumed ready but modem got unhappy, we can retest slowly
  if (!gsmReady && timeReached(now, nextGsmRetestMs)) {
    gsmReady = init_gsm();
    nextGsmRetestMs = now + GSM_RETEST_MS;
  }

  // ================= EMERGENCY FLOW =================
  // Goal: If sending SMS causes a reboot (GSM not reset), do NOT re-send SMS forever.
  // Instead: assume SMS is sent, persist a 10s countdown in RTC memory, then place CALLs.

  uint8_t cutMask = currentCutMask();
  bool transitionCut = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (!line_EN[i]) continue;
    if (last_line_ST[i] == true && line_ST[i] == false) {
      transitionCut = true;
    }
    last_line_ST[i] = line_ST[i];
  }

  // 1) Resume pending CALL countdown (survives reboot)
  if (rtc.alertStage == 1) {
    // Stay disarmed while an incident is active
    armed = false;
    rtc.armed = 0;

    // If everything is restored before the call, cancel the pending call.
    if (cutMask == 0) {
      rtc.alertStage = 0;
      rtc.pendingCutMask = 0;
      rtc.callCountdownSec = 0;
      rtc.lastCutMask = 0;
      rtc.seq++;
      rtcSave(rtc);
    } else {
      // Keep masks up-to-date (so we don't re-SMS on reboot)
      if (rtc.pendingCutMask != cutMask || rtc.lastCutMask != cutMask) {
        rtc.pendingCutMask = cutMask;
        rtc.lastCutMask = cutMask;
        rtc.seq++;
        rtcSave(rtc);
      }

      // Countdown tick (stored in RTC memory)
      if (timeReached(now, nextCallTickMs)) {
        if (rtc.callCountdownSec > 0) {
          rtc.callCountdownSec--;
          rtc.seq++;
          rtcSave(rtc);
        }
        nextCallTickMs = now + 1000UL;
      }

      // When countdown hits 0, try calling
      if (rtc.callCountdownSec == 0) {
        if (!gsmReady) {
          gsmReady = init_gsm();
        }
        if (gsmReady) {
          callEmergencyNumbers();

          // Done with call stage; remain disarmed until explicit ARM
          rtc.alertStage = 0;
          rtc.pendingCutMask = 0;
          rtc.callCountdownSec = 0;
          rtc.seq++;
          rtcSave(rtc);
        } else {
          // Modem still unhappy: retry calling in a few seconds
          rtc.callCountdownSec = 5;
          rtc.seq++;
          rtcSave(rtc);
          nextGsmRetestMs = now + GSM_RETEST_MS;
        }
      }
    }
  }

  // 2) Decide whether we should start a NEW incident
  uint8_t newCutsVsRtc = (uint8_t)(cutMask & (uint8_t)(~rtc.lastCutMask));
  bool shouldAlert = (rtc.alertStage == 0) && armed && (cutMask != 0) && (transitionCut || newCutsVsRtc != 0);

  if (shouldAlert && gsmReady) {
    // Persist incident state BEFORE sending SMS.
    // If a reboot happens mid-SMS, next boot will NOT resend SMS; it will continue countdown -> call.
    rtc.lastCutMask = cutMask;
    rtc.pendingCutMask = cutMask;
    rtc.alertStage = 1;
    rtc.callCountdownSec = EMERG_REBOOT_GRACE_SEC;

    armed = false;
    rtc.armed = 0;

    rtc.seq++;
    rtcSave(rtc);
    nextCallTickMs = now + 1000UL;

    // Update UI immediately so the user sees the CUT state
    draw_banner();
    draw_frame(line_EN, line_ST, gsmReady, gsmReady, armed);

    // SMS stage (best-effort)
    char msg[180];
    buildEmergencyMessage(cutMask, msg, sizeof(msg));

    bool smsOk = sendEmergencySmsOnly(msg);
    if (!smsOk) {
      // Modem may be unresponsive; pause GSM activity and retest later.
      gsmReady = false;
      nextGsmRetestMs = now + GSM_RETEST_MS;
    }
  }

  // 3) If all lines are OK again, clear lastCutMask so next incident can alert (once ARMED)
  if (cutMask == 0 && rtc.lastCutMask != 0 && rtc.alertStage == 0) {
    rtc.lastCutMask = 0;
    rtc.seq++;
    rtcSave(rtc);
  }

  // Periodic status messaging
  if (gsmReady && config.statusIntervalMinutes > 0 && nextStatusDueMs != 0 && timeReached(now, nextStatusDueMs)) {
    char st[240];
    buildStatusMessage(st, sizeof(st));
    bool ok = sendToStatusRecipients(st);
    nextStatusDueMs = now + (unsigned long)config.statusIntervalMinutes * 60UL * 1000UL;
    if (!ok) {
      // Retry sooner if failed (instead of waiting full interval)
      nextStatusDueMs = now + STATUS_RETRY_MS;
      gsmReady = false;
      nextGsmRetestMs = now + GSM_RETEST_MS;
    }
  }

  // UI refresh (slow)
  if (timeReached(now, nextUiMs)) {
    draw_banner();
    draw_frame(line_EN, line_ST, gsmReady, gsmReady, armed);
    nextUiMs = now + UI_REFRESH_MS;
  }

  delay(25);
  yield();
}

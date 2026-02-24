#ifndef GSM_H
#define GSM_H

#include <Arduino.h>

// Initialize GSM module (text mode, basic settings) + minimal self-test
// Returns true only if AT OK + network registered
bool init_gsm();

// Basic checks (useful for boot UI)
bool gsm_check_at();
bool gsm_check_net();

// Signal quality (RSSI 0..31, 99 unknown) and BER.
// Returns true if parsed OK.
bool gsm_get_csq(int* rssi, int* ber);

// Send an SMS to a specific number
bool send_sms(const char* number, const char* text);

// Make a call to a specific number
void make_call(const char* number);

// Incoming SMS (unread)
struct SmsMessage {
  char from[20];
  char text[180];
};

// Fetch ONE unread SMS (if any) and delete it from storage.
// Returns true if a message was retrieved.
bool gsm_fetch_unread_sms(SmsMessage* out);

#endif

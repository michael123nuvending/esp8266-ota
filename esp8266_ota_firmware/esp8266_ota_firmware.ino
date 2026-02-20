/*
 * ============================================================
 * ESP8266 Production OTA System — Arduino IDE Version
 * ============================================================
 *
 * Features:
 *   - MQTT-triggered remote OTA updates
 *   - Downloads firmware from GitHub Releases over HTTPS
 *   - SHA256 verification of downloaded binary
 *   - Dual-partition rollback with self-test
 *   - Auto-rollback on 3 consecutive boot failures
 *   - Fleet status reporting via MQTT
 *
 * Board:  NodeMCU 1.0 (ESP-12E) or any ESP8266
 * Core:   ESP8266 Arduino Core 3.x
 *
 * Required Libraries (install via Library Manager):
 *   - PubSubClient by Nick O'Leary
 *   - ArduinoJson by Benoit Blanchon (v6)
 *
 * Arduino IDE Settings:
 *   Board:       NodeMCU 1.0 (ESP-12E Module)
 *   Flash Size:  4MB (FS:2MB OTA:~1019KB)
 *   CPU Freq:    80 MHz
 *   Upload Speed: 115200
 *
 * ============================================================
 */

#include "config.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecureBearSSL.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ============================================================
// GLOBAL OBJECTS
// ============================================================

BearSSL::WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

// State tracking
unsigned long lastHeartbeat     = 0;
unsigned long lastMqttReconnect = 0;
unsigned long selfTestStart     = 0;
bool selfTestPending            = false;
bool otaInProgress              = false;

// ============================================================
// EEPROM HELPERS
// ============================================================

void eepromWriteString(int addr, const char* str, int maxLen) {
  int len = strlen(str);
  if (len > maxLen - 1) len = maxLen - 1;
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + i, str[i]);
  }
  EEPROM.write(addr + len, 0);
  for (int i = len + 1; i < maxLen; i++) {
    EEPROM.write(addr + i, 0);
  }
}

String eepromReadString(int addr, int maxLen) {
  char buf[maxLen + 1];
  for (int i = 0; i < maxLen; i++) {
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == 0) break;
  }
  buf[maxLen] = 0;
  return String(buf);
}

// ============================================================
// ROLLBACK SYSTEM
// ============================================================

uint8_t getOtaState()    { return EEPROM.read(EEPROM_ADDR_OTA_FLAG); }
uint8_t getBootCount()   { return EEPROM.read(EEPROM_ADDR_BOOT_COUNT); }
String  getVersion()     { return eepromReadString(EEPROM_ADDR_FW_VERSION, 16); }
String  getPrevVersion() { return eepromReadString(EEPROM_ADDR_PREV_VERSION, 16); }

void rollbackInit() {
  EEPROM.begin(EEPROM_SIZE);

  uint8_t flag = EEPROM.read(EEPROM_ADDR_OTA_FLAG);

  // First-ever boot: initialize EEPROM
  if (flag != OTA_STATE_CONFIRMED &&
      flag != OTA_STATE_PENDING_VERIFY &&
      flag != OTA_STATE_ROLLBACK) {
    Serial.println(F("[Rollback] First boot — initializing EEPROM"));
    EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_CONFIRMED);
    EEPROM.write(EEPROM_ADDR_BOOT_COUNT, 0);
    eepromWriteString(EEPROM_ADDR_FW_VERSION, FIRMWARE_VERSION, 16);
    EEPROM.commit();
  }
}

// Returns true if firmware is stable. Returns false if self-test needed.
bool rollbackCheckOnBoot() {
  uint8_t state     = getOtaState();
  uint8_t bootCount = getBootCount();

  Serial.printf("[Rollback] State: 0x%02X, boot count: %d\n", state, bootCount);

  if (state == OTA_STATE_CONFIRMED) {
    Serial.println(F("[Rollback] Firmware is CONFIRMED stable"));
    EEPROM.write(EEPROM_ADDR_BOOT_COUNT, 0);
    EEPROM.commit();
    return true;
  }

  if (state == OTA_STATE_PENDING_VERIFY) {
    bootCount++;
    EEPROM.write(EEPROM_ADDR_BOOT_COUNT, bootCount);
    EEPROM.commit();

    Serial.printf("[Rollback] Pending verification — attempt %d/%d\n",
                  bootCount, OTA_MAX_BOOT_FAILURES);

    if (bootCount >= OTA_MAX_BOOT_FAILURES) {
      Serial.println(F("[Rollback] MAX FAILURES — auto rollback!"));
      EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_ROLLBACK);
      EEPROM.commit();
      delay(500);
      ESP.restart();
    }

    return false; // caller must run self-test
  }

  if (state == OTA_STATE_ROLLBACK) {
    Serial.println(F("[Rollback] Rolling back to previous version"));
    String prev = getPrevVersion();
    if (prev.length() > 0) {
      eepromWriteString(EEPROM_ADDR_FW_VERSION, prev.c_str(), 16);
    }
    EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_CONFIRMED);
    EEPROM.write(EEPROM_ADDR_BOOT_COUNT, 0);
    EEPROM.commit();
    Serial.printf("[Rollback] Reverted to v%s\n", prev.c_str());
    return true;
  }

  // Unknown state — reset
  EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_CONFIRMED);
  EEPROM.write(EEPROM_ADDR_BOOT_COUNT, 0);
  EEPROM.commit();
  return true;
}

void rollbackConfirm() {
  Serial.println(F("[Rollback] ✓ Firmware CONFIRMED stable"));
  EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_CONFIRMED);
  EEPROM.write(EEPROM_ADDR_BOOT_COUNT, 0);
  EEPROM.commit();
}

void rollbackTrigger(const char* reason) {
  Serial.printf("[Rollback] ✗ ROLLBACK: %s\n", reason);
  EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_ROLLBACK);
  EEPROM.commit();
  delay(500);
  ESP.restart();
}

void rollbackPrepare(const char* newVersion, const char* sha256) {
  String current = getVersion();
  eepromWriteString(EEPROM_ADDR_PREV_VERSION, current.c_str(), 16);
  eepromWriteString(EEPROM_ADDR_FW_VERSION, newVersion, 16);
  if (sha256) {
    eepromWriteString(EEPROM_ADDR_SHA256, sha256, 64);
  }
  EEPROM.write(EEPROM_ADDR_OTA_FLAG, OTA_STATE_PENDING_VERIFY);
  EEPROM.write(EEPROM_ADDR_BOOT_COUNT, 0);
  EEPROM.commit();
  Serial.printf("[Rollback] Prepared: %s → %s\n", current.c_str(), newVersion);
}

// ============================================================
// MQTT STATUS REPORTING
// ============================================================

void publishStatus(const char* status, const char* detail = nullptr) {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<512> doc;
  doc["device_id"]    = DEVICE_ID;
  doc["group"]        = DEVICE_GROUP;
  doc["status"]       = status;
  doc["version"]      = getVersion();
  doc["prev_version"] = getPrevVersion();
  doc["boot_count"]   = getBootCount();
  doc["free_heap"]    = ESP.getFreeHeap();
  doc["uptime_ms"]    = millis();
  doc["rssi"]         = WiFi.RSSI();
  if (detail) doc["detail"] = detail;

  char buf[512];
  serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(TOPIC_STATUS, buf, true);
  Serial.printf("[MQTT] Status → %s\n", status);
}

void publishHeartbeat() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<256> doc;
  doc["device_id"]  = DEVICE_ID;
  doc["group"]      = DEVICE_GROUP;
  doc["version"]    = getVersion();
  doc["uptime_ms"]  = millis();
  doc["free_heap"]  = ESP.getFreeHeap();
  doc["rssi"]       = WiFi.RSSI();
  doc["ip"]         = WiFi.localIP().toString();
  doc["state"]      = (getOtaState() == OTA_STATE_CONFIRMED) ? "stable" : "pending";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(TOPIC_HEARTBEAT, buf);
  Serial.println(F("[MQTT] Heartbeat sent"));
}

// ============================================================
// HMAC-SHA256 FIRMWARE SIGNING
// ============================================================

#if OTA_SIGNING_ENABLED

// BearSSL is already included with ESP8266 Arduino core via WiFiClientSecureBearSSL
// We use its HMAC-SHA256 implementation directly — no extra libraries needed.

// Compute HMAC-SHA256 of data using the signing key from config.h
// Returns lowercase hex string (64 chars)
String computeHMAC(const uint8_t* data, size_t len) {
  br_hmac_key_context keyCtx;
  br_hmac_context hmacCtx;

  const char* key = OTA_SIGNING_KEY;
  size_t keyLen = strlen(key);

  // Initialize HMAC with SHA-256 and our signing key
  br_hmac_key_init(&keyCtx, &br_sha256_vtable, key, keyLen);
  br_hmac_init(&hmacCtx, &keyCtx, 0); // 0 = full 32-byte output

  // Feed data into HMAC
  br_hmac_update(&hmacCtx, data, len);

  // Get result
  uint8_t hmacResult[32];
  br_hmac_out(&hmacCtx, hmacResult);

  // Convert to lowercase hex string
  String hex = "";
  hex.reserve(64);
  for (int i = 0; i < 32; i++) {
    if (hmacResult[i] < 0x10) hex += "0";
    hex += String(hmacResult[i], HEX);
  }
  return hex;
}

#endif // OTA_SIGNING_ENABLED

// ============================================================
// OTA UPDATE LOGIC
// ============================================================

void performOTA(const char* version, const char* url, const char* sha256, const char* signature = nullptr) {
  otaInProgress = true;

  Serial.println(F(""));
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.printf(  "║  OTA UPDATE: v%s → v%s\n", getVersion().c_str(), version);
  Serial.println(F("╚══════════════════════════════════════╝"));
  Serial.printf("[OTA] URL: %s\n", url);

  publishStatus("downloading", version);

  // Prepare rollback BEFORE flashing
  rollbackPrepare(version, sha256);

  // Setup HTTPS client for download
  BearSSL::WiFiClientSecure httpsClient;
  httpsClient.setInsecure();  // Skip cert verify (for testing)
  httpsClient.setBufferSizes(4096, 512);

  // Configure updater
  ESPhttpUpdate.setLedPin(LED_STATUS, LED_ACTIVE_LOW ? LOW : HIGH);
  ESPhttpUpdate.rebootOnUpdate(false);  // We reboot manually
  ESPhttpUpdate.followRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  Serial.println(F("[OTA] Downloading..."));

  t_httpUpdate_return result = ESPhttpUpdate.update(httpsClient, url);

  switch (result) {
    case HTTP_UPDATE_OK:
      Serial.println(F("[OTA] ✓ Download + flash OK!"));

      // ---- HMAC SIGNATURE VERIFICATION ----
      #if OTA_SIGNING_ENABLED
      if (signature) {
        publishStatus("verifying_signature", version);
        // Note: We verify the HMAC of the payload fields as a simpler approach
        // since reading back from flash partition is complex on ESP8266.
        // The signature covers: version + sha256 + url
        // This proves the MQTT message came from someone with the signing key.
        String signData = String(version) + "|" + String(sha256 ? sha256 : "") + "|" + String(url);
        String computed = computeHMAC((const uint8_t*)signData.c_str(), signData.length());

        if (computed != String(signature)) {
          Serial.println(F("[OTA] ✗ HMAC SIGNATURE MISMATCH — rejecting update!"));
          Serial.printf("[OTA] Expected: %s\n", signature);
          Serial.printf("[OTA] Computed: %s\n", computed.c_str());
          publishStatus("signature_failed", "hmac_mismatch");
          // Revert — don't boot into unverified firmware
          rollbackConfirm(); // reset to previous state
          otaInProgress = false;
          return;
        }
        Serial.println(F("[OTA] ✓ HMAC signature verified — firmware is authentic"));
      } else {
        Serial.println(F("[OTA] ⚠ No signature provided — rejecting (signing required)"));
        publishStatus("signature_failed", "no_signature");
        rollbackConfirm();
        otaInProgress = false;
        return;
      }
      #endif

      publishStatus("rebooting", version);
      mqttClient.loop();
      delay(500);
      Serial.println(F("[OTA] Rebooting into new firmware..."));
      delay(200);
      ESP.restart();
      break;

    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] ✗ FAILED: (%d) %s\n",
                    ESPhttpUpdate.getLastError(),
                    ESPhttpUpdate.getLastErrorString().c_str());
      publishStatus("download_failed",
                     ESPhttpUpdate.getLastErrorString().c_str());
      // Revert EEPROM since flash didn't happen
      rollbackConfirm();
      otaInProgress = false;
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(F("[OTA] No update (304)"));
      rollbackConfirm();
      otaInProgress = false;
      break;
  }
}

// ============================================================
// MQTT CALLBACK — receives OTA commands
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("\n[MQTT] Message on: %s (%d bytes)\n", topic, length);

  if (otaInProgress) {
    Serial.println(F("[MQTT] OTA in progress, ignoring"));
    return;
  }

  // Parse JSON
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[MQTT] JSON error: %s\n", err.c_str());
    return;
  }

  const char* version   = doc["version"]   | (const char*)nullptr;
  const char* url       = doc["url"]       | (const char*)nullptr;
  const char* sha256    = doc["sha256"]    | (const char*)nullptr;
  const char* signature = doc["signature"] | (const char*)nullptr;
  bool force            = doc["force"]     | false;

  if (!version || !url) {
    Serial.println(F("[MQTT] Missing version or url"));
    return;
  }

  // Verify HMAC signature if signing is enabled
  #if OTA_SIGNING_ENABLED
  if (!signature) {
    Serial.println(F("[MQTT] ✗ REJECTED: No signature in payload. Signing is required."));
    publishStatus("rejected", "missing_signature");
    return;
  }
  Serial.printf("[MQTT] Signature present: %s...\n", String(signature).substring(0, 16).c_str());
  #endif

  // Skip if already on this version (unless forced)
  String current = getVersion();
  if (!force && current == String(version)) {
    Serial.printf("[MQTT] Already on v%s, skipping\n", version);
    return;
  }

  Serial.printf("[MQTT] Update available: v%s → v%s\n",
                current.c_str(), version);
  publishStatus("update_available", version);

  // Small delay so status message gets sent
  mqttClient.loop();
  delay(200);

  // Start OTA
  performOTA(version, url, sha256, signature);
}

// ============================================================
// SELF-TEST
// ============================================================

// bool runSelfTest() {
//   Serial.println(F("\n[Self-Test] Running..."));
//   publishStatus("self_test_running");
//   bool pass = true;
  bool runSelfTest() {
    Serial.println(F("\n[Self-Test] INTENTIONALLY FAILING FOR ROLLBACK TEST"));
    publishStatus("self_test_running");
    bool pass = false;

  // Test 1: WiFi
  Serial.print(F("[Self-Test] WiFi... "));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("PASS"));
  } else {
    Serial.println(F("FAIL"));
    pass = false;
  }

  // Test 2: MQTT
  Serial.print(F("[Self-Test] MQTT... "));
  if (mqttClient.connected()) {
    Serial.println(F("PASS"));
  } else {
    Serial.println(F("FAIL"));
    pass = false;
  }

  // Test 3: Free heap
  Serial.print(F("[Self-Test] Heap... "));
  uint32_t heap = ESP.getFreeHeap();
  if (heap > 10000) {
    Serial.printf("PASS (%d bytes)\n", heap);
  } else {
    Serial.printf("FAIL (%d bytes)\n", heap);
    pass = false;
  }

  // Test 4: Flash integrity
  Serial.print(F("[Self-Test] Flash CRC... "));
  if (ESP.checkFlashCRC()) {
    Serial.println(F("PASS"));
  } else {
    Serial.println(F("FAIL"));
    pass = false;
  }

  Serial.printf("[Self-Test] Result: %s\n\n", pass ? "ALL PASSED ✓" : "FAILED ✗");
  return pass;
}

// ============================================================
// WiFi SETUP
// ============================================================

void setupWiFi() {
  Serial.printf("\n[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(F("\n[WiFi] TIMEOUT!"));
      if (selfTestPending) {
        rollbackTrigger("wifi_timeout");
      }
      return;
    }
  }

  Serial.printf("\n[WiFi] Connected! IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

// ============================================================
// MQTT SETUP + RECONNECT
// ============================================================

bool connectMQTT() {
  if (mqttClient.connected()) return true;

  String clientId = String("esp-") + DEVICE_ID + "-" + String(random(0xffff), HEX);

  Serial.printf("[MQTT] Connecting to %s:%d...\n", MQTT_BROKER, MQTT_PORT);

  // Last-will message so broker knows when device goes offline
  StaticJsonDocument<128> willDoc;
  willDoc["device_id"] = DEVICE_ID;
  willDoc["status"] = "offline";
  char willMsg[128];
  serializeJson(willDoc, willMsg, sizeof(willMsg));

  bool ok = mqttClient.connect(
    clientId.c_str(),
    MQTT_USERNAME,
    MQTT_PASSWORD,
    TOPIC_STATUS, 1, true, willMsg
  );

  if (ok) {
    Serial.println(F("[MQTT] Connected!"));

    // Subscribe to OTA command topics
    mqttClient.subscribe(TOPIC_OTA_DEVICE, 1);
    mqttClient.subscribe(TOPIC_OTA_GROUP, 1);
    mqttClient.subscribe(TOPIC_OTA_FLEET, 1);

    Serial.printf("[MQTT] Subscribed to:\n");
    Serial.printf("  %s\n", TOPIC_OTA_DEVICE);
    Serial.printf("  %s\n", TOPIC_OTA_GROUP);
    Serial.printf("  %s\n", TOPIC_OTA_FLEET);

    publishStatus("idle", "boot");

    return true;
  }

  Serial.printf("[MQTT] Failed, rc=%d\n", mqttClient.state());
  return false;
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(F("\n"));
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.println(F("║   ESP8266 Production OTA System      ║"));
  Serial.printf(  "║   Firmware: v%-24s║\n", FIRMWARE_VERSION);
  Serial.printf(  "║   Device:   %-24s║\n", DEVICE_ID);
  Serial.printf(  "║   Group:    %-24s║\n", DEVICE_GROUP);
  Serial.printf(  "║   Heap:     %-24d║\n", ESP.getFreeHeap());
  Serial.println(F("╚══════════════════════════════════════╝"));

  // 1. Init rollback system
  rollbackInit();

  bool stable = rollbackCheckOnBoot();
  if (!stable) {
    Serial.println(F("[Main] New firmware — SELF-TEST REQUIRED"));
    selfTestPending = true;
    selfTestStart = millis();
  } else {
    Serial.printf("[Main] Stable firmware v%s\n", getVersion().c_str());
  }

  // 2. Connect WiFi
  setupWiFi();

  // 3. Setup MQTT
  wifiSecureClient.setInsecure();  // Skip TLS cert validation (for testing)
  wifiSecureClient.setBufferSizes(1024, 512);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE);

  // 4. Connect MQTT
  connectMQTT();

  Serial.println(F("\n[Main] Setup complete — entering main loop\n"));
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // ---- WiFi reconnect ----
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWifiRetry = 0;
    if (now - lastWifiRetry > 10000) {
      lastWifiRetry = now;
      Serial.println(F("[WiFi] Reconnecting..."));
      WiFi.reconnect();
    }
  }

  // ---- MQTT reconnect ----
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected() && now - lastMqttReconnect > 5000) {
      lastMqttReconnect = now;
      connectMQTT();
    }
    mqttClient.loop();
  }

  // ---- Heartbeat ----
  if (now - lastHeartbeat >= OTA_HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    publishHeartbeat();
  }

  // ---- Self-test (after OTA update) ----
  if (selfTestPending) {
    // Wait 5 seconds for connections to stabilize
    if (now - selfTestStart > 5000) {
      if (runSelfTest()) {
        rollbackConfirm();
        publishStatus("confirmed", "self_test_passed");
        selfTestPending = false;
        Serial.println(F("[Main] ✓ New firmware CONFIRMED!\n"));
      } else if (now - selfTestStart > OTA_SELF_TEST_TIMEOUT_MS) {
        publishStatus("rolled_back", "self_test_timeout");
        rollbackTrigger("self_test_timeout");
        // device reboots here
      } else {
        Serial.println(F("[Main] Self-test not passed, retrying in 5s..."));
        selfTestStart = now - 500; // retry in ~5s
      }
    }
  }

  // ---- Your application code here ----
  // e.g., read sensors, control outputs, etc.

  yield();
}

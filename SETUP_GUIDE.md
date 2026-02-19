# Step-by-Step Setup Guide
## Arduino IDE + GitHub Actions + HiveMQ + ESP8266 OTA

Follow these steps **in order**. Each step builds on the previous one.

---

## STEP 1: Install Arduino IDE Libraries

Open Arduino IDE → **Sketch** → **Include Library** → **Manage Libraries**

Search and install these two:

| Library | Author | Version |
|---------|--------|---------|
| **PubSubClient** | Nick O'Leary | 2.8+ |
| **ArduinoJson** | Benoit Blanchon | 6.x (NOT 7.x) |

## STEP 2: Install ESP8266 Board Support

1. Open **File** → **Preferences**
2. In **"Additional Board Manager URLs"**, add:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Open **Tools** → **Board** → **Board Manager**
4. Search **"esp8266"** → Install **"esp8266 by ESP8266 Community"**

## STEP 3: Configure Your Credentials

Open the file `esp8266_ota_firmware/config.h` and fill in these 6 values:

```c
// YOUR WiFi
#define WIFI_SSID           "MyHomeWiFi"        // ← change this
#define WIFI_PASSWORD       "MyWiFiPassword"     // ← change this

// YOUR HiveMQ Cloud (from the cluster Overview page)
#define MQTT_BROKER         "abc123.s1.eu.hivemq.cloud"  // ← change this
#define MQTT_USERNAME       "myuser"              // ← change this
#define MQTT_PASSWORD       "mypassword"          // ← change this

// YOUR GitHub (you'll create this in Step 5)
#define GITHUB_USER         "yourgithubname"      // ← change this
#define GITHUB_REPO         "esp8266-ota"         // ← change this
```

**Where to find HiveMQ credentials:**
- Log in at https://console.hivemq.cloud/
- Click your cluster → **Overview** tab → copy the hostname
- Click **Access Management** → your username/password

## STEP 4: Flash the Initial Firmware (v1.0.0)

1. Connect your ESP8266 via USB
2. Open `esp8266_ota_firmware/esp8266_ota_firmware.ino` in Arduino IDE
3. Go to **Tools** and set:

   | Setting | Value |
   |---------|-------|
   | Board | NodeMCU 1.0 (ESP-12E Module) |
   | Flash Size | 4MB (FS:2MB OTA:~1019KB) |
   | CPU Frequency | 80 MHz |
   | Upload Speed | 115200 |
   | Port | (your ESP8266's COM port) |

4. Click **Upload** (→ button)
5. Open **Serial Monitor** (magnifying glass icon), set baud to **115200**

You should see:
```
╔══════════════════════════════════════╗
║   ESP8266 Production OTA System      ║
║   Firmware: v1.0.0                   ║
║   Device:   esp-001                  ║
╚══════════════════════════════════════╝

[WiFi] Connecting to MyHomeWiFi....
[WiFi] Connected! IP: 192.168.1.42  RSSI: -45 dBm
[MQTT] Connecting to abc123.s1.eu.hivemq.cloud:8883...
[MQTT] Connected!
[MQTT] Subscribed to:
  ota/device/esp-001
  ota/group/production
  ota/fleet/all
```

**If it says `[MQTT] Failed, rc=-2`:**
- Double-check your broker hostname (no `https://` prefix)
- Verify username/password in HiveMQ Access Management
- Make sure port is 8883

**If WiFi times out:**
- Check SSID and password (case-sensitive)
- Make sure it's a 2.4GHz network (ESP8266 doesn't support 5GHz)

**✓ Once you see "MQTT Connected", move to Step 5.**

---

## STEP 5: Create GitHub Repository

1. Go to https://github.com/new
2. Create a new repo:
   - Name: `esp8266-ota` (must match `GITHUB_REPO` in config.h)
   - Visibility: **Public** (so ESP8266 can download releases without auth)
   - Do NOT initialize with README (we'll push our code)

3. On your computer, open a terminal in the project folder:

```bash
cd esp8266-ota-arduino

git init
git add .
git commit -m "Initial commit: ESP8266 OTA system v1.0.0"

git remote add origin https://github.com/YOUR_USERNAME/esp8266-ota.git
git branch -M main
git push -u origin main
```

**Important:** Make sure `config.h` is in `.gitignore` so you don't leak your passwords. Check by running:
```bash
cat .gitignore
```
It should contain `**/config.h`. If not, add it before pushing.

## STEP 6: Add GitHub Secrets

Your MQTT credentials need to be stored as GitHub Secrets so the CI/CD pipeline can publish MQTT notifications.

1. Go to your repo on GitHub
2. Click **Settings** tab → **Secrets and variables** → **Actions**
3. Click **"New repository secret"** and add each:

| Secret Name | Value | Example |
|-------------|-------|---------|
| `MQTT_BROKER` | Your HiveMQ hostname | `abc123.s1.eu.hivemq.cloud` |
| `MQTT_PORT` | `8883` | `8883` |
| `MQTT_USERNAME` | Your HiveMQ username | `myuser` |
| `MQTT_PASSWORD` | Your HiveMQ password | `mypassword` |
| `MQTT_USE_TLS` | `true` | `true` |

**You should have 5 secrets when done.**

## STEP 7: Test the Full Pipeline 🚀

Now the fun part — make a code change, tag it, and watch your ESP8266 update itself over the internet!

### 7a: Make a visible change

Open `esp8266_ota_firmware.ino` and find the `loop()` function. Add a print statement so you can confirm the new version is running:

```cpp
// In loop(), after the yield(); line, add:
  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 30000) {
    lastMsg = millis();
    Serial.println(F(">>> Hello from v1.1.0! OTA works! <<<"));
  }
```

### 7b: Commit, tag, and push

```bash
git add .
git commit -m "feat: add hello message for v1.1.0"
git tag v1.1.0
git push origin main --tags
```

### 7c: Watch GitHub Actions

1. Go to your repo → **Actions** tab
2. You'll see the **"Build & Deploy OTA"** workflow running
3. It goes through 3 jobs:
   - **Build Firmware** → compiles with Arduino CLI
   - **Create GitHub Release** → uploads .bin to a release
   - **Notify Devices via MQTT** → publishes update notification

### 7d: Watch your ESP8266 update!

Keep your Serial Monitor open. Within seconds of the MQTT notification, you should see:

```
[MQTT] Message on: ota/fleet/all (234 bytes)
[MQTT] Update available: v1.0.0 → v1.1.0

╔══════════════════════════════════════╗
║  OTA UPDATE: v1.0.0 → v1.1.0
╚══════════════════════════════════════╝
[OTA] Downloading...
[OTA] ✓ Download + flash OK!
[OTA] Rebooting into new firmware...

  ... reboot ...

╔══════════════════════════════════════╗
║   ESP8266 Production OTA System      ║
║   Firmware: v1.1.0                   ║
╚══════════════════════════════════════╝

[Rollback] State: 0xBB, boot count: 1
[Rollback] Pending verification — attempt 1/3
[Main] New firmware — SELF-TEST REQUIRED

[Self-Test] Running...
[Self-Test] WiFi... PASS
[Self-Test] MQTT... PASS
[Self-Test] Heap... PASS (32456 bytes)
[Self-Test] Flash CRC... PASS
[Self-Test] Result: ALL PASSED ✓

[Rollback] ✓ Firmware CONFIRMED stable
[Main] ✓ New firmware CONFIRMED!

>>> Hello from v1.1.0! OTA works! <<<
```

**🎉 That's it! You just did a production-grade OTA update over the internet!**

---

## STEP 8: Test the Rollback System (Optional but Recommended)

To verify rollback works, intentionally push broken firmware:

### 8a: Create a firmware that fails self-test

In `esp8266_ota_firmware.ino`, temporarily break the `runSelfTest()` function:

```cpp
bool runSelfTest() {
  Serial.println(F("[Self-Test] INTENTIONALLY FAILING for rollback test"));
  publishStatus("self_test_running");
  return false;  // ← always fail
}
```

### 8b: Push as v1.2.0

```bash
git add .
git commit -m "test: intentionally broken firmware for rollback test"
git tag v1.2.0
git push origin main --tags
```

### 8c: Watch the rollback

Serial Monitor should show:
```
[OTA] ✓ Download + flash OK!
[OTA] Rebooting...

  ... reboot 1 ...
[Rollback] Pending verification — attempt 1/3
[Self-Test] INTENTIONALLY FAILING
[Main] Self-test not passed, retrying...

  ... eventually times out or hits 3 reboots ...

[Rollback] MAX FAILURES — auto rollback!

  ... reboot ...

[Rollback] Rolling back to previous version
[Rollback] Reverted to v1.1.0
[Main] Stable firmware v1.1.0
>>> Hello from v1.1.0! OTA works! <<<
```

**The device automatically recovered!**

### 8d: Fix and redeploy

Revert your change, push v1.2.1:

```bash
# Undo the break
git checkout -- esp8266_ota_firmware/esp8266_ota_firmware.ino
# Make your real changes...
git add . && git commit -m "fix: restore self-test"
git tag v1.2.1
git push origin main --tags
```

---

## Troubleshooting Quick Reference

| Problem | Fix |
|---------|-----|
| `rc=-2` on MQTT connect | Wrong broker hostname or port. No `https://` prefix. |
| WiFi timeout | 2.4GHz only. Check SSID/password. |
| GitHub Actions fails at "Compile" | Missing library install — check workflow logs |
| ESP8266 doesn't receive MQTT | Check subscribed topics match what Actions publishes |
| Download 404 | Make sure repo is **public**, or the release was created |
| Download SSL error | `setInsecure()` should handle this for testing |
| Always rolling back | Check which self-test fails in serial log |
| `EEPROM_SIZE` errors | Make sure ESP8266 board package is 3.x |

---

## Folder Structure

```
esp8266-ota-arduino/
├── esp8266_ota_firmware/
│   ├── esp8266_ota_firmware.ino   ← main sketch (open this in Arduino IDE)
│   └── config.h                   ← YOUR credentials (gitignored)
├── .github/
│   └── workflows/
│       └── build-and-deploy.yml   ← CI/CD pipeline
├── scripts/
│   ├── mqtt_notify.py             ← called by GitHub Actions
│   └── monitor_fleet.py           ← run locally to watch fleet
├── .gitignore
└── SETUP_GUIDE.md                 ← this file
```

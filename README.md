# ESP8266 Production OTA Update System

### Remote firmware updates via GitHub + MQTT with automatic rollback

Push a git tag → GitHub builds your code → ESP8266 updates itself over the internet.
If the update fails, the device automatically rolls back to the previous working version.

---

## How It Works (The Big Picture)

```
YOU (Developer)                        CLOUD                         YOUR DEVICE
─────────────                    ─────────────────                  ──────────────
                                 
1. Edit code                                                        
2. git tag v1.2.0               
3. git push --tags ──────────→  4. GitHub Actions:                  
                                    - Compiles your code             
                                    - Creates firmware.bin           
                                    - Uploads to GitHub Release      
                                    - Sends MQTT message ─────────→ 5. ESP8266 receives
                                                                       "v1.2.0 ready!"
                                                                    6. Downloads .bin
                                 ←──────────────────────────────────   from GitHub
                                                                    7. Flashes new firmware
                                                                    8. Reboots
                                                                    9. Runs self-test
                                                                    10. If OK → confirmed ✓
                                                                        If FAIL → rollback ✗
```

**Two protocols are used:**
- **MQTT** (port 8883) — lightweight messaging. Used to tell the device "update available" and for the device to report its status back. Like a text message.
- **HTTPS** (port 443) — used to download the actual firmware file (~300KB) from GitHub Releases. Like downloading a file from a website.

---

## Prerequisites

| What | Why | Where to get it |
|------|-----|-----------------|
| **ESP8266 board** | NodeMCU, Wemos D1 Mini, etc. | You already have this |
| **Arduino IDE** | To flash the initial firmware via USB | https://www.arduino.cc/en/software |
| **Git** | To push code and tags | https://git-scm.com/downloads |
| **GitHub account** | Hosts your code + builds firmware + hosts .bin files | https://github.com |
| **HiveMQ Cloud account** | Free MQTT broker | https://www.hivemq.com/mqtt-cloud-broker/ |
| **USB cable** | For the initial flash only. After that, everything is wireless. | |

---

## STEP 1: Install Arduino IDE Libraries

Open Arduino IDE → **Sketch** → **Include Library** → **Manage Libraries**

Install these two:

| Library | Author | Version |
|---------|--------|---------|
| **PubSubClient** | Nick O'Leary | 2.8+ |
| **ArduinoJson** | Benoit Blanchon | **6.x** (NOT 7.x) |

---

## STEP 2: Install ESP8266 Board Support

1. Open **File** → **Preferences**
2. In **"Additional Board Manager URLs"**, paste:
```
https://arduino.esp8266.com/stable/package_esp8266com_index.json
```
3. Click OK
4. Open **Tools** → **Board** → **Board Manager**
5. Search `esp8266` → Install **"esp8266 by ESP8266 Community"**

---

## STEP 3: Set Up HiveMQ Cloud (Free MQTT Broker)

1. Go to https://console.hivemq.cloud/
2. Sign up (free, no credit card)
3. Click **"Create New Cluster"** (free tier)
4. Once created, go to **Overview** tab:
   - Copy the **hostname** (looks like: `abc123def456.s1.eu.hivemq.cloud`)
   - Note the **port**: `8883`
5. Go to **Access Management** tab:
   - Create a username and password
   - **Save these — you'll need them in the next step**

---

## STEP 4: Configure Your Credentials

Open the file `esp8266_ota_firmware/config.h` and change **ONLY** these 5 lines:

```c
#define WIFI_SSID           "YourWiFiName"                          // ← your WiFi name
#define WIFI_PASSWORD       "YourWiFiPassword"                      // ← your WiFi password
#define MQTT_BROKER         "abc123.s1.eu.hivemq.cloud"             // ← from HiveMQ Overview
#define MQTT_USERNAME       "your-hivemq-username"                  // ← from HiveMQ Access Management
#define MQTT_PASSWORD       "your-hivemq-password"                  // ← from HiveMQ Access Management
```

> **IMPORTANT:** This `config.h` with your real credentials stays on your computer only.
> It is listed in `.gitignore` so it won't be uploaded to GitHub.

---

## STEP 5: Flash Initial Firmware via USB (One Time Only)

1. Connect your ESP8266 via USB cable
2. Open `esp8266_ota_firmware/esp8266_ota_firmware.ino` in Arduino IDE
3. Go to **Tools** and set these settings:

| Setting | Value |
|---------|-------|
| **Board** | NodeMCU 1.0 (ESP-12E Module) |
| **Flash Size** | 4MB (FS:2MB OTA:~1019KB) |
| **CPU Frequency** | 80 MHz |
| **Upload Speed** | 115200 |
| **Port** | (select your ESP8266's COM/USB port) |

4. Click the **Upload** button (→ arrow)
5. Open **Tools** → **Serial Monitor**, set baud rate to **115200**

### You should see:

```
╔══════════════════════════════════════╗
║   ESP8266 Production OTA System      ║
║   Firmware: v1.0.0                   ║
║   Device:   esp-001                  ║
║   Group:    production               ║
╚══════════════════════════════════════╝

[WiFi] Connecting to YourWiFiName....
[WiFi] Connected! IP: 192.168.1.42  RSSI: -45 dBm
[MQTT] Connecting to abc123.s1.eu.hivemq.cloud:8883...
[MQTT] Connected!
[MQTT] Subscribed to:
  ota/device/esp-001
  ota/group/production
  ota/fleet/all
[MQTT] Status → idle

[Main] Setup complete — entering main loop
```

### Troubleshooting:

| Problem | Fix |
|---------|-----|
| `[WiFi] TIMEOUT!` | Wrong SSID/password, or your WiFi is 5GHz (ESP8266 only supports 2.4GHz) |
| `[MQTT] Failed, rc=-2` | Wrong broker hostname. No `https://` prefix. Just the hostname. |
| `[MQTT] Failed, rc=-4` | Wrong MQTT username/password |
| No COM port shown | Install CH340 or CP2102 USB driver for your board |

### ✅ Once you see `[MQTT] Connected!` — your device is ready. You can unplug USB. All future updates happen wirelessly.

---

## STEP 6: Create GitHub Repository

1. Go to https://github.com/new
2. Settings:
   - **Repository name:** `esp8266-ota`
   - **Visibility:** `Public` ← IMPORTANT (so ESP8266 can download releases without auth)
   - ✅ Check **"Add a README file"**
3. Click **"Create repository"**

---

## STEP 7: Set Up Folder Structure

On your computer, create this folder structure with all the project files:

```
esp8266-ota/
├── .github/
│   └── workflows/
│       └── build-and-deploy.yml    ← CI/CD pipeline (from this project)
├── .gitignore                      ← prevents uploading your passwords
├── esp8266_ota_firmware/
│   ├── esp8266_ota_firmware.ino    ← main firmware code
│   └── config.h                    ← YOUR credentials (stays local, not pushed)
├── scripts/
│   ├── mqtt_notify.py              ← used by GitHub Actions to send MQTT
│   └── monitor_fleet.py            ← optional: monitor your devices
└── README.md
```

### Commands to set up:

```bash
# Create the folders
mkdir -p esp8266-ota/.github/workflows
mkdir -p esp8266-ota/esp8266_ota_firmware
mkdir -p esp8266-ota/scripts

# Copy the files into the correct locations:
# - build-and-deploy.yml  →  esp8266-ota/.github/workflows/
# - esp8266_ota_firmware.ino  →  esp8266-ota/esp8266_ota_firmware/
# - config.h  →  esp8266-ota/esp8266_ota_firmware/
# - mqtt_notify.py  →  esp8266-ota/scripts/
# - monitor_fleet.py  →  esp8266-ota/scripts/
# - .gitignore  →  esp8266-ota/
```

---

## STEP 8: Push Code to GitHub

```bash
cd esp8266-ota

git init
git add .
git commit -m "Initial commit: ESP8266 OTA system v1.0.0"
git remote add origin https://github.com/YOUR_USERNAME/esp8266-ota.git
git branch -M main
git pull origin main --allow-unrelated-histories
git push origin main --force
```

> When asked for **password**, use a **Personal Access Token** (not your GitHub password).
> Create one at: GitHub → Settings → Developer settings → Personal access tokens → Generate new token (classic)
> Required scopes: ✅ `repo` and ✅ `workflow`

### Verify:

Go to `https://github.com/YOUR_USERNAME/esp8266-ota` and check that you see:
- `.github/` folder
- `esp8266_ota_firmware/` folder
- `scripts/` folder

---

## STEP 9: Add GitHub Secrets

Your MQTT credentials need to be stored as GitHub Secrets so the CI/CD pipeline can send MQTT notifications after building.

1. Go to your repo on GitHub
2. Click **Settings** tab (top row)
3. Left sidebar: **Secrets and variables** → **Actions**
4. Click **"New repository secret"**
5. Add these 5 secrets one by one:

| Secret Name | Value | Example |
|-------------|-------|---------|
| `MQTT_BROKER` | Your HiveMQ hostname | `abc123def.s1.eu.hivemq.cloud` |
| `MQTT_PORT` | `8883` | `8883` |
| `MQTT_USERNAME` | Your HiveMQ username | `myuser` |
| `MQTT_PASSWORD` | Your HiveMQ password | `mypassword123` |
| `MQTT_USE_TLS` | `true` | `true` |

---

## STEP 10: Push Your First OTA Update! 🚀

### 10a. Make a code change

Open `esp8266_ota_firmware/esp8266_ota_firmware.ino` and find the end of the `loop()` function.
Right **before** the `yield();` line (around line 568), add your new code:

```cpp
  // ---- Your application code here ----
  // e.g., read sensors, control outputs, etc.

  // v1.1.0 — my first OTA update!
  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 30000) {
    lastMsg = millis();
    Serial.println(F(">>> Hello from v1.1.0! OTA works! <<<"));
  }

  yield();
}
```

### 10b. Commit, tag, and push

```bash
cd esp8266-ota

git add .
git commit -m "feat: add hello message for v1.1.0"
git tag v1.1.0
git push origin main --tags
```

### 10c. Watch GitHub Actions

1. Go to your repo → **Actions** tab (top row, 4th tab)
2. You'll see **"Build & Deploy OTA"** running
3. It has 3 jobs:
   - ⏳ **Build Firmware** — compiles your code (~2 min)
   - ⏳ **Create GitHub Release** — uploads .bin file
   - ⏳ **Notify Devices via MQTT** — sends update notification

### 10d. Watch your ESP8266 update

If your Serial Monitor is open, you'll see:

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

### 🎉 Congratulations! You just did a wireless OTA update over the internet!

---

## How to Push Future Updates

Every time you want to update your devices:

```bash
# 1. Edit your code in esp8266_ota_firmware.ino
# 2. Then run:
git add .
git commit -m "describe what you changed"
git tag v1.2.0
git push origin main --tags
```

That's it. Your ESP8266 updates automatically within seconds.

**Version numbering:** Use semantic versioning: `v1.0.0` → `v1.1.0` → `v1.2.0` → `v2.0.0`
Each tag must be unique. You can't reuse `v1.1.0`.

---

## Updating from GitHub Website (No Terminal Needed)

If someone on your team doesn't have Git installed or isn't comfortable with the terminal, they can do **everything from the GitHub website**.

### Method A: Edit Code + Create a Tag (Full Update)

**Step 1: Edit the code on GitHub**

1. Go to your repo: `https://github.com/michael123nuvending/esp8266-ota`
2. Click `esp8266_ota_firmware/` folder
3. Click `esp8266_ota_firmware.ino`
4. Click the **pencil icon** ✏️ (top right of the file) to edit
5. Make your code changes (add your new code before `yield();` at the bottom)
6. Click **"Commit changes"** (green button)
7. In the popup:
   - Commit message: `feat: describe your change`
   - Select: **"Commit directly to the main branch"**
   - Click **"Commit changes"**

**Step 2: Create a release tag to trigger the build**

1. On your repo page, look at the right sidebar → click **"Releases"**
   - If you don't see it, go to: `https://github.com/YOUR_USERNAME/esp8266-ota/releases`
2. Click **"Draft a new release"** (top right)
3. Click **"Choose a tag"** dropdown
4. Type a new version tag in the box: `v1.2.0` (must start with `v`)
5. Click **"Create new tag: v1.2.0 on publish"**
6. Fill in:
   - Release title: `Firmware v1.2.0`
   - Description: `What changed in this version`
7. Click **"Publish release"** (green button)

**That's it!** Publishing the release creates the `v1.2.0` tag, which triggers GitHub Actions to build and deploy.

Go to the **Actions** tab to watch the pipeline run. Your ESP8266 will update within seconds of the pipeline finishing.

### Method B: Re-run a Previous Build (Manual Trigger)

If the code is already committed and you just want to trigger a build:

1. Go to **Actions** tab
2. Click **"Build & Deploy OTA"** in the left sidebar
3. Click **"Run workflow"** dropdown (top right)
4. Fill in:
   - **Version:** `1.3.0` (no `v` prefix here)
   - **Deploy target:** choose `fleet`, `canary`, `staging`, or `production`
5. Click **"Run workflow"** (green button)

This builds and deploys without needing to create a new git tag.

### Quick Comparison:

| Action | Terminal | GitHub Website |
|--------|----------|----------------|
| Edit code | Edit in Arduino IDE / VS Code | Click ✏️ pencil icon on GitHub |
| Commit | `git add . && git commit -m "msg"` | Click "Commit changes" button |
| Create tag + trigger build | `git tag v1.2.0 && git push --tags` | Releases → Draft new release → Publish |
| Trigger build without code change | Not needed | Actions → Run workflow |
| View build logs | Not available locally | Actions tab → click workflow run |

---

## Understanding the Code: What to Change vs What NOT to Change

### 🟢 SAFE TO CHANGE — Your Application Code

The **only place** you should add your own code is at the bottom of the `loop()` function:

```cpp
void loop() {

  // ... OTA system code above (don't touch) ...

  // ============================================================
  //  ↓↓↓ YOUR CODE GOES HERE ↓↓↓
  // ============================================================

  // Example: Read a sensor every 5 seconds
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 5000) {
    lastRead = millis();
    int sensorValue = analogRead(A0);
    Serial.printf("Sensor: %d\n", sensorValue);
  }

  // Example: Control an LED
  // digitalWrite(D1, HIGH);

  // Example: Publish sensor data to MQTT
  // if (mqttClient.connected()) {
  //   char msg[64];
  //   snprintf(msg, sizeof(msg), "{\"temp\": %d}", sensorValue);
  //   mqttClient.publish("sensors/esp-001/temperature", msg);
  // }

  // ============================================================
  //  ↑↑↑ YOUR CODE GOES HERE ↑↑↑
  // ============================================================

  yield();  // ← keep this as the last line
}
```

You can also add your own `setup()` code at the bottom of the `setup()` function:

```cpp
void setup() {
  // ... OTA system setup above (don't touch) ...

  // 4. Connect MQTT
  connectMQTT();

  Serial.println(F("\n[Main] Setup complete — entering main loop\n"));

  // ============================================================
  //  ↓↓↓ YOUR SETUP CODE GOES HERE ↓↓↓
  // ============================================================

  // Example: Set pin modes
  // pinMode(D1, OUTPUT);
  // pinMode(D2, INPUT);

  // Example: Initialize a sensor
  // dht.begin();

  // ============================================================
  //  ↑↑↑ YOUR SETUP CODE GOES HERE ↑↑↑
  // ============================================================
}
```

### You can also safely add:

- New `#include` lines at the top (after the existing includes)
- New global variables (after line 52)
- New helper functions (add them before `setup()`)
- Additional self-test checks inside `runSelfTest()` (see below)

### Adding custom self-test checks:

```cpp
bool runSelfTest() {
  // ... existing tests (WiFi, MQTT, Heap, Flash CRC) ...

  // ADD YOUR OWN TESTS HERE:
  // Example: Check if your sensor is responding
  // Serial.print(F("[Self-Test] Sensor... "));
  // if (analogRead(A0) > 0) {
  //   Serial.println(F("PASS"));
  // } else {
  //   Serial.println(F("FAIL"));
  //   pass = false;
  // }

  Serial.printf("[Self-Test] Result: %s\n\n", pass ? "ALL PASSED ✓" : "FAILED ✗");
  return pass;
}
```

### 🔴 DO NOT CHANGE — OTA System Code

**Do NOT modify anything in these sections or the OTA/rollback system will break:**

| Lines | Section | What it does |
|-------|---------|--------------|
| 30-38 | `#include` statements | Libraries needed for OTA |
| 44-52 | Global objects | MQTT client, state variables |
| 58-78 | EEPROM helpers | Read/write to flash memory |
| 84-185 | Rollback system | All `rollback*` functions — handles dual-partition switching |
| 191-228 | MQTT status reporting | `publishStatus()`, `publishHeartbeat()` — reports to MQTT |
| 235-281 | OTA update logic | `performOTA()` — downloads and flashes firmware |
| 289-339 | MQTT callback | `mqttCallback()` — receives update commands |
| 346-389 | Self-test | `runSelfTest()` — you CAN add tests, but don't remove existing ones |
| 396-465 | WiFi + MQTT setup | Connection handling and reconnection |
| 471-510 | `setup()` top half | System initialization (add your code at the bottom) |
| 517-543 | `loop()` top half | WiFi/MQTT reconnect, heartbeat, self-test handler |

### 🟡 CONFIG ONLY — config.h

`config.h` is for credentials and settings. Change values, but don't rename or remove any `#define`:

| Define | Safe to change? | Notes |
|--------|----------------|-------|
| `DEVICE_ID` | ✅ Yes | Change for each device (e.g., "esp-001", "esp-002") |
| `DEVICE_GROUP` | ✅ Yes | For group updates: "production", "staging", "canary" |
| `WIFI_SSID` | ✅ Yes | Your WiFi network name |
| `WIFI_PASSWORD` | ✅ Yes | Your WiFi password |
| `MQTT_BROKER` | ✅ Yes | Your HiveMQ hostname |
| `MQTT_USERNAME` | ✅ Yes | Your HiveMQ username |
| `MQTT_PASSWORD` | ✅ Yes | Your HiveMQ password |
| `FIRMWARE_VERSION` | ⚠️ Don't bother | GitHub Actions overrides this during build |
| `OTA_HEARTBEAT_INTERVAL` | ✅ Yes | How often device sends heartbeat (default: 5 min) |
| `OTA_MAX_BOOT_FAILURES` | ✅ Yes | How many failed boots before rollback (default: 3) |
| Everything else | ❌ No | EEPROM layout, state flags, topic patterns |

---

## Monitoring Your Devices

### Option 1: MQTT Explorer (Desktop App)

1. Download from https://mqtt-explorer.com/
2. Connect with your HiveMQ credentials:
   - Host: your HiveMQ hostname
   - Port: 8883
   - Username/Password: your HiveMQ credentials
   - ✅ Enable SSL/TLS
3. Once connected, you'll see your device's messages in the topic tree:

```
ota/
├── fleet/
│   └── all              ← update command from GitHub Actions
├── heartbeat/
│   └── esp-001          ← device heartbeat (every 5 min)
└── status/
    └── esp-001          ← device status updates
```

### Option 2: Monitor Script (Terminal)

```bash
pip install paho-mqtt

MQTT_BROKER="your-host.s1.eu.hivemq.cloud" \
MQTT_PORT="8883" \
MQTT_USERNAME="your-username" \
MQTT_PASSWORD="your-password" \
MQTT_USE_TLS="true" \
python3 scripts/monitor_fleet.py
```

### Status Messages Explained:

| Status | Meaning |
|--------|---------|
| `idle` | Device is running normally, waiting for updates |
| `update_available` | Device received an update notification |
| `downloading` | Downloading firmware.bin from GitHub |
| `rebooting` | Download done, rebooting into new firmware |
| `self_test_running` | New firmware is testing itself |
| `confirmed` | ✅ Update successful, new firmware is stable |
| `download_failed` | ❌ Download failed (check URL, internet) |
| `rolled_back` | ⟲ Update failed, reverted to previous version |
| `offline` | Device disconnected from MQTT |

---

## How Rollback Works

The system uses EEPROM flags and a boot counter to detect failed updates:

```
Normal operation:
  EEPROM flag = CONFIRMED (0xAA)
  Boot count = 0
  → Device runs normally

After OTA flash:
  EEPROM flag = PENDING_VERIFY (0xBB)
  Boot count = 0
  → Device reboots into new firmware

New firmware boots:
  Boot count → 1
  Runs self-test (WiFi? MQTT? Heap? Flash CRC?)
  
  If ALL PASS:
    Flag → CONFIRMED
    Boot count → 0
    ✅ Done!
  
  If ANY FAIL:
    Wait and retry...
    If timeout (60 seconds) → trigger rollback
  
  If device crashes/hangs:
    Watchdog reboots → boot count goes to 2, then 3
    After 3 failed boots → auto rollback
    
Rollback:
  Flag → ROLLBACK (0xCC)
  Reboot → old firmware loads
  Flag → CONFIRMED
  ✅ Device is safe on previous version
```

### Testing rollback (optional):

To verify rollback works, temporarily break the self-test:

```cpp
bool runSelfTest() {
  Serial.println(F("[Self-Test] INTENTIONALLY FAILING"));
  return false;  // ← always fail
}
```

Push as a new version, watch it fail and automatically revert:

```bash
git add .
git commit -m "test: broken firmware for rollback test"
git tag v1.2.0
git push origin main --tags
```

**Remember to fix it after testing!**

---

## Targeting Specific Devices or Groups

### Update all devices:
```bash
git tag v1.3.0
git push origin main --tags
```
This publishes to `ota/fleet/all` — every device gets it.

### Update a specific group:
Use the GitHub Actions manual trigger:
1. Go to **Actions** tab → **Build & Deploy OTA** → **Run workflow**
2. Enter version: `1.3.0`
3. Select target: `canary` or `staging` or `production`

### Update a single device:
Use the MQTT notify script directly:
```bash
python scripts/mqtt_notify.py \
  --version "1.3.0" \
  --sha256 "GET_FROM_RELEASE_PAGE" \
  --repo "YOUR_USERNAME/esp8266-ota" \
  --device "esp-001"
```

### Setting up multiple devices:
Each device needs a unique `DEVICE_ID` in its `config.h`:
```c
// Device 1
#define DEVICE_ID    "esp-001"
#define DEVICE_GROUP "production"

// Device 2
#define DEVICE_ID    "esp-002"
#define DEVICE_GROUP "production"

// Test device
#define DEVICE_ID    "esp-003"
#define DEVICE_GROUP "canary"
```

---

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| WiFi timeout | Wrong SSID/password or 5GHz network | ESP8266 only supports 2.4GHz WiFi |
| `[MQTT] Failed, rc=-2` | Can't reach broker | Check hostname (no `https://`), check internet |
| `[MQTT] Failed, rc=-4` | Auth failed | Check MQTT username/password |
| GitHub Actions: "refusing to allow PAT to create workflow" | Token missing `workflow` scope | Regenerate token with `repo` + `workflow` scopes |
| GitHub Actions: compile error | `config.h` issue | The workflow generates its own config.h for CI — check workflow file |
| ESP8266 doesn't update | Not receiving MQTT | Check Serial Monitor for `[MQTT] Connected!` |
| Download 404 | Release not created | Check repo → Releases page. Repo must be **Public**. |
| Download SSL error | Certificate issue | `setInsecure()` handles this for testing |
| Keeps rolling back | Self-test failing | Check Serial Monitor for which test says FAIL |
| `Already on v1.1.0, skipping` | Already updated | Use a new version tag, or add `"force": true` |
| Nothing in MQTT Explorer | Not connected or no messages yet | Press RST on ESP8266 to trigger a status message |

---

## File Reference

| File | Purpose | Push to GitHub? |
|------|---------|-----------------|
| `esp8266_ota_firmware.ino` | Main firmware — your code goes here | ✅ Yes |
| `config.h` | Your WiFi/MQTT credentials | ❌ NO — stays local only |
| `build-and-deploy.yml` | GitHub Actions CI/CD pipeline | ✅ Yes |
| `mqtt_notify.py` | Script that sends MQTT notification | ✅ Yes |
| `monitor_fleet.py` | Optional fleet monitoring dashboard | ✅ Yes |
| `.gitignore` | Prevents config.h from being pushed | ✅ Yes |

---

## Quick Reference: Common Commands

```bash
# First time setup
git init
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/YOUR_USER/esp8266-ota.git
git branch -M main
git push origin main --force

# Push an update to all devices
git add .
git commit -m "describe your change"
git tag v1.2.0
git push origin main --tags

# If you need to redo a tag (failed build, etc.)
git tag -d v1.2.0                         # delete local tag
git push origin :refs/tags/v1.2.0         # delete remote tag
git tag v1.2.0                            # recreate tag
git push origin main --tags --force       # push again

# Check what version tags exist
git tag -l

# Check push status
git log --oneline -5
git status
```

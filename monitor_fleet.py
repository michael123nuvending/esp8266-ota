#!/usr/bin/env python3
"""
Fleet Monitor — Watch OTA status of all ESP8266 devices in real-time.

Subscribes to MQTT status and heartbeat topics and displays a live dashboard.

Usage:
    python monitor_fleet.py

    # Or with env vars:
    MQTT_BROKER=your-broker.com MQTT_USERNAME=user MQTT_PASSWORD=pass python monitor_fleet.py

    # Or with arguments:
    python monitor_fleet.py --broker your-broker.com --username user --password pass
"""

import argparse
import json
import os
import ssl
import sys
import time
from datetime import datetime, timezone
from collections import OrderedDict

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Install paho-mqtt: pip install paho-mqtt")
    sys.exit(1)


# ---- Device Fleet State ----
devices = OrderedDict()


def format_time_ago(timestamp_ms):
    """Format milliseconds uptime into human-readable string."""
    seconds = timestamp_ms / 1000
    if seconds < 60:
        return f"{int(seconds)}s"
    elif seconds < 3600:
        return f"{int(seconds/60)}m {int(seconds%60)}s"
    elif seconds < 86400:
        return f"{int(seconds/3600)}h {int((seconds%3600)/60)}m"
    else:
        return f"{int(seconds/86400)}d {int((seconds%86400)/3600)}h"


def print_fleet_status():
    """Clear screen and print fleet status table."""
    os.system('cls' if os.name == 'nt' else 'clear')

    print("=" * 90)
    print("  ESP8266 OTA Fleet Monitor")
    print(f"  {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}  |  {len(devices)} devices online")
    print("=" * 90)
    print()
    print(f"{'Device':<12} {'Version':<10} {'Status':<18} {'Group':<12} "
          f"{'RSSI':<7} {'Heap':<8} {'Uptime':<10} {'IP':<16}")
    print("-" * 90)

    for device_id, info in devices.items():
        status = info.get("status", "unknown")
        status_icon = {
            "idle": "● idle",
            "stable": "● stable",
            "confirmed": "✓ confirmed",
            "downloading": "↓ downloading",
            "update_available": "↑ update avail",
            "self_test_running": "⧖ testing",
            "rebooting": "↻ rebooting",
            "download_failed": "✗ dl failed",
            "rolled_back": "⟲ rolled back",
            "offline": "○ offline",
        }.get(status, f"? {status}")

        uptime = format_time_ago(info.get("uptime_ms", 0))
        rssi = info.get("rssi", "?")
        heap = info.get("free_heap", 0)
        heap_str = f"{heap//1024}KB" if heap else "?"

        print(f"{device_id:<12} "
              f"{info.get('version', '?'):<10} "
              f"{status_icon:<18} "
              f"{info.get('group', '?'):<12} "
              f"{str(rssi) + 'dB':<7} "
              f"{heap_str:<8} "
              f"{uptime:<10} "
              f"{info.get('ip', '?'):<16}")

    print()
    print("-" * 90)
    print("  Press Ctrl+C to exit")


# ---- MQTT Callbacks ----

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT broker!")
        # Subscribe to all status and heartbeat topics
        client.subscribe("ota/status/#", qos=1)
        client.subscribe("ota/heartbeat/#", qos=1)
        print("Subscribed to ota/status/# and ota/heartbeat/#")
        print("Waiting for device messages...\n")
    else:
        print(f"Connection failed: {rc}")


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        device_id = payload.get("device_id", "unknown")

        # Merge incoming data into device state
        if device_id not in devices:
            devices[device_id] = {}

        devices[device_id].update(payload)
        devices[device_id]["last_seen"] = datetime.now(timezone.utc).isoformat()

        # Determine status from topic type
        if "status" in msg.topic:
            # Status messages have an explicit status field
            pass
        elif "heartbeat" in msg.topic:
            # Heartbeats indicate the device is alive
            if devices[device_id].get("status") not in [
                "downloading", "self_test_running", "rebooting"
            ]:
                state = payload.get("state", "stable")
                devices[device_id]["status"] = state

        # Refresh display
        print_fleet_status()

    except json.JSONDecodeError:
        pass
    except Exception as e:
        print(f"Error processing message: {e}")


def main():
    parser = argparse.ArgumentParser(description="Monitor ESP8266 OTA fleet")
    parser.add_argument("--broker", default=os.environ.get("MQTT_BROKER"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", "8883")))
    parser.add_argument("--username", default=os.environ.get("MQTT_USERNAME"))
    parser.add_argument("--password", default=os.environ.get("MQTT_PASSWORD"))
    parser.add_argument("--tls", default=os.environ.get("MQTT_USE_TLS", "true"))
    args = parser.parse_args()

    if not args.broker:
        print("ERROR: Provide --broker or set MQTT_BROKER env var")
        sys.exit(1)

    client = mqtt.Client(client_id=f"fleet-monitor-{int(time.time())}")
    client.on_connect = on_connect
    client.on_message = on_message

    if args.username:
        client.username_pw_set(args.username, args.password)

    if args.tls.lower() == "true":
        client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2)

    print(f"Connecting to {args.broker}:{args.port}...")
    client.connect(args.broker, args.port, keepalive=60)

    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nDisconnecting...")
        client.disconnect()


if __name__ == "__main__":
    main()

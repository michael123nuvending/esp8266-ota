#!/usr/bin/env python3
"""
MQTT OTA Notification Script

Publishes an OTA update notification to MQTT so ESP8266 devices
know a new firmware version is available.

Used by GitHub Actions after a successful build + release.

Usage:
    python mqtt_notify.py \
        --version 1.2.0 \
        --sha256 abc123... \
        --repo user/repo \
        --target fleet

Environment variables (set as GitHub Secrets):
    MQTT_BROKER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD, MQTT_USE_TLS
"""

import argparse
import json
import os
import ssl
import sys
import time
from datetime import datetime, timezone

import paho.mqtt.client as mqtt


def get_env(name: str, default: str = None) -> str:
    """Get environment variable or exit with error."""
    val = os.environ.get(name, default)
    if val is None:
        print(f"ERROR: Missing environment variable: {name}")
        sys.exit(1)
    return val


def build_firmware_url(repo: str, version: str) -> str:
    """Build the GitHub Releases download URL."""
    return f"https://github.com/{repo}/releases/download/v{version}/firmware.bin"


def main():
    parser = argparse.ArgumentParser(description="Publish OTA notification via MQTT")
    parser.add_argument("--version", required=True, help="Firmware version (e.g., 1.2.0)")
    parser.add_argument("--sha256", required=True, help="SHA256 hash of firmware.bin")
    parser.add_argument("--repo", required=True, help="GitHub repo (user/repo)")
    parser.add_argument("--target", default="fleet",
                        choices=["fleet", "canary", "staging", "production"],
                        help="Deployment target group")
    parser.add_argument("--device", default=None,
                        help="Specific device ID (overrides --target)")
    parser.add_argument("--force", action="store_true",
                        help="Force update even if device is on same version")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print payload without sending")
    args = parser.parse_args()

    # Build the OTA notification payload
    firmware_url = build_firmware_url(args.repo, args.version)

    payload = {
        "version": args.version,
        "url": firmware_url,
        "sha256": args.sha256,
        "force": args.force,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "repo": args.repo,
    }

    # Determine MQTT topic based on target
    if args.device:
        topic = f"ota/device/{args.device}"
        print(f"Target: single device → {args.device}")
    elif args.target == "fleet":
        topic = "ota/fleet/all"
        print("Target: entire fleet")
    else:
        topic = f"ota/group/{args.target}"
        print(f"Target: group → {args.target}")

    payload_json = json.dumps(payload, indent=2)

    print(f"\nTopic:   {topic}")
    print(f"Payload: {payload_json}")
    print(f"URL:     {firmware_url}")

    if args.dry_run:
        print("\n[DRY RUN] Not sending — exiting.")
        return

    # ---- Connect to MQTT and publish ----
    broker = get_env("MQTT_BROKER")
    port = int(get_env("MQTT_PORT", "8883"))
    username = get_env("MQTT_USERNAME")
    password = get_env("MQTT_PASSWORD")
    use_tls = get_env("MQTT_USE_TLS", "true").lower() == "true"

    print(f"\nConnecting to MQTT broker: {broker}:{port} (TLS: {use_tls})")

    client = mqtt.Client(client_id=f"github-actions-ota-{int(time.time())}")
    client.username_pw_set(username, password)

    if use_tls:
        client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2)

    # Connection callback
    connected = False

    def on_connect(client, userdata, flags, rc):
        nonlocal connected
        if rc == 0:
            connected = True
            print("Connected to MQTT broker!")
        else:
            print(f"MQTT connection failed with code: {rc}")

    client.on_connect = on_connect

    try:
        client.connect(broker, port, keepalive=30)
        client.loop_start()

        # Wait for connection
        timeout = 15
        for i in range(timeout):
            if connected:
                break
            time.sleep(1)
            print(f"  Waiting for connection... ({i+1}/{timeout}s)")

        if not connected:
            print("ERROR: Could not connect to MQTT broker")
            sys.exit(1)

        # Publish with QoS 1 (at least once delivery) and retain
        result = client.publish(topic, payload_json, qos=1, retain=True)
        result.wait_for_publish(timeout=10)

        if result.is_published():
            print(f"\n✓ OTA notification published successfully!")
            print(f"  Topic:   {topic}")
            print(f"  Version: {args.version}")
            print(f"  SHA256:  {args.sha256[:16]}...")
        else:
            print("ERROR: Failed to publish MQTT message")
            sys.exit(1)

    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()

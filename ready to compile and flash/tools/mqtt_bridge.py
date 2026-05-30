#!/usr/bin/env python3
"""
Simple MQTT bridge for AI Stethoscope projects.

Usage: python3 mqtt_bridge.py --broker tcp://localhost:1883 --subscribe VIAM-AI-STETH/ANALYSE --publish VIAM-AI-STETH/ANALYSE

This script forwards messages between topics (can be the same broker) and is
intended as a starter for Raspberry Pi -> Dashboard bridging or Viam bridging.
"""
import argparse
import paho.mqtt.client as mqtt
import sys


def on_connect(client, userdata, flags, rc):
    print(f"Connected to broker (rc={rc})")
    for t in userdata['subs']:
        client.subscribe(t)
        print(f"Subscribed to {t}")


def on_message(client, userdata, msg):
    payload = msg.payload
    print(f"[{msg.topic}] {len(payload)} bytes")
    # Forward the payload to all publish topics
    for pt in userdata['pubs']:
        client.publish(pt, payload)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--broker', default='localhost', help='MQTT broker hostname')
    parser.add_argument('--port', type=int, default=1883, help='MQTT broker port')
    parser.add_argument('--subscribe', action='append', required=True, help='Topic to subscribe to (repeatable)')
    parser.add_argument('--publish', action='append', required=True, help='Topic to publish to (repeatable)')
    args = parser.parse_args()

    userdata = {'subs': args.subscribe, 'pubs': args.publish}

    client = mqtt.Client(userdata=userdata)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.broker, args.port)
    except Exception as e:
        print('Failed to connect to MQTT broker:', e)
        sys.exit(2)

    client.loop_forever()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3

import paho.mqtt.client as mqtt

BROKER_HOST = "127.0.0.1"
BROKER_PORT = 1883
BROKER_USERNAME = ""
BROKER_PASSWORD = ""

SVD_COMMAND_TOPIC = "bridge/supla/shutter/command"
SVD_STATE_TOPIC = "bridge/supla/shutter/state"

ESP_COMMAND_TOPIC = "esphome/cover/shutter/command"
ESP_STATE_TOPIC = "esphome/cover/shutter/state"
ESP_POSITION_COMMAND_TOPIC = "esphome/cover/shutter/position/command"
ESP_POSITION_STATE_TOPIC = "esphome/cover/shutter/position/state"


def clamp(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def on_connect(client, userdata, flags, rc):
    if rc != 0:
        print(f"MQTT connect failed with rc={rc}", flush=True)
        return

    print("MQTT bridge connected", flush=True)
    client.subscribe(SVD_COMMAND_TOPIC)
    client.subscribe(ESP_STATE_TOPIC)
    client.subscribe(ESP_POSITION_STATE_TOPIC)
    print(
        "Subscribed to: "
        f"{SVD_COMMAND_TOPIC}, {ESP_STATE_TOPIC}, {ESP_POSITION_STATE_TOPIC}",
        flush=True,
    )


def publish_supla_state(client: mqtt.Client, shut_percent: int) -> None:
    shut_percent = clamp(shut_percent, 0, 100)
    client.publish(SVD_STATE_TOPIC, str(shut_percent), qos=1, retain=True)
    print(f"ESP -> SUPLA: shut {shut_percent}", flush=True)


def handle_supla_command(client: mqtt.Client, payload: str) -> None:
    try:
        value = int(float(payload))
    except ValueError:
        print(f"Ignoring invalid SUPLA payload: {payload!r}", flush=True)
        return

    if value == 1:
        client.publish(ESP_COMMAND_TOPIC, "CLOSE", qos=1, retain=False)
        client.publish(ESP_POSITION_COMMAND_TOPIC, "0", qos=1, retain=False)
        print("SUPLA -> ESP: CLOSE and position 0", flush=True)
    elif value == 2:
        client.publish(ESP_COMMAND_TOPIC, "OPEN", qos=1, retain=False)
        client.publish(ESP_POSITION_COMMAND_TOPIC, "100", qos=1, retain=False)
        print("SUPLA -> ESP: OPEN and position 100", flush=True)
    elif 10 <= value <= 110:
        # SUPLA uses percent of shut in range 10..110 for partial positioning.
        position = 110 - value
        client.publish(
            ESP_POSITION_COMMAND_TOPIC, str(position), qos=1, retain=False
        )
        print(f"SUPLA -> ESP: position {position}", flush=True)
    elif 0 <= value <= 100:
        # Fallback for raw 0..100 values: convert shut% to open%.
        position = 100 - value
        client.publish(
            ESP_POSITION_COMMAND_TOPIC, str(position), qos=1, retain=False
        )
        print(f"SUPLA -> ESP: raw position {position}", flush=True)
    else:
        print(f"Ignoring unsupported SUPLA payload: {payload!r}", flush=True)


def handle_esphome_state(client: mqtt.Client, payload: str) -> None:
    state = payload.strip().lower()

    if state == "closed":
        publish_supla_state(client, 100)
    elif state == "open":
        publish_supla_state(client, 0)
    elif state in ("opening", "closing"):
        print(f"ESP state update: {state}", flush=True)
    else:
        print(f"Ignoring unknown ESPHome state payload: {payload!r}", flush=True)


def handle_esphome_position_state(client: mqtt.Client, payload: str) -> None:
    try:
        position = int(float(payload))
    except ValueError:
        print(f"Ignoring invalid ESPHome position payload: {payload!r}", flush=True)
        return

    position = clamp(position, 0, 100)
    shut_percent = 100 - position

    publish_supla_state(client, shut_percent)


def on_message(client, userdata, msg):
    payload = msg.payload.decode(errors="ignore").strip()
    print(f"MQTT {msg.topic} -> {payload}", flush=True)

    if msg.topic == SVD_COMMAND_TOPIC:
        handle_supla_command(client, payload)
    elif msg.topic == ESP_STATE_TOPIC:
        handle_esphome_state(client, payload)
    elif msg.topic == ESP_POSITION_STATE_TOPIC:
        handle_esphome_position_state(client, payload)


def main() -> None:
    client = mqtt.Client(client_id="supla-cover-bridge")
    if BROKER_USERNAME:
        client.username_pw_set(BROKER_USERNAME, BROKER_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect(BROKER_HOST, BROKER_PORT, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()

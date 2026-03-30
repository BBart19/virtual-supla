# SUPLA VIRTUAL-DEVICE

This repository is a standalone fork of [`supla-dev`](https://github.com/SUPLA/supla-core/tree/master/supla-dev) and evolution of [`supla-filesensors`](https://github.com/fracz/supla-filesensors) that is able to read measurement values from files or MQTT and send them to the SUPLA, so you can display them in the app, create direct links etc. You can create buttons and perform actions such as publishing messages to MQTT or executing system commands via the SUPLA application.

Unlike the original historical layout, this repository already contains bundled and patched `supla-core` sources in `./src`. `install.sh` just restores required symlinks and builds the bundled sources.

<b>Your are using this software for your own risk. Please don't rely on it if it comes to a life danger situation.</b>


This software can be used to connect hardware with non-SUPLA firmware to SUPLA Cloud as well as supply SUPLA with data from websites and files.

If you want to use it, you can have your SUPLA account on official public `cloud.supla.org` service or private one, 
but you need some machine to run `supla-virtual-device` for you. It may be anything running linux, 
e.g. RaspberryPi or any Raspberry-like creation, VPS, your laptop etc.

# Supported sensors

* `TEMPERATURE` - sends a value from file as a temperature
* `HUMIDITY` - sends a single value as a humidity
* `TEMPERATURE_AND_HUMIDITY` - sends two values for a temperature and humidity 
* `GENERAL` - sends a single value as a general purpose measurement channel
* `ELECTRICITY_METER` - sends a 1-phase or 3-phase electricity meter value with power, voltage, current and energy
* `IC_ELECTRICITY_METER`, `IC_GAS_METER`, `IC_WATER_METER` - send an impulse-counter based meter value
* `DISTANCESENSOR`, `PRESSURESENSOR`, `RAINSENSOR`, `WEIGHTSENSOR`, `WINDSENSOR`, `DEPTHSENSOR` - send a value from file pretending to be the corresponding sensor
* `GATEWAYSENSOR`, `GATESENSOR`, `GARAGE_DOOR_SENSOR`, `NOLIQUID`, `DOORLOCKSENSOR`, `WINDOWSENSOR` - sends a 0/1 value from file to SUPLA 

# Control device supported

* `GATEWAYLOCK` `GATE` `GARAGEDOOR`, `DOORLOCK`, `POWERSWITCH`, `LIGHTSWITCH`, `PUMP_SWITCH`, `HEAT_OR_COLD_SOURCE_SWITCH`, `ROLLERSHUTTER`, `FACADEBLIND`, `HVAC_FAN` / `FAN`, `THERMOSTAT`, `THERMOSTAT_HEATPOL_HOMEPLUS`, `HVAC_THERMOSTAT`, `HVAC_THERMOSTAT_DIFFERENTIAL`, `DIMMER`, `RGBLIGHTING`, `DIMMERANDRGB`

# Installation

```
sudo apt-get update
sudo apt-get install -y git libssl-dev build-essential curl
git clone https://github.com/BBart19/virtual-supla.git
cd virtual-supla
./install.sh
```

If you use WSL, building from your Linux home directory is the safest option.
Building directly from `/mnt/c/...` may fail on symlink-heavy checkouts if your
filesystem or Git settings do not preserve symbolic links correctly.

### Upgrade

Stop `supla-virtual-device` and execute:

```
cd virtual-supla
git pull
./install.sh
```

# Configuration

There is a `supla-virtual-device.cfg` file created for you after installation.
In the `host`, `ID` and `PASSWORD` fields you should enter valid SUPLA-server
hostname, identifier of a location and its password. If you want to use it with MQTT 
you must fill MQTT section fields like `host`, `port` and if used `username` and `password` 
After successful lauch of the `supla-virtual-device` it will create a device in that location.

Then you can put as many channels in this virtual device as you wish, 
following the template.

## Adding, removing and changing channels

You can add channels to the `supla-virtual-device.cfg`. After you restart the program, they will 
be added to the device.

However, you can neither remove channels nor change their types becuase SUPLA will refuse to accept
such device with *Channels conflict* message. After such change, you need to stop `supla-virtual-device`,
remove the device from the SUPLA Cloud and then run it again. A new device with the new channels
will be registered.

This is a result of a design decision that SUPLA should never remove or change channels based on
the data received from the device, as it might result in data loss caused by e.g. some device software bug.

# Sensors with data from file
```
[CHANNEL_X]
function=TEMPERATURE
file=/home/pi/supla-virtual-device/var/raspberry_sdcard_free.txt
min_interval_sec=30
file_write_check_sec=600
```

* `CHANNEL_X` should be the next integer, starting from 0, e.g. `CHANNEL_0`, `CHANNEL_1`, ..., `CHANNEL_9`
* `function` should be set to one of the supported values mentioned above (depending on the way of presentation you need)
* `file` should point to an absolute path of the file that will contain the measurements
* `min_interval_sec` is a suggestion for the program of how often it should check for new measurements in the
  file; it is optional with a default value of `10` (seconds); if the measurement does not change often, it's
  good idea to set a bigger value not to stress your sd card with too many reads
* `file_write_check_sec` is the time when file should be written by external sensor. If is not, an error is raised and    default values sended to the Supla server

## What the file with a measurement should look like?

It should just contain the value(s) to send to the SUPLA.

So, for all channels that expect only one value (e.g. `TEMPERATURE`) it should be just number, e.g.

```
13.64
```

For the `TEMPERATURE_AND_HUMIDITY` which expects two values, put them in separate lines, e.g.:

```
13.2918
88.23
```

For the other sensors like NOLIQUID or relay it should have one line with 0/1 value

For `ROLLERSHUTTER` and `FACADEBLIND` it should have one line with position in range `0-100`,
where `0` means fully closed and `100` means fully open.

For `THERMOSTAT`, `THERMOSTAT_HEATPOL_HOMEPLUS`, `HVAC_FAN`, `HVAC_THERMOSTAT` and
`HVAC_THERMOSTAT_DIFFERENTIAL` it should have five lines:

```
0
true
21.5
20.8
0
```

These lines mean: `mode`, `power`, `preset temperature`, `measured temperature`, `fan`.
Currently only the parts needed by each bridge are used by `supla-virtual-device`.
For `HVAC_FAN` only `mode` and `power` matter, and the effective states are `off`
and `fan_only`. For `HVAC_THERMOSTAT` the file-based state is used for power and
target temperature. For `HVAC_THERMOSTAT_DIFFERENTIAL` the same single file target
is mirrored into both low/high setpoints. If you want the SUPLA HVAC screen to
show the current temperature, add a separate `TEMPERATURE` or
`TEMPERATURE_AND_HUMIDITY` channel and link it as the main thermometer of that
HVAC channel.

For `IC_ELECTRICITY_METER`, `IC_GAS_METER` and `IC_WATER_METER` the simplest file
format is a single numeric total value, for example:

```
123.456
```

For electricity that means `123.456 kWh`. You can also use JSON if you want to
provide a raw counter or optional metadata:

```json
{"counter":123456,"impulses_per_unit":1000,"unit":"kWh","price_per_unit":0.89,"currency":"PLN"}
```

For `ELECTRICITY_METER` the simplest file format is also a single total value:

```
123.456
```

For a richer 1-phase electricity meter view use JSON:

```json
{"total_kwh":123.456,"power_w":56.7,"voltage_v":229.8,"current_a":0.31,"frequency_hz":50.0}
```

That's it. Now, it's your job to fill these files with something interesting :-)


# Sensors with data from MQTT (Raw data)
```
[CHANNEL_X]
function=TEMPERATURE
state_topic=sensors/temp/kitchen/state
```
* `state_topic`: the exact topic that will be subscribed to by supla-virtual-device
   raw value means that in payload id should be raw single value like 23.5 (dot separated)
* `invert_state`: optional `0/1` flag that inverts the incoming state before it is sent to SUPLA.
  For binary channels it swaps `0` and `1`. For `ROLLERSHUTTER` and `FACADEBLIND` it converts `0-100` into `100-0`.

For `HUMIDITY` use the same format with a single humidity value:

```
[CHANNEL_X]
function=HUMIDITY
state_topic=sensors/humidity/kitchen/state
```

For `GENERAL` use the same format with a single numeric value. `GENERAL` is a
short alias for the official SUPLA function name `GENERAL_PURPOSE_MEASUREMENT`.
You can also define how SUPLA should display it:

```
[CHANNEL_X]
function=GENERAL
state_topic=sensors/air_quality/pm25/state
value_precision=1
unit_after_value=ug/m3
keep_history=1
chart_type=linear
```

`value_divider`, `value_multiplier` and `value_added` are optional and use
human-readable values from the config file. For example `value_multiplier=0.1`
will be stored in the SUPLA runtime config as `100`.

For `IC_ELECTRICITY_METER`, `IC_GAS_METER` and `IC_WATER_METER` you can use
either a plain numeric payload with the total value or a JSON payload. The
plain numeric payload is interpreted as a total value in the channel unit, for
example:

```
[CHANNEL_X]
function=IC_ELECTRICITY_METER
state_topic=meters/energy/main/state
```

with payload:

```
123.456
```

You can also publish JSON:

```json
{"counter":123456,"impulses_per_unit":1000,"unit":"kWh","price_per_unit":0.89,"currency":"PLN"}
```

When you already have a nested JSON total, `payload_value` can point to it, for
example `/total_kwh`.

For `ELECTRICITY_METER` you can use the same simple numeric payload with total
energy or a JSON payload. The 1-phase JSON format supported by
`supla-virtual-device` is:

```json
{"total_kwh":123.456,"power_w":56.7,"voltage_v":229.8,"current_a":0.31,"frequency_hz":50.0}
```

Optional fields are:
* `total_reverse_active_energy_kwh`
* `total_forward_reactive_energy_kvarh`
* `total_reverse_reactive_energy_kvarh`
* `power_reactive_var`
* `power_apparent_va`
* `power_factor`
* `phase_angle_deg`
* `period_sec`

You can also split the measurements into separate MQTT topics:

```ini
[CHANNEL_X]
function=ELECTRICITY_METER
energy_topic=meters/energy/phase1/total_kwh/state
power_topic=meters/energy/phase1/power_w/state
voltage_topic=meters/energy/phase1/voltage_v/state
current_topic=meters/energy/phase1/current_a/state
frequency_topic=meters/energy/phase1/frequency_hz/state
```

Each of these topics should publish a single numeric value. The old single
`state_topic` JSON format still works too.

For ESPHome integrations make sure you use the exact published sensor topic.
For most ESPHome sensors this means the topic ends with `/state`, for example
`gniazdko-1-ig007-bk7231n/sensor/bl0942_energy/state`.

For a 3-phase meter you can use:

```ini
[CHANNEL_X]
function=ELECTRICITY_METER
energy_topic_l1=meters/energy/l1/total_kwh/state
energy_topic_l2=meters/energy/l2/total_kwh/state
energy_topic_l3=meters/energy/l3/total_kwh/state
power_topic_l1=meters/energy/l1/power_w/state
power_topic_l2=meters/energy/l2/power_w/state
power_topic_l3=meters/energy/l3/power_w/state
voltage_topic_l1=meters/energy/l1/voltage_v/state
voltage_topic_l2=meters/energy/l2/voltage_v/state
voltage_topic_l3=meters/energy/l3/voltage_v/state
current_topic_l1=meters/energy/l1/current_a/state
current_topic_l2=meters/energy/l2/current_a/state
current_topic_l3=meters/energy/l3/current_a/state
frequency_topic=meters/energy/frequency_hz/state
```

The old names without suffix, like `power_topic` or `energy_topic`, still map
to phase 1.

Additional optional per-phase topics supported for 3-phase meters are:
* `reactive_power_topic_l1`, `reactive_power_topic_l2`, `reactive_power_topic_l3`
* `apparent_power_topic_l1`, `apparent_power_topic_l2`, `apparent_power_topic_l3`
* `power_factor_topic_l1`, `power_factor_topic_l2`, `power_factor_topic_l3`
* `phase_angle_topic_l1`, `phase_angle_topic_l2`, `phase_angle_topic_l3`
* `returned_energy_topic_l1`, `returned_energy_topic_l2`, `returned_energy_topic_l3`
* `inductive_energy_topic_l1`, `inductive_energy_topic_l2`, `inductive_energy_topic_l3`
* `capacitive_energy_topic_l1`, `capacitive_energy_topic_l2`, `capacitive_energy_topic_l3`

Aliases are also accepted:
* `power_reactive_topic_lX` for `reactive_power_topic_lX`
* `power_apparent_topic_lX` for `apparent_power_topic_lX`
* `reverse_active_energy_topic_lX` for `returned_energy_topic_lX`
* `forward_reactive_energy_topic_lX` for `inductive_energy_topic_lX`
* `reverse_reactive_energy_topic_lX` for `capacitive_energy_topic_lX`

If a given measurement topic is not configured, `supla-virtual-device` does not
set the corresponding availability flag for that measurement type. In practice
that means the app should only show sections for the measurements you actually
provide.

For `DIMMER`, `RGBLIGHTING` and `DIMMERANDRGB` you can read state from MQTT.
The simplest supported layout is:

```
[CHANNEL_X]
function=DIMMER
state_topic=lights/kitchen/state
brightness_topic=lights/kitchen/brightness/state
command_topic=lights/kitchen/command
command_template={"state":"$state$","brightness":$brightness$}
payload_on=ON
payload_off=OFF
```

For RGB use optional `color_topic` and `color_brightness_topic`:

```
[CHANNEL_X]
function=DIMMERANDRGB
state_topic=lights/desk/state
brightness_topic=lights/desk/brightness/state
color_brightness_topic=lights/desk/color_brightness/state
color_topic=lights/desk/color/state
command_topic=lights/desk/command
command_template={"state":"$state$","brightness":$brightness$,"color_brightness":$color_brightness$,"color":"$hex_color$","r":$red$,"g":$green$,"b":$blue$}
payload_on=ON
payload_off=OFF
```

Supported raw MQTT/file formats:

* `DIMMER`: single brightness value `0-100`
* `RGBLIGHTING`: `#RRGGBB`, `0xRRGGBB`, `R G B`, optionally followed by color brightness `0-100`
* `DIMMERANDRGB`: `brightness color_brightness R G B` and optional trailing `on_off`

For RGB command templates these placeholders are available:

* `$state$` / `$value$` - state payload, for example `ON` / `OFF`
* `$on_off$` - numeric `1` or `0`
* `$brightness$`
* `$color_brightness$`
* `$red$`, `$green$`, `$blue$`
* `$color$` - `0xRRGGBB`
* `$hex_color$` - `#RRGGBB`

For `TEMPERATURE_AND_HUMIDITY` you can also use two separate MQTT topics:

```
[CHANNEL_X]
function=TEMPERATURE_AND_HUMIDITY
temperature_topic=pendrive-3/sensor/sht40_temperatura/state
humidity_topic=pendrive-3/sensor/sht40_wilgotnosc/state
```

When `temperature_topic` and `humidity_topic` are used, `supla-virtual-device`
updates the combined temperature and humidity channel whenever either topic changes.
The old single `state_topic` format with both values in one payload still works too.

# Sensors with data from MQTT (Json)
```
[CHANNEL_X]
function=TEMPERATURE
state_topic=sensors/temp/kitchen/state
payload_value=/data/temp
```

* `payload_value`: [`JSONPointer`](https://tools.ietf.org/html/rfc6901) to the value in JSON payload 
   example above assumes that payload will look like {"data": { "temp": 23.5 } }

# Sensors with multiple data on one topic (Json)
[CHANNEL_X]
function=TEMPERATURE
state_topic=sensors/temp/kitchen/state
payload_value=/data/temp
id_template=/data/id
id_value=sensor_1
```

* `payload_value`: [`JSONPointer`](https://tools.ietf.org/html/rfc6901) to the value in JSON payload 
   example above assumes that payload will look like {"data": { "temp": 23.5 } }
* `id_template`:  [`JSONPointer`](https://tools.ietf.org/html/rfc6901) to the value in JSON payload 
   where sensor identifier is present
* `id_value`: sensor's identifier

When id_template and id_value are used SVD tries to get identifier speciefied by id_template from message payload and compares it to provided id_value. If those are not the same program does not perform action on channel

# Executing system command
```
[CHANNEL_X]
function=GATEWAYLOCK
command=echo 'Hello World' >> helloworld.txt
```
* `command`: system command that will be executed on switch value changed
   example above is working with monostable button 
```
[CHANNEL_X]
function=POWERSWITCH
command_on=echo 'Power On!' >> power.txt
command_off=echo 'Power Off!' >> power.txt
```
* `command_on`: system command that will be executed when channel value change to 1
* `command_off`: system command that will be executed when channel value change to 0

# Publishing MQTT device command
```
[CHANNEL_X]
function=POWERSWITCH
state_topic=switch/kitchen/state
payload_on=1
payload_off=0
command_topic=switch/kitchen/command
command_payload_on=1
command_payload_off=0
```
* `command_topic`: MQTT publish topic
* `payload_on`: value template that means channel on value
* `payload_off`: value template that means channel off value
* `command_payload_on`: MQTT payload sent when SUPLA turns the channel on
* `command_payload_off`: MQTT payload sent when SUPLA turns the channel off
* `command_template`: optional generic MQTT template, still available for more advanced payloads

`command_payload_on` and `command_payload_off` are aliases for
`command_template_on` and `command_template_off`. They are especially useful for
binary channels when the reported state values differ from the command values.
They do not affect how incoming MQTT state is parsed, only what `supla-virtual-device`
publishes to `command_topic`.

Recommended usage:
* use `command_payload_on` and `command_payload_off` for binary channels when you just need separate `on` and `off` payloads
* use `command_template` when you want a single shared template, for example JSON with `$value$`
* use `command_template_on` and `command_template_off` when the per-state payloads need a more descriptive template-style name, for example HVAC channels

Publish fallback order for binary channels:
1. `command_payload_on` / `command_payload_off` or `command_template_on` / `command_template_off`
2. `command_template`

Examples:
```
# state uses ON/OFF, command uses START/STOP
payload_on=ON
payload_off=OFF
command_payload_on=START
command_payload_off=STOP
```

```
# one shared JSON template
command_template={"state":"$value$"}
payload_on=ON
payload_off=OFF
```

`PUMP_SWITCH` / `PUMPSWITCH` and `HEAT_OR_COLD_SOURCE_SWITCH` /
`HEATORCOLDSOURCESWITCH` use the same MQTT format as `POWERSWITCH`. They are
reported to SUPLA as their dedicated functions, but from the MQTT bridge point
of view they behave like normal binary switches. These two functions require
SUPLA protocol version `25`, so `supla-virtual-device` will request protocol 25
automatically when they are configured.

Important limitation: in the reference `SuplaDevice` implementation these two
functions are treated as HVAC-related helper relays, not as normal standalone
user-controlled switches. That means the official SUPLA clients may show their
state but still refuse or skip manual switching. If you need guaranteed manual
control from SUPLA, use `POWERSWITCH` instead.

`GATE`, `GATEWAYLOCK`, `GARAGEDOOR` and `DOORLOCK` also support
`command_payload_on` / `command_payload_off` now. If you do not set them, they
keep the old `command_template` behavior.

Example pump switch:
```
[CHANNEL_X]
function=PUMP_SWITCH
state_topic=switch/pump/state
payload_on=ON
payload_off=OFF
command_topic=switch/pump/command
command_payload_on=START
command_payload_off=STOP
```

Example heat/cold source switch:
```
[CHANNEL_X]
function=HEAT_OR_COLD_SOURCE_SWITCH
state_topic=switch/heat_or_cold_source/state
payload_on=ON
payload_off=OFF
command_topic=switch/heat_or_cold_source/command
command_payload_on=HEAT
command_payload_off=COOL
```

# Publishing MQTT roller shutter command
```
[CHANNEL_X]
function=ROLLERSHUTTER
state_topic=rollershutter/livingroom/state
invert_state=1
command_topic=rollershutter/livingroom/command
command_template=$value$
```
* `state_topic`: should provide the current shutter position in range `0-100`
* `invert_state`: use `1` when your MQTT source reports open percentage and you want SUPLA to display closed percentage
* `command_template`: MQTT payload `$value$` will be replaced with requested shutter position in range `0-100`

`FACADEBLIND` uses the same MQTT and file format as `ROLLERSHUTTER`, for example:
```
[CHANNEL_X]
function=FACADEBLIND
state_topic=facadeblind/livingroom/state
invert_state=1
command_topic=facadeblind/livingroom/command
command_template=$value$
```
This first implementation gives the dedicated SUPLA facade blind function and UI
category, but it currently uses the same position-only behavior as `ROLLERSHUTTER`
and does not implement separate slat tilt control yet.

# Publishing MQTT HVAC fan command
```
[CHANNEL_X]
function=HVAC_FAN
state_topic=esphome/device/climate/fan/mode/state
payload_on=fan_only
payload_off=off
command_topic=esphome/device/climate/fan/mode/command
command_template_on=fan_only
command_template_off=off
```
* `function=HVAC_FAN`: enables the official SUPLA HVAC fan function. `FAN` is kept as a config alias.
* `state_topic`: current HVAC fan mode, usually `fan_only` or `off`
* `command_topic`: MQTT topic used for SUPLA fan on/off commands
* `command_template_on`, `command_template_off`: payloads published for turning the fan on and off

`HVAC_FAN` is currently only partially usable because the official SUPLA ecosystem
does not seem to support it fully yet. In practice:
* the public SUPLA Android app still treats `HVAC_FAN` as unsupported
* the SUPLA web UI may reject actions such as `TURN_ON`, `TURN_OFF`,
  `HVAC_SWITCH_TO_PROGRAM_MODE` and `HVAC_SWITCH_TO_MANUAL_MODE`
* the SUPLA web UI may also omit the main thermometer picker even though the
  protocol uses the standard HVAC config structure

So `HVAC_FAN` should currently be treated as experimental in `supla-virtual-device`.
The MQTT bridge is implemented, but final behavior still depends on upstream SUPLA
web/mobile support.

# Publishing MQTT roller shutter command to ESPHome cover
```
[CHANNEL_X]
function=ROLLERSHUTTER
state_topic=esp32c3-bl0939/cover/zaluzja/position/state
invert_state=1
retain=0
esphome_cover=1
command_topic=esp32c3-bl0939/cover/zaluzja/command
position_command_topic=esp32c3-bl0939/cover/zaluzja/position/command
```
* `state_topic`: ESPHome `position/state` topic with open percentage `0-100`
* `invert_state=1`: converts ESPHome open percentage into the closed percentage expected by SUPLA
* `esphome_cover=1`: enables native conversion of SUPLA shutter commands into ESPHome MQTT cover commands
* `command_topic`: receives `OPEN`, `CLOSE` and `STOP`
* `position_command_topic`: receives target open percentage for swipe gestures

# Publishing MQTT thermostat command
```
[CHANNEL_X]
function=THERMOSTAT_HEATPOL_HOMEPLUS
state_topic=esphome/device/climate/room/mode/state
payload_on=heat
payload_off=off
command_topic=esphome/device/climate/room/mode/command
command_template_on=heat
command_template_off=off
measured_temperature_topic=esphome/device/climate/room/current_temperature/state
preset_temperature_topic=esphome/device/climate/room/target_temperature/state
preset_temperature_command_topic=esphome/device/climate/room/target_temperature/command
preset_temperature_command_template=$value$
```
* `function=THERMOSTAT_HEATPOL_HOMEPLUS`: enables the thermostat UI currently supported by the SUPLA app
* `state_topic`: current thermostat power/mode topic interpreted with `payload_on` and `payload_off`
* `command_topic`: MQTT topic used for thermostat `on/off` commands from SUPLA
* `command_template_on`, `command_template_off`: payloads published for turning the thermostat on and off
* `measured_temperature_topic`: raw current temperature topic
* `preset_temperature_topic`: raw target temperature topic
* `preset_temperature_command_topic`: topic that receives target temperature set from the SUPLA app
* `preset_temperature_command_template`: optional payload template for target temperature commands. Supported placeholders: `$value$`, `$temperature$`, `$index$`

Current MQTT thermostat support focuses on the main controls that are most useful with external systems:
`on/off`, measured temperature and target temperature. Advanced Heatpol-specific modes and schedule commands are not translated to MQTT yet.

# Publishing MQTT HVAC thermostat command
```
[CHANNEL_X]
function=HVAC_THERMOSTAT
hvac_subfunction=heat
main_thermometer_channel_no=Y
state_topic=esphome/device/climate/room/mode/state
payload_on=heat
payload_off=off
command_topic=esphome/device/climate/room/mode/command
command_template_on=heat
command_template_off=off
preset_temperature_topic=esphome/device/climate/room/target_temperature/state
preset_temperature_command_topic=esphome/device/climate/room/target_temperature/command
preset_temperature_command_template=$value$
```
* `function=HVAC_THERMOSTAT`: enables the normal SUPLA thermostat UI
* `hvac_subfunction`: `heat` or `cool`; it tells SUPLA whether this is a heating or cooling thermostat
* `main_thermometer_channel_no`: optional number of a local `TEMPERATURE` or `TEMPERATURE_AND_HUMIDITY` channel used as the HVAC main thermometer
* `state_topic`: current HVAC mode topic interpreted with `payload_on` and `payload_off`
* `command_topic`: MQTT topic used for thermostat `on/off` commands from SUPLA
* `command_template_on`, `command_template_off`: payloads published for turning the thermostat on and off
* `preset_temperature_topic`: raw target temperature topic
* `preset_temperature_command_topic`: topic that receives target temperature set from the SUPLA app
* `preset_temperature_command_template`: optional payload template for target temperature commands. Supported placeholders: `$value$`, `$temperature$`, `$index$`

If you also want the current temperature to be visible inside the normal SUPLA thermostat screen,
add a separate `TEMPERATURE` or `TEMPERATURE_AND_HUMIDITY` channel for the same MQTT source and
set its channel number in `main_thermometer_channel_no`. If you omit it, `supla-virtual-device`
will try to auto-detect the first local temperature channel.

Program mode for `HVAC_THERMOSTAT` is handled locally by `supla-virtual-device` using the
weekly schedule received from SUPLA. When the program changes, the virtual device publishes the
resulting MQTT `command_topic` and `preset_temperature_command_topic` values automatically.

# Publishing MQTT HVAC differential thermostat command
```
[CHANNEL_X]
function=HVAC_THERMOSTAT_DIFFERENTIAL
main_thermometer_channel_no=Y
# report_as_hvac_thermostat=1
state_topic=esphome/device/climate/differential/mode/state
payload_on=heat_cool
payload_off=off
command_topic=esphome/device/climate/differential/mode/command
command_template_on=heat_cool
command_template_off=off
target_temperature_low_topic=esphome/device/climate/differential/target_temperature_low/state
target_temperature_low_command_topic=esphome/device/climate/differential/target_temperature_low/command
target_temperature_low_command_template=$value$
target_temperature_high_topic=esphome/device/climate/differential/target_temperature_high/state
target_temperature_high_command_topic=esphome/device/climate/differential/target_temperature_high/command
target_temperature_high_command_template=$value$
current_temperature_topic=esphome/device/climate/differential/current_temperature/state
action_topic=esphome/device/climate/differential/action/state
```
* `function=HVAC_THERMOSTAT_DIFFERENTIAL`: enables the ESPHome `bang_bang` bridge with low/high setpoints and the differential thermostat view in SUPLA web.
* `main_thermometer_channel_no`: optional number of a local `TEMPERATURE` or `TEMPERATURE_AND_HUMIDITY` channel used as the current temperature source in SUPLA. You can also choose it from the SUPLA web config.
* `report_as_hvac_thermostat=1`: optional compatibility mode for SUPLA mobile. It reports the channel as a regular `HVAC_THERMOSTAT`, so the phone stops showing "unsupported", but the web view will also change from differential `min/max` to the regular thermostat screen.
* `state_topic` / `command_topic`: MQTT mode bridge, typically ESPHome `mode/state` and `mode/command`
* `target_temperature_low_*`: low setpoint topics used by ESPHome `bang_bang`
* `target_temperature_high_*`: high setpoint topics used by ESPHome `bang_bang`
* `current_temperature_topic`: optional ESPHome current temperature topic. When set, `supla-virtual-device` updates the selected SUPLA thermometer channel from this MQTT value.
* `action_topic`: optional ESPHome action topic; when present it updates SUPLA heating/cooling state flags

This bridge intentionally keeps the control logic in ESPHome. `supla-virtual-device`
only mirrors the SUPLA interface to MQTT topics such as `mode`,
`target_temperature_low`, `target_temperature_high` and optional `action`.

`HVAC_THERMOSTAT_DIFFERENTIAL` is also only partially supported by the official
SUPLA clients:
* the SUPLA web UI can show the differential thermostat view with `min/max`
* the public SUPLA mobile app still treats the differential function as unsupported
* `report_as_hvac_thermostat=1` is a compatibility workaround for mobile, but then
  the channel is reported as a regular `HVAC_THERMOSTAT` and the web view also
  changes from differential `min/max` to the normal thermostat screen

So this function is best treated as web-first unless the channel is explicitly
reported as a normal HVAC thermostat for mobile compatibility.

# Autostarting

Autostart configuration is just the same as the [`supla-dev`](https://github.com/SUPLA/supla-core/tree/master/supla-dev#supervisor) instructions.
It's good idea to configure it so `supla-virtual-device` starts automatically after your machine boots.
Execute the steps from the instructions there, but provide process configuration for the `supla-virtual-device`:

```
[program:supla-virtual-device]
command=/home/pi/supla-virtual-device/supla-virtual-device
directory=/home/pi/supla-virtual-device
autostart=true
autorestart=true
user=pi
```

### Start, stop, restart

Just like for the [`supla-dev`](https://github.com/SUPLA/supla-core/tree/master/supla-dev#managing-the-process-with-supervisor)

```
supervisorctl status
supervisorctl stop supla-virtual-device
supervisorctl start supla-virtual-device
supervisorctl restart supla-virtual-device
supervisorctl tail supla-virtual-device
```

# Where are the sources?

If you want to see the sources of this project, check out the 
[`supla-mqtt-dev` branch on mine `supla-core`'s fork](https://github.com/lukbek/supla-core/tree/supla-mqtt-dev).

# Support 

Feel free to ask on [`SUPLA's forum`](https://forum.supla.org/viewtopic.php?f=9&t=6189) for this software and report issues on github.

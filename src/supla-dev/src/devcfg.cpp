/*
 Copyright (C) AC SOFTWARE SP. Z O.O.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "devcfg.h"

#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channel-io.h"
#include "mcp23008.h"
#include "supla-client-lib/log.h"
#include "supla-client-lib/tools.h"

char DEVICE_GUID[SUPLA_GUID_SIZE];
char DEVICE_AUTHKEY[SUPLA_AUTHKEY_SIZE];
char SOFTWARE_VERSION[SUPLA_SOFTVER_MAXSIZE] = "1.1.2";

/**
 * Use type names to process supla configuration file
 */
static int decode_function_type(const char *fnc) {
  if (strcasecmp(fnc, "TEMPERATURE") == 0) {
    return SUPLA_CHANNELFNC_THERMOMETER;
  } else if (strcasecmp(fnc, "GATEWAYLOCK") == 0) {
    return SUPLA_CHANNELFNC_CONTROLLINGTHEGATEWAYLOCK;
  } else if (strcasecmp(fnc, "GATE") == 0) {
    return SUPLA_CHANNELFNC_CONTROLLINGTHEGATE;
  } else if (strcasecmp(fnc, "GARAGEDOOR") == 0) {
    return SUPLA_CHANNELFNC_CONTROLLINGTHEGARAGEDOOR;
  } else if (strcasecmp(fnc, "HUMIDITY") == 0) {
    return SUPLA_CHANNELFNC_HUMIDITY;
  } else if (strcasecmp(fnc, "TEMPERATURE_AND_HUMIDITY") == 0) {
    return SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE;
  } else if (strcasecmp(fnc, "GATEWAYSENSOR") == 0) {
    return SUPLA_CHANNELFNC_OPENINGSENSOR_GATEWAY;
  } else if (strcasecmp(fnc, "GATESENSOR") == 0) {
    return SUPLA_CHANNELFNC_OPENINGSENSOR_GATE;
  } else if (strcasecmp(fnc, "GARAGE_DOOR_SENSOR") == 0) {
    return SUPLA_CHANNELFNC_OPENINGSENSOR_GARAGEDOOR;
  } else if (strcasecmp(fnc, "NOLIQUID") == 0) {
    return SUPLA_CHANNELFNC_NOLIQUIDSENSOR;
  } else if (strcasecmp(fnc, "DOORLOCK") == 0) {
    return SUPLA_CHANNELFNC_CONTROLLINGTHEDOORLOCK;
  } else if (strcasecmp(fnc, "DOORLOCKSENSOR") == 0) {
    return SUPLA_CHANNELFNC_OPENINGSENSOR_DOOR;
  } else if (strcasecmp(fnc, "ROLLERSHUTTER") == 0) {
    return SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER;
  } else if (strcasecmp(fnc, "FACADEBLIND") == 0 ||
             strcasecmp(fnc, "CONTROLLINGTHEFACADEBLIND") == 0) {
    return SUPLA_CHANNELFNC_CONTROLLINGTHEFACADEBLIND;
  } else if (strcasecmp(fnc, "ROLLERSHUTTERSENSOR") == 0) {
    return SUPLA_CHANNELFNC_OPENINGSENSOR_ROLLERSHUTTER;
  } else if (strcasecmp(fnc, "POWERSWITCH") == 0) {
    return SUPLA_CHANNELFNC_POWERSWITCH;
  } else if (strcasecmp(fnc, "LIGHTSWITCH") == 0) {
    return SUPLA_CHANNELFNC_LIGHTSWITCH;
  } else if (strcasecmp(fnc, "DIMMER") == 0) {
    return SUPLA_CHANNELFNC_DIMMER;
  } else if (strcasecmp(fnc, "RGBLIGHTING") == 0) {
    return SUPLA_CHANNELFNC_RGBLIGHTING;
  } else if (strcasecmp(fnc, "DIMMERANDRGB") == 0) {
    return SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING;
  } else if (strcasecmp(fnc, "DEPTHSENSOR") == 0) {
    return SUPLA_CHANNELFNC_DEPTHSENSOR;
  } else if (strcasecmp(fnc, "DISTANCESENSOR") == 0) {
    return SUPLA_CHANNELFNC_DISTANCESENSOR;
  } else if (strcasecmp(fnc, "WINDOWSENSOR") == 0) {
    return SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW;
  } else if (strcasecmp(fnc, "PRESSURESENSOR") == 0) {
    return SUPLA_CHANNELFNC_PRESSURESENSOR;
  } else if (strcasecmp(fnc, "RAINSENSOR") == 0) {
    return SUPLA_CHANNELFNC_RAINSENSOR;
  } else if (strcasecmp(fnc, "WEIGHTSENSOR") == 0) {
    return SUPLA_CHANNELFNC_WEIGHTSENSOR;
  } else if (strcasecmp(fnc, "STAIRCASETIMER") == 0) {
    return SUPLA_CHANNELFNC_STAIRCASETIMER;
  } else if (strcasecmp(fnc, "PUMP_SWITCH") == 0 ||
             strcasecmp(fnc, "PUMPSWITCH") == 0) {
    return SUPLA_CHANNELFNC_PUMPSWITCH;
  } else if (strcasecmp(fnc, "HEAT_OR_COLD_SOURCE_SWITCH") == 0 ||
             strcasecmp(fnc, "HEATORCOLDSOURCESWITCH") == 0) {
    return SUPLA_CHANNELFNC_HEATORCOLDSOURCESWITCH;
  } else if (strcasecmp(fnc, "IC_ELECTRICITY_METER") == 0) {
    return SUPLA_CHANNELFNC_IC_ELECTRICITY_METER;
  } else if (strcasecmp(fnc, "ELECTRICITY_METER") == 0) {
    return SUPLA_CHANNELFNC_ELECTRICITY_METER;
  } else if (strcasecmp(fnc, "IC_GAS_METER") == 0) {
    return SUPLA_CHANNELFNC_IC_GAS_METER;
  } else if (strcasecmp(fnc, "IC_WATER_METER") == 0) {
    return SUPLA_CHANNELFNC_IC_WATER_METER;
  } else if (strcasecmp(fnc, "GENERAL") == 0 ||
             strcasecmp(fnc, "GENERAL_PURPOSE_MEASUREMENT") == 0) {
    return SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT;
  } else if (strcasecmp(fnc, "WINDSENSOR") == 0) {
    return SUPLA_CHANNELFNC_WINDSENSOR;
  } else if (strcasecmp(fnc, "MAILSENSOR") == 0) {
    return SUPLA_CHANNELFNC_MAILSENSOR;
  } else if (strcasecmp(fnc, "FAN") == 0 ||
             strcasecmp(fnc, "HVAC_FAN") == 0) {
    return SUPLA_CHANNELFNC_HVAC_FAN;
  } else if (strcasecmp(fnc, "THERMOSTAT") == 0) {
    return SUPLA_CHANNELFNC_THERMOSTAT;
  } else if (strcasecmp(fnc, "HVAC_THERMOSTAT") == 0) {
    return SUPLA_CHANNELFNC_HVAC_THERMOSTAT;
  } else if (strcasecmp(fnc, "HVAC_THERMOSTAT_DIFFERENTIAL") == 0 ||
             strcasecmp(fnc, "THERMOSTAT_DIFFERENTIAL") == 0 ||
             strcasecmp(fnc, "HVAC_BANG_BANG") == 0 ||
             strcasecmp(fnc, "BANG_BANG") == 0) {
    return SUPLA_CHANNELFNC_HVAC_THERMOSTAT_DIFFERENTIAL;
  } else if (strcasecmp(fnc, "THERMOSTAT_HEATPOL_HOMEPLUS") == 0 ||
             strcasecmp(fnc, "THERMOSTAT_HEATPOL") == 0 ||
             strcasecmp(fnc, "THERMOSTAT_HEATPOOL") == 0) {
    return SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS;
  } else
    return SUPLA_CHANNELFNC_NONE;
}

static int decode_channel_type(const char *type) {
  if (strcasecmp(type, "SENSORNO") == 0) {
    return SUPLA_CHANNELTYPE_SENSORNO;
  } else if (strcasecmp(type, "SENSORNC") == 0) {
    return SUPLA_CHANNELTYPE_SENSORNC;
  } else if (strcasecmp(type, "RELAYHFD4") == 0) {
    return SUPLA_CHANNELTYPE_RELAYHFD4;
  } else if (strcasecmp(type, "RELAYG5LA1A") == 0) {
    return SUPLA_CHANNELTYPE_RELAYG5LA1A;
  } else if (strcasecmp(type, "2XRELAYG5LA1A") == 0) {
    return SUPLA_CHANNELTYPE_2XRELAYG5LA1A;
  } else if (strcasecmp(type, "RELAY") == 0) {
    return SUPLA_CHANNELTYPE_RELAY;
  } else if (strcasecmp(type, "THERMOMETERDS18B20") == 0) {
    return SUPLA_CHANNELTYPE_THERMOMETERDS18B20;
  } else if (strcasecmp(type, "DHT11") == 0) {
    return SUPLA_CHANNELTYPE_DHT11;
  } else if (strcasecmp(type, "DHT22") == 0) {
    return SUPLA_CHANNELTYPE_DHT22;
  } else if (strcasecmp(type, "AM2302") == 0) {
    return SUPLA_CHANNELTYPE_AM2302;
  } else if (strcasecmp(type, "DIMMER") == 0) {
    return SUPLA_CHANNELTYPE_DIMMER;
  } else if (strcasecmp(type, "RGBLEDCONTROLLER") == 0) {
    return SUPLA_CHANNELTYPE_RGBLEDCONTROLLER;
  } else if (strcasecmp(type, "DIMMERANDRGBLED") == 0) {
    return SUPLA_CHANNELTYPE_DIMMERANDRGBLED;
  } else if (strcasecmp(type, "HUMIDITYSENSOR") == 0) {
    return SUPLA_CHANNELTYPE_HUMIDITYSENSOR;
  } else if (strcasecmp(type, "THERMOSTAT") == 0) {
    return SUPLA_CHANNELTYPE_THERMOSTAT;
  } else if (strcasecmp(type, "HVAC") == 0) {
    return SUPLA_CHANNELTYPE_HVAC;
  } else if (strcasecmp(type, "ENGINE") == 0) {
    return SUPLA_CHANNELTYPE_ENGINE;
  } else if (strcasecmp(type, "THERMOSTAT_HEATPOL_HOMEPLUS") == 0 ||
             strcasecmp(type, "THERMOSTAT_HEATPOL") == 0 ||
             strcasecmp(type, "THERMOSTAT_HEATPOOL") == 0) {
    return SUPLA_CHANNELTYPE_THERMOSTAT_HEATPOL_HOMEPLUS;
  }

  return atoi(type);
}

static int decode_general_chart_type(const char *value) {
  if (strcasecmp(value, "linear") == 0) {
    return SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_LINEAR;
  } else if (strcasecmp(value, "bar") == 0) {
    return SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_BAR;
  } else if (strcasecmp(value, "candle") == 0) {
    return SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_CANDLE;
  }

  return atoi(value);
}

static _supla_int_t parse_general_scaled_value(const char *value) {
  double parsed = strtod(value, NULL) * 1000.0;

  if (parsed > std::numeric_limits<_supla_int_t>::max()) {
    parsed = std::numeric_limits<_supla_int_t>::max();
  } else if (parsed < std::numeric_limits<_supla_int_t>::min()) {
    parsed = std::numeric_limits<_supla_int_t>::min();
  }

  return static_cast<_supla_int_t>(parsed >= 0 ? parsed + 0.5 : parsed - 0.5);
}

static _supla_int64_t parse_general_scaled_value64(const char *value) {
  double parsed = strtod(value, NULL) * 1000.0;

  if (parsed > static_cast<double>(std::numeric_limits<_supla_int64_t>::max())) {
    parsed = static_cast<double>(std::numeric_limits<_supla_int64_t>::max());
  } else if (parsed <
             static_cast<double>(std::numeric_limits<_supla_int64_t>::min())) {
    parsed = static_cast<double>(std::numeric_limits<_supla_int64_t>::min());
  }

  return static_cast<_supla_int64_t>(parsed >= 0 ? parsed + 0.5
                                                 : parsed - 0.5);
}

static _supla_int16_t parse_hvac_temperature_raw(const char *value) {
  double parsed = strtod(value, NULL) * 100.0;

  if (parsed > std::numeric_limits<_supla_int16_t>::max()) {
    parsed = std::numeric_limits<_supla_int16_t>::max();
  } else if (parsed < std::numeric_limits<_supla_int16_t>::min()) {
    parsed = std::numeric_limits<_supla_int16_t>::min();
  }

  return static_cast<_supla_int16_t>(parsed >= 0 ? parsed + 0.5
                                                 : parsed - 0.5);
}

static int decode_electricity_measurement_phase(const char *name,
                                                const char *measurement) {
  if (name == NULL || measurement == NULL) return -1;

  const char *patterns[] = {"%s_topic", "state_%s_topic", "state_%s"};
  char buffer[64];
  char phasedBuffer[70];

  for (int phase = 0; phase < 3; phase++) {
    for (size_t idx = 0; idx < sizeof(patterns) / sizeof(patterns[0]); idx++) {
      snprintf(buffer, sizeof(buffer), patterns[idx], measurement);

      if (phase == 0 && strcasecmp(name, buffer) == 0) {
        return 0;
      }

      snprintf(phasedBuffer, sizeof(phasedBuffer), "%s_l%d", buffer, phase + 1);
      if (strcasecmp(name, phasedBuffer) == 0) {
        return phase;
      }
    }
  }

  return -1;
}

static int decode_electricity_measurement_phase_aliases(
    const char *name, const char **aliases, size_t aliasCount) {
  if (name == NULL || aliases == NULL) return -1;

  for (size_t idx = 0; idx < aliasCount; idx++) {
    int phase = decode_electricity_measurement_phase(name, aliases[idx]);
    if (phase >= 0) return phase;
  }

  return -1;
}
/*
static int decode_channel_driver(const char *type) {
  if (strcasecmp(type, "mcp23008") == 0) {
    return SUPLA_CHANNELDRIVER_MCP23008;
  }
  return 0;
}*/

void devcfg_channel_cfg(const char *section, const char *name,
                        const char *value) {
  const char *sec_name = "CHANNEL_";
  size_t sec_name_len = strlen(sec_name);

  if (strlen(section) <= sec_name_len ||
      strncasecmp(section, sec_name, sec_name_len) != 0)
    return;

  if (strlen(value) == 0) {
    supla_log(LOG_ERR, "Empty value in configuration file for key: %s", name);
    return;
  }

  unsigned char number = atoi(&section[sec_name_len]);
  int electricityPhase = -1;
  static const char *reactivePowerAliases[] = {"reactive_power",
                                               "power_reactive"};
  static const char *apparentPowerAliases[] = {"apparent_power",
                                               "power_apparent"};
  static const char *returnedEnergyAliases[] = {"returned_energy",
                                                "reverse_active_energy",
                                                "active_energy_returned"};
  static const char *inductiveEnergyAliases[] = {"inductive_energy",
                                                 "forward_reactive_energy",
                                                 "reactive_energy_inductive"};
  static const char *capacitiveEnergyAliases[] = {"capacitive_energy",
                                                  "reverse_reactive_energy",
                                                  "reactive_energy_capacitive"};

  if (strcasecmp(name, "type") == 0) {
    channelio_set_type(number, decode_channel_type(value));
  } else if (strcasecmp(name, "function") == 0) {
    channelio_set_function(number, decode_function_type(value));
  } else if (strcasecmp(name, "file") == 0 && strlen(value) > 0) {
    channelio_set_filename(number, value);
  } else if (strcasecmp(name, "command") == 0 && strlen(value) > 0) {
    channelio_set_execute(number, value);
  } else if (strcasecmp(name, "command_on") == 0 && strlen(value) > 0) {
    channelio_set_execute_on(number, value);
  } else if (strcasecmp(name, "command_off") == 0 && strlen(value) > 0) {
    channelio_set_execute_off(number, value);
  } else if (strcasecmp(name, "state_topic") == 0 && strlen(value) > 0) {
    channelio_set_mqtt_topic_in(number, value);
  } else if (strcasecmp(name, "command_topic") == 0 && strlen(value) > 0) {
    channelio_set_mqtt_topic_out(number, value);
  } else if (strcasecmp(name, "temperature_topic") == 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_temperature_topic_in(number, value);
  } else if (strcasecmp(name, "humidity_topic") == 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_humidity_topic_in(number, value);
  } else if ((electricityPhase =
                  decode_electricity_measurement_phase(name, "voltage")) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_voltage_topic_phase_in(number, electricityPhase, value);
  } else if ((electricityPhase =
                  decode_electricity_measurement_phase(name, "current")) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_current_topic_phase_in(number, electricityPhase, value);
  } else if ((electricityPhase =
                  decode_electricity_measurement_phase(name, "power")) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_power_topic_phase_in(number, electricityPhase, value);
  } else if ((electricityPhase =
                  decode_electricity_measurement_phase(name, "energy")) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_energy_topic_phase_in(number, electricityPhase, value);
  } else if ((electricityPhase = decode_electricity_measurement_phase_aliases(
                  name, reactivePowerAliases,
                  sizeof(reactivePowerAliases) / sizeof(reactivePowerAliases[0]))) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_reactive_power_topic_phase_in(number, electricityPhase,
                                                     value);
  } else if ((electricityPhase = decode_electricity_measurement_phase_aliases(
                  name, apparentPowerAliases,
                  sizeof(apparentPowerAliases) / sizeof(apparentPowerAliases[0]))) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_apparent_power_topic_phase_in(number, electricityPhase,
                                                     value);
  } else if ((electricityPhase =
                  decode_electricity_measurement_phase(name, "power_factor")) >=
                 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_power_factor_topic_phase_in(number, electricityPhase,
                                                   value);
  } else if ((electricityPhase =
                  decode_electricity_measurement_phase(name, "phase_angle")) >=
                 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_phase_angle_topic_phase_in(number, electricityPhase,
                                                  value);
  } else if ((electricityPhase = decode_electricity_measurement_phase_aliases(
                  name, returnedEnergyAliases,
                  sizeof(returnedEnergyAliases) / sizeof(returnedEnergyAliases[0]))) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_returned_energy_topic_phase_in(number, electricityPhase,
                                                      value);
  } else if ((electricityPhase = decode_electricity_measurement_phase_aliases(
                  name, inductiveEnergyAliases,
                  sizeof(inductiveEnergyAliases) /
                      sizeof(inductiveEnergyAliases[0]))) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_inductive_energy_topic_phase_in(number, electricityPhase,
                                                       value);
  } else if ((electricityPhase = decode_electricity_measurement_phase_aliases(
                  name, capacitiveEnergyAliases,
                  sizeof(capacitiveEnergyAliases) /
                      sizeof(capacitiveEnergyAliases[0]))) >= 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_capacitive_energy_topic_phase_in(
        number, electricityPhase, value);
  } else if ((strcasecmp(name, "frequency_topic") == 0 ||
              strcasecmp(name, "state_frequency_topic") == 0 ||
              strcasecmp(name, "state_frequency") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_frequency_topic_in(number, value);
  } else if (strcasecmp(name, "brightness_topic") == 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_brightness_topic_in(number, value);
  } else if (strcasecmp(name, "color_brightness_topic") == 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_color_brightness_topic_in(number, value);
  } else if (strcasecmp(name, "color_topic") == 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_color_topic_in(number, value);
  } else if ((strcasecmp(name, "measured_temperature_topic") == 0 ||
              strcasecmp(name, "current_temperature_topic") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_measured_temperature_topic_in(number, value);
  } else if ((strcasecmp(name, "preset_temperature_topic") == 0 ||
              strcasecmp(name, "target_temperature_low_topic") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_preset_temperature_topic_in(number, value);
  } else if ((strcasecmp(name, "preset_temperature_high_topic") == 0 ||
              strcasecmp(name, "target_temperature_high_topic") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_preset_temperature_high_topic_in(number, value);
  } else if (strcasecmp(name, "action_topic") == 0 && strlen(value) > 0) {
    channelio_set_mqtt_action_topic_in(number, value);
  } else if (strcasecmp(name, "position_command_topic") == 0 &&
             strlen(value) > 0) {
    channelio_set_mqtt_position_topic_out(number, value);
  } else if ((strcasecmp(name, "preset_temperature_command_topic") == 0 ||
              strcasecmp(name, "target_temperature_low_command_topic") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_preset_temperature_topic_out(number, value);
  } else if ((strcasecmp(name, "preset_temperature_high_command_topic") == 0 ||
              strcasecmp(name, "target_temperature_high_command_topic") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_preset_temperature_high_topic_out(number, value);
  } else if (strcasecmp(name, "retain") == 0 && strlen(value) == 1) {
    channelio_set_mqtt_retain(number, atoi(value));
  } else if (strcasecmp(name, "invert_state") == 0 && strlen(value) == 1) {
    channelio_set_invert_state(number, atoi(value));
  } else if (strcasecmp(name, "esphome_cover") == 0 &&
             strlen(value) == 1) {
    channelio_set_esphome_cover(number, atoi(value));
  } else if (strcasecmp(name, "esphome_rgbw") == 0 &&
             strlen(value) == 1) {
    channelio_set_esphome_rgbw(number, atoi(value));
  } else if ((strcasecmp(name, "report_as_hvac_thermostat") == 0 ||
              strcasecmp(name, "hvac_report_as_thermostat") == 0) &&
             strlen(value) == 1) {
    channelio_set_hvac_report_as_thermostat(number, atoi(value));
  } else if (strcasecmp(name, "hvac_subfunction") == 0 && strlen(value) > 0) {
    channelio_set_hvac_subfunction(number, value);
  } else if (strcasecmp(name, "main_thermometer_channel_no") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_main_thermometer_channel(number, atoi(value));
  } else if (strcasecmp(name, "aux_thermometer_channel_no") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_aux_thermometer_channel(number, atoi(value));
  } else if (strcasecmp(name, "aux_thermometer_type") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_aux_thermometer_type(number, value);
  } else if (strcasecmp(name, "hvac_algorithm") == 0 && strlen(value) > 0) {
    channelio_set_hvac_algorithm(number, value);
  } else if (strcasecmp(name, "hvac_min_on_time_s") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_min_on_time_s(number, atoi(value) % 65536);
  } else if (strcasecmp(name, "hvac_min_off_time_s") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_min_off_time_s(number, atoi(value) % 65536);
  } else if (strcasecmp(name, "hvac_output_value_on_error") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_output_value_on_error(number, atoi(value));
  } else if ((strcasecmp(name, "hvac_antifreeze_overheat_protection") == 0 ||
              strcasecmp(name, "hvac_protection") == 0) &&
             strlen(value) == 1) {
    channelio_set_hvac_antifreeze_overheat_protection(number, atoi(value));
  } else if ((strcasecmp(name, "hvac_aux_minmax_setpoint_enabled") == 0 ||
              strcasecmp(name, "hvac_aux_min_max_setpoint_enabled") == 0) &&
             strlen(value) == 1) {
    channelio_set_hvac_aux_minmax_setpoint_enabled(number, atoi(value));
  } else if ((strcasecmp(name, "hvac_hysteresis") == 0 ||
              strcasecmp(name, "hvac_histeresis") == 0) &&
             strlen(value) > 0) {
    channelio_set_hvac_temperature_cfg(number, TEMPERATURE_HISTERESIS,
                                       parse_hvac_temperature_raw(value));
  } else if ((strcasecmp(name, "hvac_aux_hysteresis") == 0 ||
              strcasecmp(name, "hvac_aux_histeresis") == 0) &&
             strlen(value) > 0) {
    channelio_set_hvac_temperature_cfg(number, TEMPERATURE_AUX_HISTERESIS,
                                       parse_hvac_temperature_raw(value));
  } else if (strcasecmp(name, "hvac_aux_min_setpoint") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_temperature_cfg(number, TEMPERATURE_AUX_MIN_SETPOINT,
                                       parse_hvac_temperature_raw(value));
  } else if (strcasecmp(name, "hvac_aux_max_setpoint") == 0 &&
             strlen(value) > 0) {
    channelio_set_hvac_temperature_cfg(number, TEMPERATURE_AUX_MAX_SETPOINT,
                                       parse_hvac_temperature_raw(value));
  } else if (strcasecmp(name, "value_divider") == 0 && strlen(value) > 0) {
    channelio_set_general_value_divider(number,
                                        parse_general_scaled_value(value));
  } else if (strcasecmp(name, "value_multiplier") == 0 &&
             strlen(value) > 0) {
    channelio_set_general_value_multiplier(number,
                                           parse_general_scaled_value(value));
  } else if (strcasecmp(name, "value_added") == 0 && strlen(value) > 0) {
    channelio_set_general_value_added(number,
                                      parse_general_scaled_value64(value));
  } else if (strcasecmp(name, "value_precision") == 0 && strlen(value) > 0) {
    channelio_set_general_value_precision(number, atoi(value));
  } else if (strcasecmp(name, "unit_before_value") == 0 &&
             strlen(value) > 0) {
    channelio_set_general_unit_before_value(number, value);
  } else if (strcasecmp(name, "unit_after_value") == 0 &&
             strlen(value) > 0) {
    channelio_set_general_unit_after_value(number, value);
  } else if (strcasecmp(name, "no_space_before_value") == 0 &&
             strlen(value) == 1) {
    channelio_set_general_no_space_before_value(number, atoi(value));
  } else if (strcasecmp(name, "no_space_after_value") == 0 &&
             strlen(value) == 1) {
    channelio_set_general_no_space_after_value(number, atoi(value));
  } else if (strcasecmp(name, "keep_history") == 0 && strlen(value) == 1) {
    channelio_set_general_keep_history(number, atoi(value));
  } else if (strcasecmp(name, "chart_type") == 0 && strlen(value) > 0) {
    channelio_set_general_chart_type(number, decode_general_chart_type(value));
  } else if (strcasecmp(name, "refresh_interval_ms") == 0 &&
             strlen(value) > 0) {
    channelio_set_general_refresh_interval_ms(number, atoi(value) % 65536);
  } else if (strcasecmp(name, "state_tamplate") == 0 && strlen(value) > 0) {
    channelio_set_mqtt_template_in(number, value);
  } else if (strcasecmp(name, "command_template") == 0 && strlen(value) > 0) {
    channelio_set_mqtt_template_out(number, value);
  } else if ((strcasecmp(name, "command_template_on") == 0 ||
              strcasecmp(name, "command_payload_on") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_template_on_out(number, value);
  } else if ((strcasecmp(name, "command_template_off") == 0 ||
              strcasecmp(name, "command_payload_off") == 0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_template_off_out(number, value);
  } else if ((strcasecmp(name, "preset_temperature_command_template") == 0 ||
              strcasecmp(name, "target_temperature_low_command_template") ==
                  0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_preset_temperature_template_out(number, value);
  } else if ((strcasecmp(name, "preset_temperature_high_command_template") ==
                  0 ||
              strcasecmp(name, "target_temperature_high_command_template") ==
                  0) &&
             strlen(value) > 0) {
    channelio_set_mqtt_preset_temperature_high_template_out(number, value);
  } else if (strcasecmp(name, "payload_on") == 0 && strlen(value) > 0) {
    channelio_set_payload_on(number, value);
  } else if (strcasecmp(name, "payload_off") == 0 && strlen(value) > 0) {
    channelio_set_payload_off(number, value);
  } else if (strcasecmp(name, "payload_value") == 0 && strlen(value) > 0) {
    channelio_set_payload_value(number, value);
  } else if (strcasecmp(name, "min_interval_sec") == 0 && strlen(value) > 0) {
    channelio_set_interval(number, atoi(value) % 100000);
  } else if (strcasecmp(name, "min_toggle_sec") == 0 && strlen(value) > 0) {
    channelio_set_toggle(number, atoi(value) % 100000);
  } else if (strcasecmp(name, "file_write_check_sec") == 0 &&
             strlen(value) > 0) {
    channelio_set_file_write_check(number, atoi(value) % 100000);
  } else if (strcasecmp(name, "id_Template") == 0 && strlen(value) > 0) {
    channelio_set_id_template(number, value);
  } else if (strcasecmp(name, "id_Value") == 0 && strlen(value) > 0) {
    channelio_set_id_value(number, value);

  } else if (strcasecmp(name, "battery_powered") == 0 && strlen(value) == 1) {
    channelio_set_battery_powered(number, atoi(value));
  }
}

unsigned char devcfg_init(int argc, char *argv[]) {
  memset(DEVICE_GUID, 0, SUPLA_GUID_SIZE);
  memset(DEVICE_AUTHKEY, 0, SUPLA_AUTHKEY_SIZE);

  unsigned char result = 0;

  scfg_set_callback(devcfg_channel_cfg);

  // !!! order is important !!!
  static char s_global[] = "GLOBAL";
  static char global_guid_file[] = "./var/dev_guid";
  static char global_alt_cfg[] = "";
  static char global_state_file[] = "./var/last_state.txt";
  static char global_device_name[] = "SUPLA VIRTUAL DEVICE";
  static char s_server[] = "SERVER";
  static char server_host[] = "";
  static char s_location[] = "LOCATION";
  static char s_auth[] = "AUTH";
  static char auth_email[] = "";
  static char s_mqtt[] = "MQTT";
  static char mqtt_host[] = "";
  static char mqtt_username[] = "";
  static char mqtt_password[] = "";
  static char mqtt_client_name[] = "supla-vd-client";
  static char default_cfg_file[] = "./supla-virtual-device.cfg";

  scfg_add_str_param(s_global, "device_guid_file", global_guid_file);
  scfg_add_str_param(s_global, "alt_cfg", global_alt_cfg);
  scfg_add_str_param(s_global, "state_file", global_state_file);
  scfg_add_str_param(s_global, "device_name", global_device_name);

  scfg_add_str_param(s_server, "host", server_host);
  scfg_add_int_param(s_server, "tcp_port", 2015);
  scfg_add_int_param(s_server, "ssl_port", 2016);
  scfg_add_bool_param(s_server, "ssl_enabled", 1);
  scfg_add_int_param(s_server, "protocol_version", 21);

  scfg_add_int_param(s_location, "ID", 0);
  scfg_add_str_param(s_location, "PASSWORD", 0);

  scfg_add_str_param(s_auth, "email", auth_email);

  scfg_add_str_param(s_mqtt, "host", mqtt_host);
  scfg_add_int_param(s_mqtt, "port", 1833);
  scfg_add_str_param(s_mqtt, "username", mqtt_username);
  scfg_add_str_param(s_mqtt, "password", mqtt_password);
  scfg_add_str_param(s_mqtt, "client_name", mqtt_client_name);

  result = scfg_load(argc, argv, default_cfg_file);

  if (result == 1 && st_file_exists(scfg_string(CFG_ALTCFG_FILE)) == 1) {
    result = scfg_load(argc, argv, scfg_string(CFG_ALTCFG_FILE));
  }

  scfg_names_free();

  return result;
}

char devcfg_getdev_guid() {
  return st_read_guid_from_file(scfg_string(CFG_GUID_FILE), DEVICE_GUID, 1);
}

char devcfg_getdev_authkey() {
  char *guid_file = scfg_string(CFG_GUID_FILE);

  if (guid_file == NULL || strnlen(guid_file, 1024) == 0) return 0;

  int len = strnlen(guid_file, 1024) + 1;
  char result = 0;

  char *authkey_file = (char *)malloc(len);
  snprintf(authkey_file, len, "%s.authkey", guid_file);

  result = st_read_authkey_from_file(authkey_file, DEVICE_AUTHKEY, 1);

  free(authkey_file);

  return result;
}

void devcfg_free(void) { scfg_free(); }

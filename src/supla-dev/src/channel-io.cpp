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

#include "channel-io.h"

#include <assert.h>
#include <cctype>
#include <limits>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "gpio.h"
#include "mcp23008.h"
#include "supla-client-lib/eh.h"
#include "supla-client-lib/lck.h"
#include "supla-client-lib/log.h"
#include "supla-client-lib/safearray.h"
#include "supla-client-lib/srpc.h"
#include "supla-client-lib/sthread.h"
#include "w1.h"

#define GPIO_MINDELAY_USEC 500000
#ifdef __SINGLE_THREAD
#define W1_TEMP_MINDELAY_SEC 120
#else
#define W1_TEMP_MINDELAY_USEC 500000
#endif
#define MCP23008_MINDELAY_SEC 1

void (*mqtt_publish_callback)(const char *topic, const char *payload,
                              char retain, char qos);

client_device_channels *channels = NULL;
static unsigned char runtime_default_config_received[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned char runtime_weekly_config_received[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned char initial_default_config_sent[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned char initial_weekly_config_sent[SUPLA_CHANNELMAXCOUNT + 1];
static TChannelConfig_WeeklySchedule
    runtime_weekly_schedule[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned char
    runtime_weekly_schedule_available[SUPLA_CHANNELMAXCOUNT + 1];
static time_t runtime_weekly_schedule_last_publish[SUPLA_CHANNELMAXCOUNT + 1];
static time_t runtime_hvac_countdown_end[SUPLA_CHANNELMAXCOUNT + 1];
static THVACValue
    runtime_hvac_countdown_restore_value[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned char
    runtime_hvac_countdown_restore_available[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned _supla_int_t
    runtime_hvac_countdown_last_sent[SUPLA_CHANNELMAXCOUNT + 1];
static unsigned char runtime_device_config_received = 0;
static unsigned char initial_device_config_sent = 0;

#ifndef __SINGLE_THREAD
void *thread_w1;
void channelio_w1_execute(void *user_data, void *sthread);
#endif

char channelio_apply_current_hvac_weekly_schedule(
    client_device_channel *channel, char publish, char raise);
char channelio_iterate_hvac_countdown(client_device_channel *channel);
char channelio_set_hvac_value(client_device_channel *channel,
                              char value[SUPLA_CHANNELVALUE_SIZE],
                              unsigned int time_ms);

namespace {

unsigned char channelio_tracking_index(unsigned char channelNumber) {
  if (channelNumber > SUPLA_CHANNELMAXCOUNT) return 0;

  return channelNumber;
}

bool channelio_is_hvac_countdown_active(unsigned char channelNumber) {
  unsigned char trackingIndex = channelio_tracking_index(channelNumber);

  return trackingIndex > 0 && runtime_hvac_countdown_end[trackingIndex] > 1;
}

void channelio_clear_hvac_countdown(unsigned char channelNumber) {
  unsigned char trackingIndex = channelio_tracking_index(channelNumber);
  if (trackingIndex == 0) return;

  runtime_hvac_countdown_end[trackingIndex] = 0;
  runtime_hvac_countdown_restore_available[trackingIndex] = 0;
  runtime_hvac_countdown_last_sent[trackingIndex] = 0;
  memset(&runtime_hvac_countdown_restore_value[trackingIndex], 0,
         sizeof(THVACValue));
}

void channelio_raise_hvac_timer_state(client_device_channel *channel,
                                      unsigned _supla_int_t remainingSeconds,
                                      const THVACValue *targetValue) {
  if (channel == NULL || channels == NULL ||
      channels->on_extendedValueChanged == NULL) {
    return;
  }

  TSuplaChannelExtendedValue extended;
  TTimerState_ExtendedValue timerValue;

  memset(&extended, 0, sizeof(extended));
  memset(&timerValue, 0, sizeof(timerValue));

  extended.type = EV_TYPE_TIMER_STATE_V1_SEC;
  extended.size = sizeof(TTimerState_ExtendedValue);

  timerValue.RemainingTimeS = remainingSeconds;
  if (targetValue != NULL) {
    memcpy(timerValue.TargetValue, targetValue, sizeof(timerValue.TargetValue));
  }

  memcpy(extended.value, &timerValue, sizeof(timerValue));

  channels->on_extendedValueChanged(channel->getNumber(), &extended,
                                    channels->on_valuechanged_user_data);
}

void channelio_set_hvac_temperature(THVACTemperatureCfg *temperatures,
                                    unsigned _supla_int_t key,
                                    _supla_int16_t value) {
  if (temperatures == NULL || key == 0) return;

  temperatures->Index |= key;

  for (unsigned int idx = 0; idx < sizeof(temperatures->Temperature) /
                                        sizeof(temperatures->Temperature[0]);
       idx++) {
    if ((1U << idx) == key) {
      temperatures->Temperature[idx] = value;
      return;
    }
  }
}

bool channelio_is_temperature_channel_function(int function) {
  return function == SUPLA_CHANNELFNC_THERMOMETER ||
         function == SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE;
}

void channelio_copy_general_unit(char target[SUPLA_GENERAL_PURPOSE_UNIT_SIZE],
                                 const std::string &source) {
  memset(target, 0, SUPLA_GENERAL_PURPOSE_UNIT_SIZE);

  if (source.length() == 0) return;

  strncpy(target, source.c_str(), SUPLA_GENERAL_PURPOSE_UNIT_SIZE - 1);
}

std::string channelio_trim_copy(const std::string &value) {
  size_t start = 0;
  while (start < value.length() && std::isspace((unsigned char)value[start])) {
    start++;
  }

  size_t end = value.length();
  while (end > start && std::isspace((unsigned char)value[end - 1])) {
    end--;
  }

  return value.substr(start, end - start);
}

bool channelio_parse_int_in_range(const std::string &value, int minValue,
                                  int maxValue, int *result) {
  if (result == NULL) return false;

  std::string trimmed = channelio_trim_copy(value);
  if (trimmed.length() == 0) return false;

  try {
    size_t pos = 0;
    int parsed = std::stoi(trimmed, &pos, 10);
    if (pos != trimmed.length()) return false;
    if (parsed < minValue) parsed = minValue;
    if (parsed > maxValue) parsed = maxValue;
    *result = parsed;
    return true;
  } catch (std::exception &) {
    return false;
  }
}

bool channelio_parse_color_payload(const std::string &value, int *color) {
  if (color == NULL) return false;

  std::string normalized = channelio_trim_copy(value);
  if (normalized.length() == 0) return false;

  for (size_t idx = 0; idx < normalized.length(); idx++) {
    if (normalized[idx] == ',') normalized[idx] = ' ';
  }

  if (normalized[0] == '#') {
    normalized = normalized.substr(1);
  }

  if (normalized.rfind("0x", 0) == 0 || normalized.rfind("0X", 0) == 0) {
    normalized = normalized.substr(2);
  }

  if (normalized.find(' ') == std::string::npos && normalized.length() == 6) {
    try {
      *color = std::stoi(normalized, NULL, 16);
      return true;
    } catch (std::exception &) {
      return false;
    }
  }

  std::stringstream stream(normalized);
  int red = 0;
  int green = 0;
  int blue = 0;

  if (!(stream >> red >> green >> blue)) return false;

  if (red < 0) red = 0;
  if (red > 255) red = 255;
  if (green < 0) green = 0;
  if (green > 255) green = 255;
  if (blue < 0) blue = 0;
  if (blue > 255) blue = 255;

  *color = ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
  return true;
}

bool channelio_parse_rgblighting_state(const std::string &value, int *color,
                                       char *colorBrightness, char *onOff) {
  if (color == NULL || colorBrightness == NULL || onOff == NULL) return false;

  std::stringstream stream(channelio_trim_copy(value));
  std::string token;
  if (!(stream >> token)) return false;

  if (!channelio_parse_color_payload(token, color)) return false;

  int parsed = 100;
  if (stream >> token) {
    if (channelio_parse_int_in_range(token, 0, 100, &parsed)) {
      *colorBrightness = parsed;
    }
  } else {
    *colorBrightness = parsed;
  }

  int parsedOnOff = *colorBrightness > 0 ? 1 : 0;
  if (stream >> token) {
    if (channelio_parse_int_in_range(token, 0, 1, &parsedOnOff)) {
      *onOff = parsedOnOff;
    }
  } else {
    *onOff = parsedOnOff;
  }

  return true;
}

bool channelio_parse_dimmerandrgb_state(const std::string &value,
                                        char *brightness,
                                        char *colorBrightness, int *color,
                                        char *onOff) {
  if (brightness == NULL || colorBrightness == NULL || color == NULL ||
      onOff == NULL) {
    return false;
  }

  std::string normalized = channelio_trim_copy(value);
  for (size_t idx = 0; idx < normalized.length(); idx++) {
    if (normalized[idx] == ',') normalized[idx] = ' ';
  }

  std::stringstream stream(normalized);
  int parsedBrightness = 0;
  int parsedColorBrightness = 0;
  int red = 0;
  int green = 0;
  int blue = 0;
  int parsedOnOff = 1;

  if (!(stream >> parsedBrightness >> parsedColorBrightness >> red >> green >>
        blue)) {
    return false;
  }

  if (parsedBrightness < 0) parsedBrightness = 0;
  if (parsedBrightness > 100) parsedBrightness = 100;
  if (parsedColorBrightness < 0) parsedColorBrightness = 0;
  if (parsedColorBrightness > 100) parsedColorBrightness = 100;
  if (red < 0) red = 0;
  if (red > 255) red = 255;
  if (green < 0) green = 0;
  if (green > 255) green = 255;
  if (blue < 0) blue = 0;
  if (blue > 255) blue = 255;

  if (!(stream >> parsedOnOff)) {
    parsedOnOff = parsedBrightness > 0 || parsedColorBrightness > 0 ? 1 : 0;
  }

  *brightness = parsedBrightness;
  *colorBrightness = parsedColorBrightness;
  *color = ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
  *onOff = parsedOnOff > 0 ? 1 : 0;
  return true;
}

bool channelio_json_pointer_get_string(const jsoncons::json &payload,
                                       const std::string &path,
                                       std::string *result) {
  if (result == NULL) return false;

  try {
    const jsoncons::json &value = jsoncons::jsonpointer::get(payload, path);

    if (value.is_string()) {
      *result = value.as<std::string>();
      return true;
    }

    if (value.is_bool()) {
      *result = value.as<bool>() ? "true" : "false";
      return true;
    }

    if (value.is_int64()) {
      *result = std::to_string(value.as<int64_t>());
      return true;
    }

    if (value.is_uint64()) {
      *result = std::to_string(value.as<uint64_t>());
      return true;
    }

    if (value.is_double()) {
      std::ostringstream stream;
      stream << value.as<double>();
      *result = stream.str();
      return true;
    }
  } catch (jsoncons::json_exception &) {
  }

  return false;
}

bool channelio_json_pointer_get_int(const jsoncons::json &payload,
                                    const std::string &path, int *result) {
  if (result == NULL) return false;

  try {
    const jsoncons::json &value = jsoncons::jsonpointer::get(payload, path);

    if (value.is_int64()) {
      *result = static_cast<int>(value.as<int64_t>());
      return true;
    }

    if (value.is_uint64()) {
      *result = static_cast<int>(value.as<uint64_t>());
      return true;
    }

    if (value.is_double()) {
      *result = static_cast<int>(value.as<double>());
      return true;
    }

    if (value.is_string()) {
      try {
        *result = std::stoi(channelio_trim_copy(value.as<std::string>()));
        return true;
      } catch (std::exception &) {
        return false;
      }
    }
  } catch (jsoncons::json_exception &) {
  }

  return false;
}

bool channelio_json_pointer_get_uint64(const jsoncons::json &payload,
                                       const std::string &path,
                                       unsigned _supla_int64_t *result) {
  if (result == NULL) return false;

  try {
    const jsoncons::json &value = jsoncons::jsonpointer::get(payload, path);

    if (value.is_uint64()) {
      *result = static_cast<unsigned _supla_int64_t>(value.as<uint64_t>());
      return true;
    }

    if (value.is_int64()) {
      int64_t parsed = value.as<int64_t>();
      if (parsed < 0) return false;
      *result = static_cast<unsigned _supla_int64_t>(parsed);
      return true;
    }

    if (value.is_double()) {
      double parsed = value.as<double>();
      if (parsed < 0) return false;
      if (parsed >
          static_cast<double>(
              std::numeric_limits<unsigned _supla_int64_t>::max())) {
        parsed = static_cast<double>(
            std::numeric_limits<unsigned _supla_int64_t>::max());
      }
      *result = static_cast<unsigned _supla_int64_t>(parsed + 0.5);
      return true;
    }

    if (value.is_string()) {
      try {
        std::string text = channelio_trim_copy(value.as<std::string>());
        if (text.length() == 0) return false;

        size_t pos = 0;
        unsigned long long parsed = std::stoull(text, &pos, 10);
        if (pos != text.length()) return false;
        *result = static_cast<unsigned _supla_int64_t>(parsed);
        return true;
      } catch (std::exception &) {
        return false;
      }
    }
  } catch (jsoncons::json_exception &) {
  }

  return false;
}

bool channelio_json_pointer_get_double(const jsoncons::json &payload,
                                       const std::string &path,
                                       double *result) {
  if (result == NULL) return false;

  try {
    const jsoncons::json &value = jsoncons::jsonpointer::get(payload, path);

    if (value.is_double()) {
      *result = value.as<double>();
      return true;
    }

    if (value.is_int64()) {
      *result = static_cast<double>(value.as<int64_t>());
      return true;
    }

    if (value.is_uint64()) {
      *result = static_cast<double>(value.as<uint64_t>());
      return true;
    }

    if (value.is_string()) {
      try {
        std::string text = channelio_trim_copy(value.as<std::string>());
        if (text.length() == 0) return false;

        size_t pos = 0;
        *result = std::stod(text, &pos);
        return pos == text.length();
      } catch (std::exception &) {
        return false;
      }
    }
  } catch (jsoncons::json_exception &) {
  }

  return false;
}

unsigned _supla_int64_t channelio_scale_meter_total_to_counter(double total) {
  if (total < 0) total = 0;

  double scaled = total * 1000.0;
  if (scaled >
      static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max())) {
    scaled =
        static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max());
  }

  return static_cast<unsigned _supla_int64_t>(scaled + 0.5);
}

_supla_int_t channelio_scale_price_per_unit(double price) {
  if (price < 0) price = 0;

  double scaled = price * 10000.0;
  if (scaled > std::numeric_limits<_supla_int_t>::max()) {
    scaled = std::numeric_limits<_supla_int_t>::max();
  }

  return static_cast<_supla_int_t>(scaled + 0.5);
}

_supla_int_t channelio_scale_total_cost(double cost) {
  if (cost < 0) cost = 0;

  double scaled = cost * 100.0;
  if (scaled > std::numeric_limits<_supla_int_t>::max()) {
    scaled = std::numeric_limits<_supla_int_t>::max();
  }

  return static_cast<_supla_int_t>(scaled + 0.5);
}

unsigned _supla_int64_t channelio_scale_energy_total_kwh(double total) {
  if (total < 0) total = 0;

  double scaled = total * 100000.0;
  if (scaled >
      static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max())) {
    scaled =
        static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max());
  }

  return static_cast<unsigned _supla_int64_t>(scaled + 0.5);
}

unsigned _supla_int_t channelio_scale_energy_total_kwh_short(double total) {
  if (total < 0) total = 0;

  double scaled = total * 100.0;
  if (scaled >
      static_cast<double>(std::numeric_limits<unsigned _supla_int_t>::max())) {
    scaled =
        static_cast<double>(std::numeric_limits<unsigned _supla_int_t>::max());
  }

  return static_cast<unsigned _supla_int_t>(scaled + 0.5);
}

unsigned _supla_int16_t channelio_scale_frequency_hz(double value) {
  if (value < 0) value = 0;

  double scaled = value * 100.0;
  if (scaled > std::numeric_limits<unsigned _supla_int16_t>::max()) {
    scaled = std::numeric_limits<unsigned _supla_int16_t>::max();
  }

  return static_cast<unsigned _supla_int16_t>(scaled + 0.5);
}

unsigned _supla_int16_t channelio_scale_voltage_v(double value) {
  if (value < 0) value = 0;

  double scaled = value * 100.0;
  if (scaled > std::numeric_limits<unsigned _supla_int16_t>::max()) {
    scaled = std::numeric_limits<unsigned _supla_int16_t>::max();
  }

  return static_cast<unsigned _supla_int16_t>(scaled + 0.5);
}

unsigned _supla_int16_t channelio_scale_current_a(double value) {
  if (value < 0) value = 0;

  double scaled = value * 1000.0;
  if (scaled > std::numeric_limits<unsigned _supla_int16_t>::max()) {
    scaled = std::numeric_limits<unsigned _supla_int16_t>::max();
  }

  return static_cast<unsigned _supla_int16_t>(scaled + 0.5);
}

_supla_int_t channelio_scale_power_w(double value) {
  double scaled = value * 100.0;

  if (scaled > std::numeric_limits<_supla_int_t>::max()) {
    scaled = std::numeric_limits<_supla_int_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int_t>::min()) {
    scaled = std::numeric_limits<_supla_int_t>::min();
  }

  return static_cast<_supla_int_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

_supla_int16_t channelio_scale_power_factor(double value) {
  double scaled = value * 1000.0;

  if (scaled > std::numeric_limits<_supla_int16_t>::max()) {
    scaled = std::numeric_limits<_supla_int16_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int16_t>::min()) {
    scaled = std::numeric_limits<_supla_int16_t>::min();
  }

  return static_cast<_supla_int16_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

_supla_int16_t channelio_scale_phase_angle(double value) {
  double scaled = value * 10.0;

  if (scaled > std::numeric_limits<_supla_int16_t>::max()) {
    scaled = std::numeric_limits<_supla_int16_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int16_t>::min()) {
    scaled = std::numeric_limits<_supla_int16_t>::min();
  }

  return static_cast<_supla_int16_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

void channelio_set_impulse_counter_default_unit(int function, char unit[9]) {
  memset(unit, 0, 9);

  switch (function) {
    case SUPLA_CHANNELFNC_IC_ELECTRICITY_METER:
      strncpy(unit, "kWh", 8);
      break;
    case SUPLA_CHANNELFNC_IC_GAS_METER:
    case SUPLA_CHANNELFNC_IC_WATER_METER:
      strncpy(unit, "m3", 8);
      break;
  }
}

bool channelio_parse_impulse_counter_state(
    int function, const std::string &content,
    TSC_ImpulseCounter_ExtendedValue *impulseCounterValue) {
  if (impulseCounterValue == NULL) return false;

  memset(impulseCounterValue, 0, sizeof(TSC_ImpulseCounter_ExtendedValue));
  channelio_set_impulse_counter_default_unit(function,
                                             impulseCounterValue->custom_unit);

  std::string trimmed = channelio_trim_copy(content);
  if (trimmed.length() == 0) return false;

  bool hasCounter = false;
  bool hasHumanReadableTotal = false;
  unsigned _supla_int64_t counter = 0;
  double total = 0;

  try {
    jsoncons::json payload = jsoncons::json::parse(content);
    if (payload.is_object()) {
      hasCounter =
          channelio_json_pointer_get_uint64(payload, "/counter", &counter);

      if (!hasCounter) {
        hasHumanReadableTotal =
            channelio_json_pointer_get_double(payload, "/total_kwh", &total) ||
            channelio_json_pointer_get_double(payload, "/total_m3", &total) ||
            channelio_json_pointer_get_double(payload, "/total", &total) ||
            channelio_json_pointer_get_double(payload, "/value", &total);
      }

      int impulsesPerUnit = 0;
      if (channelio_json_pointer_get_int(payload, "/impulses_per_unit",
                                         &impulsesPerUnit) &&
          impulsesPerUnit > 0) {
        impulseCounterValue->impulses_per_unit = impulsesPerUnit;
      }

      double pricePerUnit = 0;
      if (channelio_json_pointer_get_double(payload, "/price_per_unit",
                                            &pricePerUnit)) {
        impulseCounterValue->price_per_unit =
            channelio_scale_price_per_unit(pricePerUnit);
      }

      double totalCost = 0;
      if (channelio_json_pointer_get_double(payload, "/total_cost",
                                            &totalCost)) {
        impulseCounterValue->total_cost =
            channelio_scale_total_cost(totalCost);
      }

      std::string currency;
      if (channelio_json_pointer_get_string(payload, "/currency", &currency) &&
          currency.length() >= 3) {
        memcpy(impulseCounterValue->currency, currency.c_str(), 3);
      }

      std::string unit;
      if (channelio_json_pointer_get_string(payload, "/unit", &unit) &&
          unit.length() > 0) {
        memset(impulseCounterValue->custom_unit, 0,
               sizeof(impulseCounterValue->custom_unit));
        strncpy(impulseCounterValue->custom_unit, unit.c_str(),
                sizeof(impulseCounterValue->custom_unit) - 1);
      }
    }
  } catch (jsoncons::json_exception &) {
  }

  if (!hasCounter && !hasHumanReadableTotal) {
    try {
      size_t pos = 0;
      total = std::stod(trimmed, &pos);
      if (pos != trimmed.length()) return false;
      hasHumanReadableTotal = true;
    } catch (std::exception &) {
      return false;
    }
  }

  if (!hasCounter && hasHumanReadableTotal) {
    counter = channelio_scale_meter_total_to_counter(total);
    if (impulseCounterValue->impulses_per_unit == 0) {
      impulseCounterValue->impulses_per_unit = 1000;
    }
  }

  impulseCounterValue->counter = counter;

  if (impulseCounterValue->impulses_per_unit > 0) {
    impulseCounterValue->calculated_value =
        static_cast<_supla_int64_t>(counter * 1000 /
                                    impulseCounterValue->impulses_per_unit);
  } else if (hasHumanReadableTotal) {
    impulseCounterValue->calculated_value =
        static_cast<_supla_int64_t>(counter);
  }

  if (impulseCounterValue->total_cost == 0 &&
      impulseCounterValue->price_per_unit > 0 &&
      impulseCounterValue->calculated_value > 0) {
    double calculatedUnits = impulseCounterValue->calculated_value / 1000.0;
    double price = impulseCounterValue->price_per_unit / 10000.0;
    impulseCounterValue->total_cost =
        channelio_scale_total_cost(calculatedUnits * price);
  }

  return true;
}

bool channelio_parse_electricity_meter_state(
    const std::string &content, TElectricityMeter_Value *channelValue,
    TElectricityMeter_ExtendedValue *electricityMeterValue) {
  if (channelValue == NULL || electricityMeterValue == NULL) return false;

  memset(channelValue, 0, sizeof(TElectricityMeter_Value));
  memset(electricityMeterValue, 0, sizeof(TElectricityMeter_ExtendedValue));

  std::string trimmed = channelio_trim_copy(content);
  if (trimmed.length() == 0) return false;

  bool hasAnyData = false;
  bool hasMeasurement = false;
  double totalKwh = 0;

  try {
    jsoncons::json payload = jsoncons::json::parse(content);
    if (payload.is_object()) {
      if (channelio_json_pointer_get_double(payload, "/total_kwh", &totalKwh) ||
          channelio_json_pointer_get_double(payload, "/energy_kwh", &totalKwh) ||
          channelio_json_pointer_get_double(payload, "/total_energy_kwh",
                                            &totalKwh) ||
          channelio_json_pointer_get_double(
              payload, "/total_forward_active_energy_kwh", &totalKwh) ||
          channelio_json_pointer_get_double(payload, "/value", &totalKwh) ||
          channelio_json_pointer_get_double(payload, "/total", &totalKwh)) {
        electricityMeterValue->total_forward_active_energy[0] =
            channelio_scale_energy_total_kwh(totalKwh);
        channelValue->total_forward_active_energy =
            channelio_scale_energy_total_kwh_short(totalKwh);
        electricityMeterValue->measured_values |= EM_VAR_FORWARD_ACTIVE_ENERGY;
        hasAnyData = true;
      }

      double reverseTotal = 0;
      if (channelio_json_pointer_get_double(
              payload, "/total_reverse_active_energy_kwh", &reverseTotal)) {
        electricityMeterValue->total_reverse_active_energy[0] =
            channelio_scale_energy_total_kwh(reverseTotal);
        electricityMeterValue->measured_values |= EM_VAR_REVERSE_ACTIVE_ENERGY;
        hasAnyData = true;
      }

      double reactiveForward = 0;
      if (channelio_json_pointer_get_double(
              payload, "/total_forward_reactive_energy_kvarh",
              &reactiveForward)) {
        electricityMeterValue->total_forward_reactive_energy[0] =
            channelio_scale_energy_total_kwh(reactiveForward);
        electricityMeterValue->measured_values |=
            EM_VAR_FORWARD_REACTIVE_ENERGY;
        hasAnyData = true;
      }

      double reactiveReverse = 0;
      if (channelio_json_pointer_get_double(
              payload, "/total_reverse_reactive_energy_kvarh",
              &reactiveReverse)) {
        electricityMeterValue->total_reverse_reactive_energy[0] =
            channelio_scale_energy_total_kwh(reactiveReverse);
        electricityMeterValue->measured_values |=
            EM_VAR_REVERSE_REACTIVE_ENERGY;
        hasAnyData = true;
      }

      double frequency = 0;
      if (channelio_json_pointer_get_double(payload, "/frequency_hz",
                                            &frequency) ||
          channelio_json_pointer_get_double(payload, "/frequency", &frequency)) {
        electricityMeterValue->m[0].freq =
            channelio_scale_frequency_hz(frequency);
        electricityMeterValue->measured_values |= EM_VAR_FREQ;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double voltage = 0;
      if (channelio_json_pointer_get_double(payload, "/voltage_v", &voltage) ||
          channelio_json_pointer_get_double(payload, "/voltage", &voltage)) {
        electricityMeterValue->m[0].voltage[0] =
            channelio_scale_voltage_v(voltage);
        electricityMeterValue->measured_values |= EM_VAR_VOLTAGE;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double current = 0;
      if (channelio_json_pointer_get_double(payload, "/current_a", &current) ||
          channelio_json_pointer_get_double(payload, "/current", &current)) {
        electricityMeterValue->m[0].current[0] =
            channelio_scale_current_a(current);
        electricityMeterValue->measured_values |= EM_VAR_CURRENT;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double powerActive = 0;
      if (channelio_json_pointer_get_double(payload, "/power_w",
                                            &powerActive) ||
          channelio_json_pointer_get_double(payload, "/power_active_w",
                                            &powerActive) ||
          channelio_json_pointer_get_double(payload, "/active_power_w",
                                            &powerActive) ||
          channelio_json_pointer_get_double(payload, "/power", &powerActive)) {
        electricityMeterValue->m[0].power_active[0] =
            channelio_scale_power_w(powerActive);
        electricityMeterValue->measured_values |= EM_VAR_POWER_ACTIVE;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double powerReactive = 0;
      if (channelio_json_pointer_get_double(payload, "/power_reactive_var",
                                            &powerReactive) ||
          channelio_json_pointer_get_double(payload, "/reactive_power_var",
                                            &powerReactive)) {
        electricityMeterValue->m[0].power_reactive[0] =
            channelio_scale_power_w(powerReactive);
        electricityMeterValue->measured_values |= EM_VAR_POWER_REACTIVE;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double powerApparent = 0;
      if (channelio_json_pointer_get_double(payload, "/power_apparent_va",
                                            &powerApparent) ||
          channelio_json_pointer_get_double(payload, "/apparent_power_va",
                                            &powerApparent)) {
        electricityMeterValue->m[0].power_apparent[0] =
            channelio_scale_power_w(powerApparent);
        electricityMeterValue->measured_values |= EM_VAR_POWER_APPARENT;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double powerFactor = 0;
      if (channelio_json_pointer_get_double(payload, "/power_factor",
                                            &powerFactor)) {
        electricityMeterValue->m[0].power_factor[0] =
            channelio_scale_power_factor(powerFactor);
        electricityMeterValue->measured_values |= EM_VAR_POWER_FACTOR;
        hasMeasurement = true;
        hasAnyData = true;
      }

      double phaseAngle = 0;
      if (channelio_json_pointer_get_double(payload, "/phase_angle_deg",
                                            &phaseAngle) ||
          channelio_json_pointer_get_double(payload, "/phase_angle",
                                            &phaseAngle)) {
        electricityMeterValue->m[0].phase_angle[0] =
            channelio_scale_phase_angle(phaseAngle);
        electricityMeterValue->measured_values |= EM_VAR_PHASE_ANGLE;
        hasMeasurement = true;
        hasAnyData = true;
      }

      int period = 0;
      if (channelio_json_pointer_get_int(payload, "/period_sec", &period) ||
          channelio_json_pointer_get_int(payload, "/period", &period)) {
        if (period < 0) period = 0;
        electricityMeterValue->period = period;
      }

      double pricePerUnit = 0;
      if (channelio_json_pointer_get_double(payload, "/price_per_unit",
                                            &pricePerUnit)) {
        electricityMeterValue->price_per_unit =
            channelio_scale_price_per_unit(pricePerUnit);
      }

      double totalCost = 0;
      if (channelio_json_pointer_get_double(payload, "/total_cost",
                                            &totalCost)) {
        electricityMeterValue->total_cost =
            channelio_scale_total_cost(totalCost);
      }

      std::string currency;
      if (channelio_json_pointer_get_string(payload, "/currency", &currency) &&
          currency.length() >= 3) {
        memcpy(electricityMeterValue->currency, currency.c_str(), 3);
      }
    }
  } catch (jsoncons::json_exception &) {
  }

  if (!hasAnyData) {
    try {
      size_t pos = 0;
      totalKwh = std::stod(trimmed, &pos);
      if (pos != trimmed.length()) return false;

      electricityMeterValue->total_forward_active_energy[0] =
          channelio_scale_energy_total_kwh(totalKwh);
      channelValue->total_forward_active_energy =
          channelio_scale_energy_total_kwh_short(totalKwh);
      electricityMeterValue->measured_values |= EM_VAR_FORWARD_ACTIVE_ENERGY;
      hasAnyData = true;
    } catch (std::exception &) {
      return false;
    }
  }

  if (hasMeasurement) {
    electricityMeterValue->m_count = 1;
  }

  if (hasAnyData) {
    channelValue->flags = EM_VALUE_FLAG_PHASE1_ON;
  }

  return hasAnyData;
}

unsigned char channelio_find_hvac_main_thermometer_channel(
    client_device_channel *channel) {
  if (channel == NULL || channels == NULL) return 0;

  unsigned char configuredChannelNo = channel->getHvacMainThermometerChannelNo();
  if (configuredChannelNo > 0 && configuredChannelNo != channel->getNumber()) {
    client_device_channel *configuredChannel =
        channels->find_channel(configuredChannelNo);

    if (configuredChannel != NULL &&
        channelio_is_temperature_channel_function(
            configuredChannel->getFunction())) {
      return configuredChannelNo;
    }
  }

  for (int idx = 0; idx < channels->getChannelCount(); idx++) {
    client_device_channel *candidate = channels->getChannel(idx);

    if (candidate == NULL || candidate->getNumber() == channel->getNumber()) {
      continue;
    }

    if (channelio_is_temperature_channel_function(candidate->getFunction())) {
      return candidate->getNumber();
    }
  }

  return 0;
}

void channelio_fill_hvac_config(client_device_channel *channel,
                                TSDS_SetChannelConfig *request) {
  memset(request, 0, sizeof(TSDS_SetChannelConfig));
  request->ChannelNumber = channel->getNumber();
  request->Func = channel->getFunction();
  request->ConfigType = SUPLA_CONFIG_TYPE_DEFAULT;
  request->ConfigSize = sizeof(TChannelConfig_HVAC);

  TChannelConfig_HVAC *config = (TChannelConfig_HVAC *)request->Config;
  memset(config, 0, sizeof(TChannelConfig_HVAC));

  unsigned char mainThermometerChannelNo =
      channelio_find_hvac_main_thermometer_channel(channel);
  config->MainThermometerChannelNo =
      mainThermometerChannelNo > 0 ? mainThermometerChannelNo
                                   : channel->getNumber();
  config->AuxThermometerChannelNo = channel->getNumber();
  config->BinarySensorChannelNo = channel->getNumber();
  config->AvailableAlgorithms = SUPLA_HVAC_ALGORITHM_ON_OFF_SETPOINT_MIDDLE;
  config->UsedAlgorithm = SUPLA_HVAC_ALGORITHM_ON_OFF_SETPOINT_MIDDLE;
  config->Subfunction = channel->getHvacSubfunctionCool()
                            ? SUPLA_HVAC_SUBFUNCTION_COOL
                            : SUPLA_HVAC_SUBFUNCTION_HEAT;
  config->TemperatureSetpointChangeSwitchesToManualMode = 1;
  config->TemperatureControlType =
      SUPLA_HVAC_TEMPERATURE_CONTROL_TYPE_ROOM_TEMPERATURE;
  config->MinAllowedTemperatureSetpointFromLocalUI = 500;
  config->MaxAllowedTemperatureSetpointFromLocalUI = 3500;

  if (mainThermometerChannelNo > 0) {
    config->ParameterFlags.MainThermometerChannelNoReadonly = 1;
    config->ParameterFlags.MainThermometerChannelNoHidden = 1;
  }
  config->ParameterFlags.AuxThermometerChannelNoReadonly = 1;
  config->ParameterFlags.AuxThermometerChannelNoHidden = 1;
  config->ParameterFlags.BinarySensorChannelNoReadonly = 1;
  config->ParameterFlags.BinarySensorChannelNoHidden = 1;
  config->ParameterFlags.AuxThermometerTypeReadonly = 1;
  config->ParameterFlags.AuxThermometerTypeHidden = 1;
  config->ParameterFlags.AntiFreezeAndOverheatProtectionEnabledReadonly = 1;
  config->ParameterFlags.AntiFreezeAndOverheatProtectionEnabledHidden = 1;
  config->ParameterFlags.UsedAlgorithmReadonly = 1;
  config->ParameterFlags.UsedAlgorithmHidden = 1;
  config->ParameterFlags.MinOnTimeSReadonly = 1;
  config->ParameterFlags.MinOnTimeSHidden = 1;
  config->ParameterFlags.MinOffTimeSReadonly = 1;
  config->ParameterFlags.MinOffTimeSHidden = 1;
  config->ParameterFlags.OutputValueOnErrorReadonly = 1;
  config->ParameterFlags.OutputValueOnErrorHidden = 1;
  config->ParameterFlags.SubfunctionReadonly = 1;
  config->ParameterFlags.SubfunctionHidden = 1;
  config->ParameterFlags.TemperatureSetpointChangeSwitchesToManualModeReadonly =
      1;
  config->ParameterFlags.TemperatureSetpointChangeSwitchesToManualModeHidden =
      1;
  config->ParameterFlags.AuxMinMaxSetpointEnabledReadonly = 1;
  config->ParameterFlags.AuxMinMaxSetpointEnabledHidden = 1;
  config->ParameterFlags.UseSeparateHeatCoolOutputsReadonly = 1;
  config->ParameterFlags.UseSeparateHeatCoolOutputsHidden = 1;
  config->ParameterFlags.TemperaturesFreezeProtectionReadonly = 1;
  config->ParameterFlags.TemperaturesFreezeProtectionHidden = 1;
  config->ParameterFlags.TemperaturesEcoReadonly = 1;
  config->ParameterFlags.TemperaturesEcoHidden = 1;
  config->ParameterFlags.TemperaturesComfortReadonly = 1;
  config->ParameterFlags.TemperaturesComfortHidden = 1;
  config->ParameterFlags.TemperaturesBoostReadonly = 1;
  config->ParameterFlags.TemperaturesBoostHidden = 1;
  config->ParameterFlags.TemperaturesHeatProtectionReadonly = 1;
  config->ParameterFlags.TemperaturesHeatProtectionHidden = 1;
  config->ParameterFlags.TemperaturesBelowAlarmReadonly = 1;
  config->ParameterFlags.TemperaturesBelowAlarmHidden = 1;
  config->ParameterFlags.TemperaturesAboveAlarmReadonly = 1;
  config->ParameterFlags.TemperaturesAboveAlarmHidden = 1;
  config->ParameterFlags.TemperaturesAuxMinSetpointReadonly = 1;
  config->ParameterFlags.TemperaturesAuxMinSetpointHidden = 1;
  config->ParameterFlags.TemperaturesAuxMaxSetpointReadonly = 1;
  config->ParameterFlags.TemperaturesAuxMaxSetpointHidden = 1;
  config->ParameterFlags.MasterThermostatChannelNoReadonly = 1;
  config->ParameterFlags.MasterThermostatChannelNoHidden = 1;
  config->ParameterFlags.HeatOrColdSourceSwitchReadonly = 1;
  config->ParameterFlags.HeatOrColdSourceSwitchHidden = 1;
  config->ParameterFlags.PumpSwitchReadonly = 1;
  config->ParameterFlags.PumpSwitchHidden = 1;
  config->ParameterFlags.TemperaturesAuxHisteresisReadonly = 1;
  config->ParameterFlags.TemperaturesAuxHisteresisHidden = 1;

  channelio_set_hvac_temperature(&config->Temperatures, TEMPERATURE_ROOM_MIN,
                                 500);
  channelio_set_hvac_temperature(&config->Temperatures, TEMPERATURE_ROOM_MAX,
                                 3500);
  channelio_set_hvac_temperature(&config->Temperatures,
                                 TEMPERATURE_HISTERESIS_MIN, 10);
  channelio_set_hvac_temperature(&config->Temperatures,
                                 TEMPERATURE_HISTERESIS_MAX, 500);
  channelio_set_hvac_temperature(&config->Temperatures, TEMPERATURE_HISTERESIS,
                                 50);
}

void channelio_fill_hvac_weekly_schedule_config(client_device_channel *channel,
                                                TSDS_SetChannelConfig *request) {
  memset(request, 0, sizeof(TSDS_SetChannelConfig));
  request->ChannelNumber = channel->getNumber();
  request->Func = channel->getFunction();
  request->ConfigType = SUPLA_CONFIG_TYPE_WEEKLY_SCHEDULE;
  request->ConfigSize = sizeof(TChannelConfig_WeeklySchedule);

  TChannelConfig_WeeklySchedule *config =
      (TChannelConfig_WeeklySchedule *)request->Config;
  memset(config, 0, sizeof(TChannelConfig_WeeklySchedule));

  for (unsigned char idx = 0; idx < SUPLA_WEEKLY_SCHEDULE_PROGRAMS_MAX_SIZE;
       idx++) {
    config->Program[idx].Mode = channel->getHvacSubfunctionCool()
                                    ? SUPLA_HVAC_MODE_COOL
                                    : SUPLA_HVAC_MODE_HEAT;
    config->Program[idx].SetpointTemperatureHeat = (20 + idx) * 100;
    config->Program[idx].SetpointTemperatureCool = (24 + idx) * 100;
  }
}

void channelio_fill_general_measurement_config(
    client_device_channel *channel, TSDS_SetChannelConfig *request) {
  memset(request, 0, sizeof(TSDS_SetChannelConfig));
  request->ChannelNumber = channel->getNumber();
  request->Func = channel->getFunction();
  request->ConfigType = SUPLA_CONFIG_TYPE_DEFAULT;
  request->ConfigSize = sizeof(TChannelConfig_GeneralPurposeMeasurement);

  TChannelConfig_GeneralPurposeMeasurement *config =
      (TChannelConfig_GeneralPurposeMeasurement *)request->Config;
  memset(config, 0, sizeof(TChannelConfig_GeneralPurposeMeasurement));

  config->ValueDivider = channel->getGeneralValueDivider();
  config->ValueMultiplier = channel->getGeneralValueMultiplier();
  config->ValueAdded = channel->getGeneralValueAdded();
  config->ValuePrecision = channel->getGeneralValuePrecision();
  config->NoSpaceBeforeValue = channel->getGeneralNoSpaceBeforeValue() ? 1 : 0;
  config->NoSpaceAfterValue = channel->getGeneralNoSpaceAfterValue() ? 1 : 0;
  config->KeepHistory = channel->getGeneralKeepHistory() ? 1 : 0;
  config->ChartType = channel->getGeneralChartType();
  config->RefreshIntervalMs = channel->getGeneralRefreshIntervalMs();

  config->DefaultValueDivider = channel->getGeneralValueDivider();
  config->DefaultValueMultiplier = channel->getGeneralValueMultiplier();
  config->DefaultValueAdded = channel->getGeneralValueAdded();
  config->DefaultValuePrecision = channel->getGeneralValuePrecision();

  channelio_copy_general_unit(config->UnitBeforeValue,
                              channel->getGeneralUnitBeforeValue());
  channelio_copy_general_unit(config->UnitAfterValue,
                              channel->getGeneralUnitAfterValue());
  channelio_copy_general_unit(config->DefaultUnitBeforeValue,
                              channel->getGeneralUnitBeforeValue());
  channelio_copy_general_unit(config->DefaultUnitAfterValue,
                              channel->getGeneralUnitAfterValue());
}

int channelio_get_weekly_schedule_program_id(
    const TChannelConfig_WeeklySchedule *schedule, int index) {
  if (schedule == NULL || index < 0 ||
      index >= SUPLA_WEEKLY_SCHEDULE_VALUES_SIZE) {
    return 0;
  }

  return (schedule->Quarters[index / 2] >> ((index % 2) * 4)) & 0xF;
}

int channelio_get_current_weekly_schedule_index() {
  time_t now = time(NULL);
  if (now == (time_t)-1) return -1;

  struct tm timeInfo;

#ifdef _WIN32
  if (localtime_s(&timeInfo, &now) != 0) {
    return -1;
  }
#else
  if (localtime_r(&now, &timeInfo) == NULL) {
    return -1;
  }
#endif

  return (timeInfo.tm_wday * 24 + timeInfo.tm_hour) * 4 +
         (timeInfo.tm_min / 15);
}

void channelio_get_hvac_weekly_schedule(client_device_channel *channel,
                                        TChannelConfig_WeeklySchedule *config) {
  if (config == NULL) return;

  memset(config, 0, sizeof(TChannelConfig_WeeklySchedule));
  if (channel == NULL) return;

  unsigned char trackingIndex = channelio_tracking_index(channel->getNumber());
  if (trackingIndex > 0 && runtime_weekly_schedule_available[trackingIndex]) {
    memcpy(config, &runtime_weekly_schedule[trackingIndex],
           sizeof(TChannelConfig_WeeklySchedule));
    return;
  }

  TSDS_SetChannelConfig request;
  channelio_fill_hvac_weekly_schedule_config(channel, &request);
  memcpy(config, request.Config, sizeof(TChannelConfig_WeeklySchedule));
}

unsigned char channelio_get_hvac_default_mode(client_device_channel *channel) {
  if (channel != NULL && channel->getHvacSubfunctionCool()) {
    return SUPLA_HVAC_MODE_COOL;
  }

  return SUPLA_HVAC_MODE_HEAT;
}

char channelio_get_current_hvac_program(client_device_channel *channel,
                                        TWeeklyScheduleProgram *program) {
  if (channel == NULL || program == NULL) return 0;

  TChannelConfig_WeeklySchedule schedule;
  channelio_get_hvac_weekly_schedule(channel, &schedule);

  int programId = 1;
  int quarterIndex = channelio_get_current_weekly_schedule_index();
  if (quarterIndex >= 0) {
    programId = channelio_get_weekly_schedule_program_id(&schedule, quarterIndex);
  }

  memset(program, 0, sizeof(TWeeklyScheduleProgram));
  program->SetpointTemperatureHeat = SUPLA_TEMPERATURE_INVALID_INT16;
  program->SetpointTemperatureCool = SUPLA_TEMPERATURE_INVALID_INT16;

  if (programId == 0) {
    program->Mode = SUPLA_HVAC_MODE_OFF;
    return 1;
  }

  if (programId < 0 || programId > SUPLA_WEEKLY_SCHEDULE_PROGRAMS_MAX_SIZE) {
    return 0;
  }

  *program = schedule.Program[programId - 1];
  return 1;
}

void channelio_fill_device_config(TSDS_SetDeviceConfig *request) {
  memset(request, 0, sizeof(TSDS_SetDeviceConfig));
  request->EndOfDataFlag = 1;
  request->AvailableFields = SUPLA_DEVICE_CONFIG_FIELD_STATUS_LED;
  request->Fields = SUPLA_DEVICE_CONFIG_FIELD_STATUS_LED;
  request->ConfigSize = sizeof(TDeviceConfig_StatusLed);

  TDeviceConfig_StatusLed *config = (TDeviceConfig_StatusLed *)request->Config;
  config->StatusLedType = SUPLA_DEVCFG_STATUS_LED_ALWAYS_OFF;
}

char channelio_send_initial_hvac_config(client_device_channel *channel,
                                        void *srpc) {
  TSDS_SetChannelConfig config;
  channelio_fill_hvac_config(channel, &config);

  if (srpc_ds_async_set_channel_config_request(srpc, &config) == 0) {
    supla_log(LOG_WARNING, "failed to send initial HVAC config for channel %d",
              channel->getNumber());
    return 0;
  }

  supla_log(LOG_DEBUG, "sent initial HVAC config for channel %d",
            channel->getNumber());
  return 1;
}

char channelio_send_initial_hvac_weekly_schedule_config(
    client_device_channel *channel, void *srpc) {
  TSDS_SetChannelConfig config;
  channelio_fill_hvac_weekly_schedule_config(channel, &config);

  if (srpc_ds_async_set_channel_config_request(srpc, &config) == 0) {
    supla_log(LOG_WARNING,
              "failed to send initial HVAC weekly schedule for channel %d",
              channel->getNumber());
    return 0;
  }

  supla_log(LOG_DEBUG, "sent initial HVAC weekly schedule for channel %d",
            channel->getNumber());
  return 1;
}

char channelio_send_initial_general_measurement_config(
    client_device_channel *channel, void *srpc) {
  TSDS_SetChannelConfig config;
  channelio_fill_general_measurement_config(channel, &config);

  if (srpc_ds_async_set_channel_config_request(srpc, &config) == 0) {
    supla_log(LOG_WARNING,
              "failed to send initial GENERAL config for channel %d",
              channel->getNumber());
    return 0;
  }

  supla_log(LOG_DEBUG, "sent initial GENERAL config for channel %d",
            channel->getNumber());
  return 1;
}

char channelio_send_initial_device_config_request(void *srpc) {
  TSDS_SetDeviceConfig config;
  channelio_fill_device_config(&config);

  if (srpc_ds_async_set_device_config_request(srpc, &config) == 0) {
    supla_log(LOG_WARNING, "failed to send initial device config");
    return 0;
  }

  supla_log(LOG_DEBUG, "sent initial device config");
  return 1;
}

}  // namespace

void channelio_raise_valuechanged(client_device_channel *channel) {
  char value[SUPLA_CHANNELVALUE_SIZE];

  channel->getValue(value);

  TSuplaChannelExtendedValue extended;

  if (channels->on_valuechanged)
    channels->on_valuechanged(channel->getNumber(), value,
                              channels->on_valuechanged_user_data);

  if (channels->on_extendedValueChanged &&
      channel->getExtendedValue(&extended)) {
    channels->on_extendedValueChanged(channel->getNumber(), &extended,
                                      channels->on_valuechanged_user_data);
  }
}

bool read_file_to_string(const std::string &p_name, std::string &p_content) {
  // We create the file object, saying I want to read it
  std::fstream file(p_name.c_str(), std::fstream::in);

  // We verify if the file was successfully opened
  if (file.is_open()) {
    // We use the standard getline function to read the file into
    // a std::string, stoping only at "\0"
    std::getline(file, p_content, '\0');

    // We return the success of the operation
    return !file.bad();
  }

  // The file was not successfully opened, so returning false
  return false;
}

bool isFileOk(std::string filename, int file_write_sec) {
  struct timeval now;
  gettimeofday(&now, NULL);

  struct stat result;
  if (file_write_sec > 0) {
    if (stat(filename.c_str(), &result) == 0) {
      auto mod_time = result.st_mtim;
      if (now.tv_sec - mod_time.tv_sec >= file_write_sec) {
        supla_log(LOG_ERR, "file write check error! ");
        return false;
      }
    }
  }
  return true;
}

char channelio_read_from_file(client_device_channel *channel, char log_err) {
  double val1 = -275, val2 = -1;
  unsigned char val3 = 0;

  struct timeval now;
  char read_result = 0;

  if (channel->getFileName().length() > 0) {
    gettimeofday(&now, NULL);

    int interval_sec = channel->getIntervalSec();
    int min_interval = interval_sec >= 0 ? interval_sec : 10;
    if (now.tv_sec - channel->getLastSeconds() >= min_interval) {
      channel->setLastSeconds();

      supla_log(LOG_DEBUG, "reading channel_%d value from file %s",
                channel->getNumber(), channel->getFileName().c_str());
      try {
        std::string content;
        if (channel->getFunction() == SUPLA_CHANNELFNC_ELECTRICITY_METER ||
            channel->getFunction() == SUPLA_CHANNELFNC_IC_ELECTRICITY_METER ||
            channel->getFunction() == SUPLA_CHANNELFNC_IC_GAS_METER ||
            channel->getFunction() == SUPLA_CHANNELFNC_IC_WATER_METER) {
          read_result = read_file_to_string(channel->getFileName(), content);
        } else if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER ||
                   channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING ||
                   channel->getFunction() ==
                       SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
          read_result = read_file_to_string(channel->getFileName(), content);
        } else {
          read_result = file_read_sensor(channel->getFileName().c_str(), &val1,
                                         &val2, &val3);
        };

        char tmp_value[SUPLA_CHANNELVALUE_SIZE];
        memset(tmp_value, 0, SUPLA_CHANNELVALUE_SIZE);

        switch (channel->getFunction()) {
          case SUPLA_CHANNELFNC_ELECTRICITY_METER: {
            if (!read_result) return 0;

            TElectricityMeter_Value channelValue;
            TElectricityMeter_ExtendedValue electricityMeterValue;

            if (!channelio_parse_electricity_meter_state(
                    content, &channelValue, &electricityMeterValue)) {
              supla_log(LOG_DEBUG, "readed content %s", content.c_str());
              read_result = 0;
              break;
            }

            char newValue[SUPLA_CHANNELVALUE_SIZE];
            memset(newValue, 0, SUPLA_CHANNELVALUE_SIZE);
            memcpy(newValue, &channelValue, sizeof(channelValue));
            channel->setValue(newValue);

            TSuplaChannelExtendedValue extendedValue;
            if (srpc_evtool_v1_emextended2extended(&electricityMeterValue,
                                                   &extendedValue) == 1) {
              channel->setExtendedValue(&extendedValue);
            } else {
              channel->setExtendedValue(NULL);
            }

            return 1;
          } break;
          case SUPLA_CHANNELFNC_IC_ELECTRICITY_METER:
          case SUPLA_CHANNELFNC_IC_GAS_METER:
          case SUPLA_CHANNELFNC_IC_WATER_METER: {
            if (!read_result) return 0;

            TSC_ImpulseCounter_ExtendedValue impulseCounterValue;
            if (!channelio_parse_impulse_counter_state(channel->getFunction(),
                                                       content,
                                                       &impulseCounterValue)) {
              supla_log(LOG_DEBUG, "readed content %s", content.c_str());
              read_result = 0;
              break;
            }

            TDS_ImpulseCounter_Value channelValue;
            memset(&channelValue, 0, sizeof(channelValue));
            channelValue.counter = impulseCounterValue.counter;

            char newValue[SUPLA_CHANNELVALUE_SIZE];
            memset(newValue, 0, SUPLA_CHANNELVALUE_SIZE);
            memcpy(newValue, &channelValue, sizeof(channelValue));
            channel->setValue(newValue);

            TSuplaChannelExtendedValue extendedValue;
            if (srpc_evtool_v1_icextended2extended(&impulseCounterValue,
                                                   &extendedValue) == 1) {
              channel->setExtendedValue(&extendedValue);
            } else {
              channel->setExtendedValue(NULL);
            }

            return 1;
          } break;
          case SUPLA_CHANNELFNC_POWERSWITCH:
          case SUPLA_CHANNELFNC_LIGHTSWITCH:
          case SUPLA_CHANNELFNC_STAIRCASETIMER:
          case SUPLA_CHANNELFNC_CONTROLLINGTHEDOORLOCK:
          case SUPLA_CHANNELFNC_CONTROLLINGTHEGARAGEDOOR:
          case SUPLA_CHANNELFNC_CONTROLLINGTHEGATEWAYLOCK:
          case SUPLA_CHANNELFNC_CONTROLLINGTHEGATE:
          case SUPLA_CHANNELFNC_OPENINGSENSOR_GATEWAY:
          case SUPLA_CHANNELFNC_OPENINGSENSOR_GATE:
          case SUPLA_CHANNELFNC_OPENINGSENSOR_GARAGEDOOR:
          case SUPLA_CHANNELFNC_OPENINGSENSOR_DOOR:
          case SUPLA_CHANNELFNC_NOLIQUIDSENSOR:
          case SUPLA_CHANNELFNC_OPENINGSENSOR_ROLLERSHUTTER:
          case SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW:  // ver. >= 8
          case SUPLA_CHANNELFNC_MAILSENSOR: {
            tmp_value[0] = val1 == 1 ? 1 : 0;
          } break;
          case SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER: {
            int shutter = static_cast<int>(val1);

            if (shutter < 0) shutter = 0;
            if (shutter > 100) shutter = 100;

            tmp_value[0] = shutter;
          } break;
          case SUPLA_CHANNELFNC_DIMMER: {
            int brightness = 0;
            if (!read_result ||
                !channelio_parse_int_in_range(content, 0, 100, &brightness)) {
              read_result = 0;
              break;
            }

            channel->setRGBW(0, 0, brightness, brightness > 0 ? 1 : 0);
            channel->getValue(tmp_value);
          } break;
          case SUPLA_CHANNELFNC_RGBLIGHTING: {
            int color = 0;
            char colorBrightness = 0;
            char onOff = 0;

            if (!read_result ||
                !channelio_parse_rgblighting_state(content, &color,
                                                   &colorBrightness, &onOff)) {
              read_result = 0;
              break;
            }

            channel->setRGBW(color, colorBrightness, 0, onOff);
            channel->getValue(tmp_value);
          } break;
          case SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING: {
            int color = 0;
            char brightness = 0;
            char colorBrightness = 0;
            char onOff = 0;

            if (!read_result ||
                !channelio_parse_dimmerandrgb_state(content, &brightness,
                                                    &colorBrightness, &color,
                                                    &onOff)) {
              read_result = 0;
              break;
            }

            channel->setRGBW(color, colorBrightness, brightness, onOff);
            channel->getValue(tmp_value);
          } break;
          case SUPLA_CHANNELFNC_THERMOMETER:
          case SUPLA_CHANNELFNC_DISTANCESENSOR:
          case SUPLA_CHANNELFNC_DEPTHSENSOR:
          case SUPLA_CHANNELFNC_WINDSENSOR:
          case SUPLA_CHANNELFNC_PRESSURESENSOR:
          case SUPLA_CHANNELFNC_RAINSENSOR:
          case SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT:
          case SUPLA_CHANNELFNC_WEIGHTSENSOR: {
            if (!isFileOk(channel->getFileName(),
                          channel->getFileWriteCheckSec()))
              val1 = -275;

            memcpy(tmp_value, &val1, sizeof(double));
          } break;
          case SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE: {
            if (!isFileOk(channel->getFileName(),
                          channel->getFileWriteCheckSec())) {
              val1 = -275;
              val2 = -1;
            }

            int n;

            n = val1 * 1000;
            memcpy(tmp_value, &n, 4);

            n = val2 * 1000;
            memcpy(&tmp_value[4], &n, 4);
          } break;
          case SUPLA_CHANNELFNC_HUMIDITY: {
            if (!isFileOk(channel->getFileName(),
                          channel->getFileWriteCheckSec())) {
              val1 = -1;
            }

            int n;

            n = 0 * 1000;
            memcpy(tmp_value, &n, 4);

            n = val1 * 1000;
            memcpy(&tmp_value[4], &n, 4);

            val3 = val2;

          } break;
          case SUPLA_CHANNELFNC_THERMOSTAT:
          case SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS: {
            int mode;
            int power;
            int fan;
            double preset;
            double measured;
            read_result =
                file_read_ac_data(channel->getFileName().c_str(), &mode, &power,
                                  &preset, &measured, &fan);
            (void)mode;
            (void)fan;

            if (read_result == 1) {
              channel->updateThermostatState(true, power == 1, true, measured,
                                             true, preset);
              channel->getValue(tmp_value);
            }
          } break;
          case SUPLA_CHANNELFNC_HVAC_THERMOSTAT: {
            int mode;
            int power;
            int fan;
            double preset;
            double measured;

            read_result =
                file_read_ac_data(channel->getFileName().c_str(), &mode, &power,
                                  &preset, &measured, &fan);
            (void)mode;
            (void)measured;
            (void)fan;

            if (read_result == 1) {
              channel->updateHvacState(true, power == 1, true, preset);
              channel->getValue(tmp_value);
            }
          } break;
        }

        channel->transformIncomingState(tmp_value);

        if (channel->isBatteryPowered() && val3 != 0)
          channel->setBatteryLevel(val3);
        if (read_result == 1) channel->setValue(tmp_value);

        if (read_result == 0 && log_err == 1)
          supla_log(LOG_ERR, "Can't read file %s",
                    channel->getFileName().c_str());

      } catch (std::exception &exception) {
        supla_log(LOG_ERR, exception.what());
      }
    }
  }
  return read_result;
}

char channelio_gpio_in(TDeviceChannel *channel, char port12) {
  if (channel->type == SUPLA_CHANNELTYPE_SENSORNO ||
      channel->type == SUPLA_CHANNELTYPE_SENSORNC ||
      ((channel->type == SUPLA_CHANNELTYPE_RELAYHFD4 ||
        channel->type == SUPLA_CHANNELTYPE_RELAYG5LA1A ||
        channel->type == SUPLA_CHANNELTYPE_RELAYG5LA1A) &&
       port12 == 2 && channel->bistable == 1))
    return 1;

  return 0;
}

void mqtt_subscribe_callback(void **state,
                             struct mqtt_response_publish *publish) {
  std::string topic =
      std::string((char *)publish->topic_name, publish->topic_name_size);

  std::string message = std::string((char *)publish->application_message,
                                    publish->application_message_size);

  std::vector<client_device_channel *> chnls;
  channels->get_channels_for_topic(topic, &chnls);

  if (chnls.size() > 0) {
    for (client_device_channel *channel : chnls) {
      if (handle_subscribed_message(channel, topic, message,
                                    channels->on_valuechanged,
                                    channels->on_valuechanged_user_data)) {
        channelio_raise_execute_command(channel);
      }
    }
  }
}

char channelio_init(void) {
  channels = new client_device_channels();
  return 1;
}

void channelio_free(void) {
  if (channels) {
    delete channels;
  }

  mqtt_client_free();
  sthread_twf(thread_w1);
}

char channelio_allowed_type(int type) {
  switch (type) {
    case SUPLA_CHANNELTYPE_RELAYHFD4:
    case SUPLA_CHANNELTYPE_SENSORNO:
    case SUPLA_CHANNELTYPE_SENSORNC:
    case SUPLA_CHANNELTYPE_THERMOMETERDS18B20:
    case SUPLA_CHANNELTYPE_RELAYG5LA1A:
    case SUPLA_CHANNELTYPE_2XRELAYG5LA1A:
    case SUPLA_CHANNELTYPE_DHT11:
    case SUPLA_CHANNELTYPE_DHT22:
    case SUPLA_CHANNELTYPE_AM2302:
    case SUPLA_CHANNELTYPE_DIMMER:
    case SUPLA_CHANNELTYPE_RGBLEDCONTROLLER:
    case SUPLA_CHANNELTYPE_DIMMERANDRGBLED:
    case SUPLA_CHANNELTYPE_HUMIDITYSENSOR:
    case SUPLA_CHANNELTYPE_RELAY:
      return 1;
  }

  return 0;
}

void channelio_channel_init(void) {
  if (channels->getInitialized()) return;

  int a;

  client_device_channel *channel;
  for (a = 0; a < channels->getChannelCount(); a++) {
    channel = channels->getChannel(a);

    if (channel->getFileName().length() == 0) continue;
    if (channelio_read_from_file(channel, 1)) {
      channelio_raise_valuechanged(channel);
    };
  }

  channels->setInitialized(true);
#ifndef __SINGLE_THREAD
  thread_w1 = sthread_simple_run(channelio_w1_execute, NULL, 0);
#endif
}

int channelio_channel_count(void) { return channels->getChannelCount(); }

void channelio_set_file_write_check(unsigned char number, int value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setFileWriteCheckSec(value);
}

void channelio_set_id_template(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setIdTemplate(value);
}

void channelio_set_id_value(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setIdValue(value);
}

void channelio_set_payload_on(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setPayloadOn(value);
}
void channelio_set_payload_off(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setPayloadOff(value);
}
void channelio_set_interval(unsigned char number, int interval) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setIntervalSec(interval);
}

void channelio_set_payload_value(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setPayloadValue(value);
}

void channelio_set_filename(unsigned char number, const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setFileName(value);
}

void channelio_set_execute_on(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setExecuteOn(value);
}

void channelio_set_execute_off(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setExecuteOff(value);
}

void channelio_set_execute(unsigned char number, const char *value) {
  if (channels == NULL) return;
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setExecute(value);
}

void channelio_set_function(unsigned char number, int function) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setFunction(function);
}

void channelio_set_type(unsigned char number, int type) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setType(type);
}
int channelio_get_type(unsigned char number) {
  if (channels == NULL) return 0;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) return channel->getType();

  return 0;
}
int channelio_get_function(unsigned char number) {
  if (channels == NULL) return 0;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) return channel->getFunction();

  return 0;
}
void channelio_set_toggle(unsigned char number, int toggle) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setToggleSec(toggle);
}

void channelio_set_mqtt_topic_in(unsigned char number, const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setStateTopic(value);
}
void channelio_set_mqtt_topic_out(unsigned char number, const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setCommandTopic(value);
}
void channelio_set_mqtt_temperature_topic_in(unsigned char number,
                                             const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setTemperatureTopic(value);
}
void channelio_set_mqtt_humidity_topic_in(unsigned char number,
                                          const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setHumidityTopic(value);
}
void channelio_set_mqtt_voltage_topic_in(unsigned char number,
                                         const char *value) {
  channelio_set_mqtt_voltage_topic_phase_in(number, 0, value);
}
void channelio_set_mqtt_voltage_topic_phase_in(unsigned char number,
                                               unsigned char phase,
                                               const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setVoltageTopic(phase, value);
}
void channelio_set_mqtt_current_topic_in(unsigned char number,
                                         const char *value) {
  channelio_set_mqtt_current_topic_phase_in(number, 0, value);
}
void channelio_set_mqtt_current_topic_phase_in(unsigned char number,
                                               unsigned char phase,
                                               const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setCurrentTopic(phase, value);
}
void channelio_set_mqtt_power_topic_in(unsigned char number,
                                       const char *value) {
  channelio_set_mqtt_power_topic_phase_in(number, 0, value);
}
void channelio_set_mqtt_power_topic_phase_in(unsigned char number,
                                             unsigned char phase,
                                             const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setPowerTopic(phase, value);
}
void channelio_set_mqtt_energy_topic_in(unsigned char number,
                                        const char *value) {
  channelio_set_mqtt_energy_topic_phase_in(number, 0, value);
}
void channelio_set_mqtt_energy_topic_phase_in(unsigned char number,
                                              unsigned char phase,
                                              const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setEnergyTopic(phase, value);
}
void channelio_set_mqtt_frequency_topic_in(unsigned char number,
                                           const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setFrequencyTopic(value);
}
void channelio_set_mqtt_reactive_power_topic_phase_in(unsigned char number,
                                                      unsigned char phase,
                                                      const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setReactivePowerTopic(phase, value);
}
void channelio_set_mqtt_apparent_power_topic_phase_in(unsigned char number,
                                                      unsigned char phase,
                                                      const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setApparentPowerTopic(phase, value);
}
void channelio_set_mqtt_power_factor_topic_phase_in(unsigned char number,
                                                    unsigned char phase,
                                                    const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setPowerFactorTopic(phase, value);
}
void channelio_set_mqtt_phase_angle_topic_phase_in(unsigned char number,
                                                   unsigned char phase,
                                                   const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setPhaseAngleTopic(phase, value);
}
void channelio_set_mqtt_returned_energy_topic_phase_in(unsigned char number,
                                                       unsigned char phase,
                                                       const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setReturnedEnergyTopic(phase, value);
}
void channelio_set_mqtt_inductive_energy_topic_phase_in(unsigned char number,
                                                        unsigned char phase,
                                                        const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setInductiveEnergyTopic(phase, value);
}
void channelio_set_mqtt_capacitive_energy_topic_phase_in(unsigned char number,
                                                         unsigned char phase,
                                                         const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->setCapacitiveEnergyTopic(phase, value);
}
void channelio_set_mqtt_brightness_topic_in(unsigned char number,
                                            const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setBrightnessTopic(value);
}
void channelio_set_mqtt_color_brightness_topic_in(unsigned char number,
                                                  const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setColorBrightnessTopic(value);
}
void channelio_set_mqtt_color_topic_in(unsigned char number,
                                       const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setColorTopic(value);
}
void channelio_set_mqtt_measured_temperature_topic_in(unsigned char number,
                                                      const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setMeasuredTemperatureTopic(value);
}
void channelio_set_mqtt_preset_temperature_topic_in(unsigned char number,
                                                    const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setPresetTemperatureTopic(value);
}
void channelio_set_mqtt_position_topic_out(unsigned char number,
                                           const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setPositionCommandTopic(value);
}
void channelio_set_mqtt_preset_temperature_topic_out(unsigned char number,
                                                     const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setPresetTemperatureCommandTopic(value);
}
void channelio_set_mqtt_retain(unsigned char number, unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setRetain(value);
}

void channelio_set_invert_state(unsigned char number, unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setInvertState(value);
}

void channelio_set_esphome_cover(unsigned char number, unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setEsphomeCover(value);
}

void channelio_set_esphome_rgbw(unsigned char number, unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setEsphomeRgbw(value);
}

void channelio_set_hvac_subfunction(unsigned char number, const char *value) {
  if (channels == NULL || value == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) {
    channel->setHvacSubfunctionCool(strcasecmp(value, "cool") == 0);
  }
}

void channelio_set_hvac_main_thermometer_channel(unsigned char number,
                                                 unsigned char channelNo) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) {
    channel->setHvacMainThermometerChannelNo(channelNo);
  }
}

void channelio_set_general_value_divider(unsigned char number,
                                         _supla_int_t value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralValueDivider(value);
}

void channelio_set_general_value_multiplier(unsigned char number,
                                            _supla_int_t value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralValueMultiplier(value);
}

void channelio_set_general_value_added(unsigned char number,
                                       _supla_int64_t value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralValueAdded(value);
}

void channelio_set_general_value_precision(unsigned char number,
                                           unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralValuePrecision(value);
}

void channelio_set_general_unit_before_value(unsigned char number,
                                             const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralUnitBeforeValue(value);
}

void channelio_set_general_unit_after_value(unsigned char number,
                                            const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralUnitAfterValue(value);
}

void channelio_set_general_no_space_before_value(unsigned char number,
                                                 unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralNoSpaceBeforeValue(value != 0);
}

void channelio_set_general_no_space_after_value(unsigned char number,
                                                unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralNoSpaceAfterValue(value != 0);
}

void channelio_set_general_keep_history(unsigned char number,
                                        unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralKeepHistory(value != 0);
}

void channelio_set_general_chart_type(unsigned char number,
                                      unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralChartType(value);
}

void channelio_set_general_refresh_interval_ms(unsigned char number,
                                               unsigned short value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setGeneralRefreshIntervalMs(value);
}

void channelio_set_battery_powered(unsigned char number, unsigned char value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setBatteryPowered(value);
}

void channelio_set_mqtt_template_in(unsigned char number, const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setStateTemplate(value);
}
void channelio_set_mqtt_template_out(unsigned char number, const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setCommandTemplate(value);
}
void channelio_set_mqtt_template_on_out(unsigned char number,
                                        const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setCommandTemplateOn(value);
}
void channelio_set_mqtt_template_off_out(unsigned char number,
                                         const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setCommandTemplateOff(value);
}
void channelio_set_mqtt_preset_temperature_template_out(unsigned char number,
                                                        const char *value) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel) channel->setPresetTemperatureCommandTemplate(value);
}

void channelio_w1_iterate(void) {
  int a;
  if (!channels->getInitialized()) return;

  client_device_channel *channel;
  for (a = 0; a < channels->getChannelCount(); a++) {
    channel = channels->getChannel(a);

    if (channel->getFileName().length() > 0) {
      if (channelio_read_from_file(channel, 1)) {
        channelio_raise_valuechanged(channel);
      };
    };

    if (channel->getToggleSec() > 0 && !channel->getToggled()) {
      struct timeval now;
      gettimeofday(&now, NULL);

      if (now.tv_sec - channel->getLastSeconds() >= channel->getToggleSec()) {
        supla_log(LOG_DEBUG, "toggling value for channel: %s",
                  channel->getNumber());
        channel->toggleValue();
        channelio_raise_valuechanged(channel);
      };
    };

    if (channel->getFunction() == SUPLA_CHANNELFNC_HVAC_THERMOSTAT) {
      if (channelio_iterate_hvac_countdown(channel)) {
        continue;
      }

      channelio_apply_current_hvac_weekly_schedule(channel, 1, 1);
    }
  }
}

#ifdef __SINGLE_THREAD
void channelio_iterate(void) {
  if (!channels->getInitialized()) return;
  channelio_w1_iterate();
}
#else
void channelio_w1_execute(void *user_data, void *sthread) {
  while (!sthread_isterminated(sthread)) {
    channelio_w1_iterate();
    usleep(W1_TEMP_MINDELAY_USEC);
  }
}
#endif

void channelio_raise_execute_command(client_device_channel *channel) {
  std::string command = channel->getExecute();
  std::string commandOn = channel->getExecuteOn();
  std::string commandOff = channel->getExecuteOff();

  if (command.length() == 0 && commandOn.length() == 0 &&
      commandOff.length() == 0)
    return;

  if (command.length() > 0) {
    int commandResult = system(command.c_str());
    if (commandResult != 0) {
      supla_log(LOG_WARNING, "%s", command.c_str());
      supla_log(LOG_WARNING, "The command above failed with exist status %d",
                commandResult);
    }
  }

  char value[SUPLA_CHANNELVALUE_SIZE];
  channel->getValue(value);

  if (commandOn.length() > 0 && value[0] == 1) {
    int commandResult = system(commandOn.c_str());
    if (commandResult != 0) {
      supla_log(LOG_WARNING, "%s", commandOn.c_str());
      supla_log(LOG_WARNING, "The command above failed with exist status %d",
                commandResult);
    }
  }

  if (commandOn.length() > 0 && value[0] == 0) {
    int commandResult = system(commandOff.c_str());
    if (commandResult != 0) {
      supla_log(LOG_WARNING, "%s", commandOff.c_str());
      supla_log(LOG_WARNING, "The command above failed with exist status %d",
                commandResult);
    }
  }
}

void channelio_get_value(unsigned char number,
                         char value[SUPLA_CHANNELVALUE_SIZE]) {
  client_device_channel *channel = channels->find_channel(number);
  if (channel) channel->getValue(value);
}

void channelio_raise_mqtt_valuechannged(client_device_channel *channel) {
  if (channel->getCommandTopic().length() == 0 &&
      channel->getPositionCommandTopic().length() == 0 &&
      channel->getPresetTemperatureCommandTopic().length() == 0)
    return;
  publish_mqtt_message_for_channel(channel);
}

void channelio_normalize_hvac_value(client_device_channel *channel,
                                    THVACValue *hvacValue) {
  if (channel == NULL || hvacValue == NULL) return;

  hvacValue->Flags &= ~(SUPLA_HVAC_VALUE_FLAG_HEATING |
                        SUPLA_HVAC_VALUE_FLAG_COOLING |
                        SUPLA_HVAC_VALUE_FLAG_COOL);

  if (channel->getHvacSubfunctionCool()) {
    hvacValue->Flags |= SUPLA_HVAC_VALUE_FLAG_COOL;
  }

  if (hvacValue->Mode == SUPLA_HVAC_MODE_OFF) {
    hvacValue->IsOn = 0;
  } else if (hvacValue->Mode == SUPLA_HVAC_MODE_HEAT ||
             hvacValue->Mode == SUPLA_HVAC_MODE_COOL ||
             hvacValue->Mode == SUPLA_HVAC_MODE_HEAT_COOL) {
    hvacValue->IsOn = 1;
  }
}

char channelio_commit_hvac_value(client_device_channel *channel,
                                 const THVACValue *hvacValue, char publish,
                                 char raise) {
  if (channel == NULL || hvacValue == NULL) return 0;

  THVACValue currentValue;
  if (!channel->getHvac(&currentValue)) {
    memset(&currentValue, 0, sizeof(THVACValue));
  }

  if (memcmp(&currentValue, hvacValue, sizeof(THVACValue)) == 0) {
    return 0;
  }

  char rawValue[SUPLA_CHANNELVALUE_SIZE];
  memset(rawValue, 0, sizeof(rawValue));
  memcpy(rawValue, hvacValue, sizeof(THVACValue));
  channel->setValue(rawValue);

  channelio_raise_execute_command(channel);

  if (publish) {
    channelio_raise_mqtt_valuechannged(channel);

    unsigned char trackingIndex = channelio_tracking_index(channel->getNumber());
    if (trackingIndex > 0) {
      time_t now = time(NULL);
      if (now != (time_t)-1) {
        runtime_weekly_schedule_last_publish[trackingIndex] = now;
      }
    }
  }

  if (raise) {
    channelio_raise_valuechanged(channel);
  }

  return 1;
}

char channelio_iterate_hvac_countdown(client_device_channel *channel) {
  if (channel == NULL) return 0;

  unsigned char trackingIndex = channelio_tracking_index(channel->getNumber());
  if (trackingIndex == 0 || runtime_hvac_countdown_end[trackingIndex] <= 1) {
    return 0;
  }

  time_t now = time(NULL);
  if (now == (time_t)-1) return 0;

  THVACValue currentValue;
  if (!channel->getHvac(&currentValue)) {
    memset(&currentValue, 0, sizeof(THVACValue));
  }

  if (runtime_hvac_countdown_end[trackingIndex] > now) {
    unsigned _supla_int_t remainingSeconds =
        runtime_hvac_countdown_end[trackingIndex] - now;

    if (runtime_hvac_countdown_last_sent[trackingIndex] != remainingSeconds) {
      THVACValue targetValue;
      memset(&targetValue, 0, sizeof(THVACValue));

      if (runtime_hvac_countdown_restore_available[trackingIndex]) {
        memcpy(&targetValue, &runtime_hvac_countdown_restore_value[trackingIndex],
               sizeof(THVACValue));
      } else {
        memcpy(&targetValue, &currentValue, sizeof(THVACValue));
        targetValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_COUNTDOWN_TIMER;
      }

      channelio_raise_hvac_timer_state(channel, remainingSeconds, &targetValue);
      runtime_hvac_countdown_last_sent[trackingIndex] = remainingSeconds;
    }

    return 1;
  }

  THVACValue nextValue;
  memset(&nextValue, 0, sizeof(THVACValue));

  if (runtime_hvac_countdown_restore_available[trackingIndex]) {
    memcpy(&nextValue, &runtime_hvac_countdown_restore_value[trackingIndex],
           sizeof(THVACValue));
  } else {
    memcpy(&nextValue, &currentValue, sizeof(THVACValue));
    nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_COUNTDOWN_TIMER;
  }

  nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_COUNTDOWN_TIMER;
  channelio_normalize_hvac_value(channel, &nextValue);

  channelio_clear_hvac_countdown(channel->getNumber());
  channelio_commit_hvac_value(channel, &nextValue, 1, 1);
  channelio_raise_hvac_timer_state(channel, 0, &nextValue);

  return 1;
}

char channelio_build_hvac_weekly_value(client_device_channel *channel,
                                       THVACValue *hvacValue) {
  if (channel == NULL || hvacValue == NULL) return 0;

  TWeeklyScheduleProgram program;
  if (!channelio_get_current_hvac_program(channel, &program)) {
    return 0;
  }

  hvacValue->Flags &= ~(SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET |
                        SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET |
                        SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE);
  hvacValue->Flags |= SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;

  if (program.Mode == SUPLA_HVAC_MODE_OFF) {
    hvacValue->Mode = SUPLA_HVAC_MODE_OFF;
    hvacValue->IsOn = 0;
    channelio_normalize_hvac_value(channel, hvacValue);
    return 1;
  }

  if (program.Mode != SUPLA_HVAC_MODE_HEAT &&
      program.Mode != SUPLA_HVAC_MODE_COOL &&
      program.Mode != SUPLA_HVAC_MODE_HEAT_COOL) {
    return 0;
  }

  hvacValue->Mode = program.Mode;
  hvacValue->IsOn = 1;

  if (program.Mode != SUPLA_HVAC_MODE_COOL &&
      program.SetpointTemperatureHeat != SUPLA_TEMPERATURE_INVALID_INT16) {
    hvacValue->SetpointTemperatureHeat = program.SetpointTemperatureHeat;
    hvacValue->Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET;
  }

  if (program.Mode != SUPLA_HVAC_MODE_HEAT &&
      program.SetpointTemperatureCool != SUPLA_TEMPERATURE_INVALID_INT16) {
    hvacValue->SetpointTemperatureCool = program.SetpointTemperatureCool;
    hvacValue->Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET;
  }

  channelio_normalize_hvac_value(channel, hvacValue);
  return 1;
}

char channelio_apply_current_hvac_weekly_schedule(
    client_device_channel *channel, char publish, char raise) {
  if (channel == NULL) return 0;

  THVACValue hvacValue;
  if (!channel->getHvac(&hvacValue) ||
      (hvacValue.Flags & SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE) == 0) {
    return 0;
  }

  THVACValue desiredValue = hvacValue;
  if (!channelio_build_hvac_weekly_value(channel, &desiredValue)) {
    return 0;
  }

  unsigned char trackingIndex = channelio_tracking_index(channel->getNumber());
  if (trackingIndex > 0) {
    time_t now = time(NULL);
    if (now != (time_t)-1 &&
        runtime_weekly_schedule_last_publish[trackingIndex] > 0 &&
        now - runtime_weekly_schedule_last_publish[trackingIndex] < 5 &&
        memcmp(&hvacValue, &desiredValue, sizeof(THVACValue)) != 0) {
      return 0;
    }
  }

  return channelio_commit_hvac_value(channel, &desiredValue, publish, raise);
}

char channelio_set_hvac_value(client_device_channel *channel,
                              char value[SUPLA_CHANNELVALUE_SIZE],
                              unsigned int time_ms) {
  if (channel == NULL || value == NULL) return 0;

  THVACValue currentValue;
  if (!channel->getHvac(&currentValue)) {
    memset(&currentValue, 0, sizeof(THVACValue));
  }

  THVACValue requestValue;
  memset(&requestValue, 0, sizeof(THVACValue));
  memcpy(&requestValue, value, sizeof(THVACValue));

  THVACValue nextValue = currentValue;
  bool hadWeeklySchedule =
      (currentValue.Flags & SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE) != 0;
  bool heatSetpointChanged =
      (requestValue.Flags & SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET) != 0;
  bool coolSetpointChanged =
      (requestValue.Flags & SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET) != 0;
  bool startCountdown = time_ms > 0;
  unsigned char trackingIndex = channelio_tracking_index(channel->getNumber());

  if (!startCountdown && channelio_is_hvac_countdown_active(channel->getNumber())) {
    channelio_clear_hvac_countdown(channel->getNumber());
  }

  if (requestValue.Mode == SUPLA_HVAC_MODE_CMD_WEEKLY_SCHEDULE) {
    if (!channelio_build_hvac_weekly_value(channel, &nextValue)) {
      nextValue.Flags |= SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;
      nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE;
      nextValue.Mode = channelio_get_hvac_default_mode(channel);
      nextValue.IsOn = 1;
      channelio_normalize_hvac_value(channel, &nextValue);
    }
  } else {
    if ((heatSetpointChanged || coolSetpointChanged) && hadWeeklySchedule) {
      nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;
      nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE;

      if (nextValue.Mode == SUPLA_HVAC_MODE_OFF ||
          nextValue.Mode == SUPLA_HVAC_MODE_NOT_SET) {
        nextValue.Mode = channelio_get_hvac_default_mode(channel);
        nextValue.IsOn = 1;
      }
    }

    switch (requestValue.Mode) {
      case SUPLA_HVAC_MODE_OFF:
        nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;
        nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE;
        nextValue.Mode = SUPLA_HVAC_MODE_OFF;
        nextValue.IsOn = 0;
        break;
      case SUPLA_HVAC_MODE_HEAT:
      case SUPLA_HVAC_MODE_COOL:
      case SUPLA_HVAC_MODE_HEAT_COOL:
        nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;
        nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE;
        nextValue.Mode = requestValue.Mode;
        nextValue.IsOn = 1;
        break;
      case SUPLA_HVAC_MODE_CMD_TURN_ON:
        if (hadWeeklySchedule) {
          channelio_build_hvac_weekly_value(channel, &nextValue);
        } else {
          if (nextValue.Mode == SUPLA_HVAC_MODE_OFF ||
              nextValue.Mode == SUPLA_HVAC_MODE_NOT_SET) {
            nextValue.Mode = channelio_get_hvac_default_mode(channel);
          }
          nextValue.IsOn = 1;
        }
        break;
      case SUPLA_HVAC_MODE_CMD_SWITCH_TO_MANUAL:
        nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;
        nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE;

        if (nextValue.Mode == SUPLA_HVAC_MODE_OFF ||
            nextValue.Mode == SUPLA_HVAC_MODE_NOT_SET) {
          nextValue.Mode = channelio_get_hvac_default_mode(channel);
          nextValue.IsOn = 1;
        }
        break;
      default:
        break;
    }

    if (heatSetpointChanged) {
      nextValue.SetpointTemperatureHeat = requestValue.SetpointTemperatureHeat;
      nextValue.Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET;
    }

    if (coolSetpointChanged) {
      nextValue.SetpointTemperatureCool = requestValue.SetpointTemperatureCool;
      nextValue.Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET;
    }

    channelio_normalize_hvac_value(channel, &nextValue);
  }

  if (startCountdown && trackingIndex > 0) {
    time_t now = time(NULL);
    if (now == (time_t)-1) {
      startCountdown = false;
    } else {
      // For HVAC channels SUPLA uses DurationSec in the same protocol field
      // that older channel types use as DurationMS.
      unsigned int durationSeconds = time_ms;
      if (durationSeconds == 0) {
        durationSeconds = 1;
      }

      if (!runtime_hvac_countdown_restore_available[trackingIndex]) {
        memcpy(&runtime_hvac_countdown_restore_value[trackingIndex], &currentValue,
               sizeof(THVACValue));
        runtime_hvac_countdown_restore_value[trackingIndex].Flags &=
            ~SUPLA_HVAC_VALUE_FLAG_COUNTDOWN_TIMER;
        runtime_hvac_countdown_restore_available[trackingIndex] = 1;
      }

      runtime_hvac_countdown_end[trackingIndex] = now + durationSeconds;
      runtime_hvac_countdown_last_sent[trackingIndex] = 0;

      nextValue.Flags |= SUPLA_HVAC_VALUE_FLAG_COUNTDOWN_TIMER;
      nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE;
      nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_WEEKLY_SCHEDULE_TEMPORAL_OVERRIDE;
    }
  } else {
    nextValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_COUNTDOWN_TIMER;
  }

  channelio_commit_hvac_value(channel, &nextValue, 1, 1);

  if (startCountdown && trackingIndex > 0 &&
      runtime_hvac_countdown_end[trackingIndex] > 1) {
    time_t now = time(NULL);
    unsigned _supla_int_t remainingSeconds = 0;
    if (now != (time_t)-1 &&
        runtime_hvac_countdown_end[trackingIndex] > now) {
      remainingSeconds = runtime_hvac_countdown_end[trackingIndex] - now;
    }

    channelio_raise_hvac_timer_state(
        channel, remainingSeconds,
        &runtime_hvac_countdown_restore_value[trackingIndex]);
    runtime_hvac_countdown_last_sent[trackingIndex] = remainingSeconds;
  } else {
    channelio_raise_hvac_timer_state(channel, 0, &nextValue);
  }

  return 1;
}

char channelio_set_value(unsigned char number,
                         char value[SUPLA_CHANNELVALUE_SIZE],
                         unsigned int time_ms) {
  client_device_channel *channel = channels->find_channel(number);

  if (channel) {
    if (channel->getFunction() == SUPLA_CHANNELFNC_HVAC_THERMOSTAT) {
      return channelio_set_hvac_value(channel, value, time_ms);
    }

    char previousValue[SUPLA_CHANNELVALUE_SIZE];
    channel->getValue(previousValue);

    if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER ||
        channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING ||
        channel->getFunction() == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
      int color = 0;
      char colorBrightness = 0;
      char brightness = 0;
      char onOff = 0;
      char requestedOnOff = value[5];
      int rememberedColor = 0;
      char rememberedColorBrightness = 0;
      char rememberedBrightness = 0;
      int previousColor = 0;
      char previousColorBrightness = 0;
      char previousBrightness = 0;
      char previousOnOff = 0;

      channel->getRememberedRGBW(&rememberedColor, &rememberedColorBrightness,
                                 &rememberedBrightness);
      channel->getRGBW(&previousColor, &previousColorBrightness,
                       &previousBrightness, &previousOnOff);

      if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER ||
          channel->getFunction() == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
        brightness = value[0];
        if (brightness < 0 || brightness > 100) brightness = 0;
      }

      if (channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING ||
          channel->getFunction() == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
        colorBrightness = value[1];
        if (colorBrightness < 0 || colorBrightness > 100) colorBrightness = 0;

        color = ((value[4] & 0xFF) << 16) | ((value[3] & 0xFF) << 8) |
                (value[2] & 0xFF);
      }

      if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER) {
        bool wasOff = previousBrightness == 0;

        if (brightness == 0) {
          brightness = 0;
          onOff = 0;
        } else {
          if ((requestedOnOff & RGBW_BRIGHTNESS_ONOFF) != 0 && brightness == 100 &&
              wasOff && rememberedBrightness > 0 && rememberedBrightness < 100) {
            brightness = rememberedBrightness;
          }

          onOff = RGBW_BRIGHTNESS_ONOFF;
        }
      } else if (channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING) {
        bool wasOff = previousColorBrightness == 0;

        if (colorBrightness == 0) {
          colorBrightness = 0;
          onOff = 0;
        } else {
          if ((requestedOnOff & RGBW_COLOR_ONOFF) != 0 &&
              colorBrightness == 100 && wasOff &&
              rememberedColorBrightness > 0 &&
              rememberedColorBrightness < 100) {
            colorBrightness = rememberedColorBrightness;
          }

          if (color == 0 && rememberedColor != 0) {
            color = rememberedColor;
          }

          onOff = RGBW_COLOR_ONOFF;
        }
      } else {
        bool wasOff = previousBrightness == 0 && previousColorBrightness == 0;
        bool requestBrightnessOnly =
            (requestedOnOff & RGBW_BRIGHTNESS_ONOFF) != 0 &&
            (requestedOnOff & RGBW_COLOR_ONOFF) == 0;
        bool requestColorOnly =
            (requestedOnOff & RGBW_COLOR_ONOFF) != 0 &&
            (requestedOnOff & RGBW_BRIGHTNESS_ONOFF) == 0;

        // In DIMMERANDRGB the SUPLA app can operate on the white slider
        // or on the RGB part separately. Respect the requested flag set so
        // controlling RGB does not keep the white channel active and vice versa.
        if (requestBrightnessOnly) {
          colorBrightness = 0;
        } else if (requestColorOnly) {
          brightness = 0;
        }

        if (brightness == 0 && colorBrightness == 0) {
          brightness = 0;
          colorBrightness = 0;
          onOff = 0;
        } else {
          if ((requestedOnOff & RGBW_BRIGHTNESS_ONOFF) != 0 && brightness == 100 &&
              wasOff && rememberedBrightness > 0 && rememberedBrightness < 100) {
            brightness = rememberedBrightness;
          }

          if ((requestedOnOff & RGBW_COLOR_ONOFF) != 0 &&
              colorBrightness == 100 && wasOff &&
              rememberedColorBrightness > 0 &&
              rememberedColorBrightness < 100) {
            colorBrightness = rememberedColorBrightness;
          }

          if (brightness > 0) {
            onOff |= RGBW_BRIGHTNESS_ONOFF;
          }

          if (colorBrightness > 0) {
            onOff |= RGBW_COLOR_ONOFF;
          }

          if (color == 0 && rememberedColor != 0 &&
              (onOff & RGBW_COLOR_ONOFF) != 0) {
            color = rememberedColor;
          }
        }
      }

      channel->setRGBW(color, colorBrightness, brightness, onOff);
    } else {
      channel->setValue(value);
    }

    /* execute command if specified */
    channelio_raise_execute_command(channel);

    /* send value to MQTT if specified */
    channelio_raise_mqtt_valuechannged(channel);

    if (channel->getFunction() ==
        SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER) {
      // For roller shutters, values coming from SUPLA are commands
      // (for example 1/2 or 10..110), not the actual position state.
      // Restore the last known position and wait for the real state to come
      // back from MQTT/file input.
      channel->setValue(previousValue);
      return true;
    }

    /* report value back to SUPLA server */
    channelio_raise_valuechanged(channel);

    return true;
  };

  return false;
}

char channelio_handle_calcfg_request(unsigned char number,
                                     TSD_DeviceCalCfgRequest *request) {
  if (channels == NULL || request == NULL) return SUPLA_RESULTCODE_FALSE;

  client_device_channel *channel = channels->find_channel(number);
  if (channel == NULL) return SUPLA_RESULTCODE_CHANNELNOTFOUND;

  if (channel->getFunction() != SUPLA_CHANNELFNC_THERMOSTAT &&
      channel->getFunction() != SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS) {
    return SUPLA_RESULTCODE_UNSUPORTED;
  }

  switch (request->Command) {
    case SUPLA_THERMOSTAT_CMD_TURNON: {
      if (request->DataSize < 1) return SUPLA_RESULTCODE_FALSE;

      return publish_thermostat_power_message_for_channel(
                 channel, request->Data[0] > 0 ? 1 : 0)
                 ? SUPLA_RESULTCODE_TRUE
                 : SUPLA_RESULTCODE_FALSE;
    }

    case SUPLA_THERMOSTAT_CMD_SET_TEMPERATURE: {
      if (request->DataSize < 2) return SUPLA_RESULTCODE_FALSE;

      unsigned short changedMask =
          ((unsigned char)request->Data[0]) |
          (((unsigned char)request->Data[1]) << 8);
      bool published = false;

      for (unsigned char idx = 0; idx < 10; idx++) {
        if ((changedMask & (1 << idx)) == 0) continue;

        size_t offset = 2 + idx * 2;
        if (offset + 1 >= request->DataSize) continue;

        _supla_int16_t rawTemperature =
            (_supla_int16_t)(((unsigned char)request->Data[offset]) |
                             (((unsigned char)request->Data[offset + 1]) << 8));

        if (publish_thermostat_preset_message_for_channel(
                channel, idx, rawTemperature / 100.0)) {
          published = true;
        }
      }

      return published ? SUPLA_RESULTCODE_TRUE : SUPLA_RESULTCODE_FALSE;
    }
  }

  return SUPLA_RESULTCODE_UNSUPORTED;
}

unsigned char channelio_required_proto_version(void) {
  if (channels == NULL) return 0;

  unsigned char requiredProtoVersion = 0;

  for (int idx = 0; idx < channels->getChannelCount(); idx++) {
    client_device_channel *channel = channels->getChannel(idx);

    if (channel == NULL) continue;

    if (channel->getFunction() ==
        SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT) {
      return 23;
    }

    if (channel->getFunction() == SUPLA_CHANNELFNC_HVAC_THERMOSTAT) {
      requiredProtoVersion = 21;
    }
  }

  return requiredProtoVersion;
}

void channelio_reset_runtime_config_tracking(void) {
  memset(runtime_default_config_received, 0,
         sizeof(runtime_default_config_received));
  memset(runtime_weekly_config_received, 0,
         sizeof(runtime_weekly_config_received));
  memset(initial_default_config_sent, 0, sizeof(initial_default_config_sent));
  memset(initial_weekly_config_sent, 0, sizeof(initial_weekly_config_sent));
  memset(runtime_weekly_schedule, 0, sizeof(runtime_weekly_schedule));
  memset(runtime_weekly_schedule_available, 0,
         sizeof(runtime_weekly_schedule_available));
  memset(runtime_weekly_schedule_last_publish, 0,
         sizeof(runtime_weekly_schedule_last_publish));
  runtime_device_config_received = 0;
  initial_device_config_sent = 0;
}

void channelio_send_initial_configs_if_needed(void *srpc) {
  if (channels == NULL || srpc == NULL) return;
  bool hasHvacChannel = false;

  for (int idx = 0; idx < channels->getChannelCount(); idx++) {
    client_device_channel *channel = channels->getChannel(idx);

    if (channel == NULL) continue;

    unsigned char trackingIndex = channelio_tracking_index(channel->getNumber());
    if (trackingIndex == 0) continue;

    if (channel->getFunction() ==
        SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT) {
      if (!runtime_default_config_received[trackingIndex] &&
          !initial_default_config_sent[trackingIndex] &&
          channelio_send_initial_general_measurement_config(channel, srpc)) {
        initial_default_config_sent[trackingIndex] = 1;
      }
      continue;
    }

    if (channel->getFunction() == SUPLA_CHANNELFNC_HVAC_THERMOSTAT) {
      hasHvacChannel = true;

      if (!runtime_default_config_received[trackingIndex] &&
          !initial_default_config_sent[trackingIndex] &&
          channelio_send_initial_hvac_config(channel, srpc)) {
        initial_default_config_sent[trackingIndex] = 1;
      }

      if (!runtime_weekly_config_received[trackingIndex] &&
          !initial_weekly_config_sent[trackingIndex] &&
          channelio_send_initial_hvac_weekly_schedule_config(channel, srpc)) {
        initial_weekly_config_sent[trackingIndex] = 1;
      }
    }
  }

  if (hasHvacChannel && !runtime_device_config_received &&
      !initial_device_config_sent &&
      channelio_send_initial_device_config_request(srpc)) {
    initial_device_config_sent = 1;
  }
}

char channelio_handle_runtime_channel_config(
    TSDS_SetChannelConfig *request, TSDS_SetChannelConfigResult *result) {
  if (result != NULL) {
    memset(result, 0, sizeof(TSDS_SetChannelConfigResult));
    if (request != NULL) {
      result->ChannelNumber = request->ChannelNumber;
      result->ConfigType = request->ConfigType;
    }
  }

  if (channels == NULL || request == NULL) return SUPLA_CONFIG_RESULT_DATA_ERROR;

  client_device_channel *channel = channels->find_channel(request->ChannelNumber);
  if (channel == NULL) return SUPLA_CONFIG_RESULT_DEVICE_NOT_FOUND;

  unsigned char trackingIndex = channelio_tracking_index(request->ChannelNumber);

  if (channel->getFunction() ==
      SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT) {
    switch (request->ConfigType) {
      case SUPLA_CONFIG_TYPE_DEFAULT: {
        if (request->ConfigSize <
            sizeof(TChannelConfig_GeneralPurposeMeasurement)) {
          return SUPLA_CONFIG_RESULT_DATA_ERROR;
        }

        TChannelConfig_GeneralPurposeMeasurement *config =
            (TChannelConfig_GeneralPurposeMeasurement *)request->Config;

        channel->setGeneralValueDivider(config->ValueDivider);
        channel->setGeneralValueMultiplier(config->ValueMultiplier);
        channel->setGeneralValueAdded(config->ValueAdded);
        channel->setGeneralValuePrecision(config->ValuePrecision);
        channel->setGeneralUnitBeforeValue(config->UnitBeforeValue);
        channel->setGeneralUnitAfterValue(config->UnitAfterValue);
        channel->setGeneralNoSpaceBeforeValue(config->NoSpaceBeforeValue != 0);
        channel->setGeneralNoSpaceAfterValue(config->NoSpaceAfterValue != 0);
        channel->setGeneralKeepHistory(config->KeepHistory != 0);
        channel->setGeneralChartType(config->ChartType);
        channel->setGeneralRefreshIntervalMs(config->RefreshIntervalMs);

        if (trackingIndex > 0) {
          runtime_default_config_received[trackingIndex] = 1;
        }

        channelio_raise_valuechanged(channel);
        supla_log(LOG_DEBUG, "applied runtime GENERAL config for channel %d",
                  channel->getNumber());
        return SUPLA_CONFIG_RESULT_TRUE;
      }
    }

    return SUPLA_CONFIG_RESULT_TYPE_NOT_SUPPORTED;
  }

  if (channel->getFunction() != SUPLA_CHANNELFNC_HVAC_THERMOSTAT) {
    return SUPLA_CONFIG_RESULT_FUNCTION_NOT_SUPPORTED;
  }

  switch (request->ConfigType) {
    case SUPLA_CONFIG_TYPE_DEFAULT: {
      if (request->ConfigSize < sizeof(TChannelConfig_HVAC)) {
        return SUPLA_CONFIG_RESULT_DATA_ERROR;
      }

      TChannelConfig_HVAC *config = (TChannelConfig_HVAC *)request->Config;

      if (config->MainThermometerChannelNo > 0 &&
          config->MainThermometerChannelNo != channel->getNumber()) {
        channel->setHvacMainThermometerChannelNo(
            config->MainThermometerChannelNo);
      } else {
        channel->setHvacMainThermometerChannelNo(0);
      }

      if (config->Subfunction == SUPLA_HVAC_SUBFUNCTION_COOL) {
        channel->setHvacSubfunctionCool(true);
      } else if (config->Subfunction == SUPLA_HVAC_SUBFUNCTION_HEAT) {
        channel->setHvacSubfunctionCool(false);
      }

      if (trackingIndex > 0) {
        runtime_default_config_received[trackingIndex] = 1;
      }

      channelio_apply_current_hvac_weekly_schedule(channel, 1, 1);
      supla_log(LOG_DEBUG, "applied runtime HVAC config for channel %d",
                channel->getNumber());
      return SUPLA_CONFIG_RESULT_TRUE;
    }
    case SUPLA_CONFIG_TYPE_WEEKLY_SCHEDULE:
    case SUPLA_CONFIG_TYPE_ALT_WEEKLY_SCHEDULE:
      if (request->ConfigSize < sizeof(TChannelConfig_WeeklySchedule)) {
        return SUPLA_CONFIG_RESULT_DATA_ERROR;
      }

      if (trackingIndex > 0) {
        runtime_weekly_config_received[trackingIndex] = 1;
        runtime_weekly_schedule_available[trackingIndex] = 1;
        memcpy(&runtime_weekly_schedule[trackingIndex], request->Config,
               sizeof(TChannelConfig_WeeklySchedule));
      }

      channelio_apply_current_hvac_weekly_schedule(channel, 1, 1);
      supla_log(LOG_DEBUG,
                "received runtime HVAC weekly schedule for channel %d",
                channel->getNumber());
      return SUPLA_CONFIG_RESULT_TRUE;
  }

  return SUPLA_CONFIG_RESULT_TYPE_NOT_SUPPORTED;
}

char channelio_handle_runtime_device_config(
    TSDS_SetDeviceConfig *request, TSDS_SetDeviceConfigResult *result) {
  if (result != NULL) {
    memset(result, 0, sizeof(TSDS_SetDeviceConfigResult));
  }

  if (request == NULL) return SUPLA_CONFIG_RESULT_DATA_ERROR;

  runtime_device_config_received = 1;
  supla_log(LOG_DEBUG, "received runtime device config");

  return SUPLA_CONFIG_RESULT_TRUE;
}

void channelio_channels_to_srd_b(unsigned char *channel_count,
                                 TDS_SuplaDeviceChannel_B *chnl) {
  int a;

  *channel_count = channels->getChannelCount();

  for (a = 0; a < *channel_count; a++) {
    client_device_channel *channel = channels->getChannel(a);
    if (channel) {
      chnl[a].Number = channel->getNumber();
      chnl[a].Type = channel->getType();
      chnl[a].Default = channel->getFunction();
      channel->getValue(chnl[a].value);
    }
  }
}

void channelio_channels_to_srd_c(unsigned char *channel_count,
                                 TDS_SuplaDeviceChannel_C *chnl) {
  int a;

  *channel_count = channels->getChannelCount();

  for (a = 0; a < *channel_count; a++) {
    client_device_channel *channel = channels->getChannel(a);
    if (channel) {
      chnl[a].Number = channel->getNumber();
      chnl[a].Type = channel->getType();
      chnl[a].Default = channel->getFunction();

      if (channel->getFunction() == SUPLA_CHANNELFNC_HVAC_THERMOSTAT) {
        chnl[a].Flags |= SUPLA_CHANNEL_FLAG_RUNTIME_CHANNEL_CONFIG_UPDATE |
                         SUPLA_CHANNEL_FLAG_WEEKLY_SCHEDULE |
                         SUPLA_CHANNEL_FLAG_COUNTDOWN_TIMER_SUPPORTED;
      } else if (channel->getFunction() ==
                 SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT) {
        chnl[a].Flags |= SUPLA_CHANNEL_FLAG_RUNTIME_CHANNEL_CONFIG_UPDATE;
      }

      if (channel->isBatteryPowered())
        chnl[a].Flags |= SUPLA_CHANNEL_FLAG_CHANNELSTATE;

      channel->getValue(chnl[a].value);
    }
  }
}

void channelio_get_channel_state(unsigned char number,
                                 TDSC_ChannelState *state) {
  if (channels == NULL) return;

  client_device_channel *channel = channels->find_channel(number);

  if (channel == NULL) return;

  if (channel->isBatteryPowered()) {
    state->Fields |= SUPLA_CHANNELSTATE_FIELD_BATTERYPOWERED |
                     SUPLA_CHANNELSTATE_FIELD_BATTERYLEVEL;
    state->BatteryPowered = true;
    state->BatteryLevel = channel->getBatteryLevel();
  }
}

void channelio_setcalback_on_channel_value_changed(
    _func_channelio_valuechanged on_valuechanged,
    _func_channelio_extendedValueChanged on_extendedValueChanged,
    void *user_data) {
  supla_log(LOG_DEBUG, "setting callbacks for value changing...");

  channels->on_valuechanged = on_valuechanged;
  channels->on_extendedValueChanged = on_extendedValueChanged;
  channels->on_valuechanged_user_data = user_data;

  supla_log(LOG_DEBUG, "callbacks are set");
  // MQTT SETUP

  std::string mqtt_host(scfg_string(CFG_MQTT_SERVER));
  if (mqtt_host.length() == 0) return;

  mqtt_client_free();

  if (on_valuechanged == NULL) {
    supla_log(LOG_DEBUG,
              "callbacks cleared, MQTT subscriber temporarily disabled");
    return;
  }

  vector<std::string> topics;
  channels->getMqttSubscriptionTopics(&topics);

  supla_log(LOG_DEBUG, "initializing MQTT broker connection...");

  mqtt_client_init(std::string(scfg_string(CFG_MQTT_SERVER)),
                   scfg_int(CFG_MQTT_PORT),
                   std::string(scfg_string(CFG_MQTT_USERNAME)),
                   std::string(scfg_string(CFG_MQTT_PASSWORD)),
                   std::string(scfg_string(CFG_MQTT_CLIENT_NAME)), 3, topics,
                   mqtt_subscribe_callback);

  supla_log(LOG_DEBUG, "initialization completed");
}

void tmp_channelio_raise_valuechanged(unsigned char number) {
  client_device_channel *channel = channels->find_channel(number);

  if (channel) {
    channelio_raise_valuechanged(channel);
  }
}

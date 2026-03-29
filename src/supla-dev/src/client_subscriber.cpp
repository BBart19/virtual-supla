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

#include "client_subscriber.h"

#include <cctype>
#include <limits>
#include <sstream>
#include <vector>

#include "channel-io.h"
#include "supla-client-lib/srpc.h"

bool value_exists(jsoncons::json payload, std::string path) {
  try {
    return jsoncons::jsonpointer::contains(payload, path);
  } catch (jsoncons::json_exception& je) {
    return false;
  }
}

std::string trim_copy(const std::string &value) {
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

std::string to_lower_copy(const std::string &value) {
  std::string result = value;
  for (size_t idx = 0; idx < result.length(); idx++) {
    result[idx] = std::tolower((unsigned char)result[idx]);
  }
  return result;
}

std::string json_value_to_string(const jsoncons::json &value) {
  if (value.is_string()) {
    return value.as<std::string>();
  }

  if (value.is_bool()) {
    return value.as<bool>() ? "true" : "false";
  }

  if (value.is_int64()) {
    return std::to_string(value.as<int64_t>());
  }

  if (value.is_uint64()) {
    return std::to_string(value.as<uint64_t>());
  }

  if (value.is_double()) {
    std::ostringstream stream;
    stream << value.as<double>();
    return stream.str();
  }

  return "";
}

bool json_pointer_get_string(const jsoncons::json &payload,
                             const std::string &path, std::string *result) {
  if (result == NULL) return false;

  try {
    const jsoncons::json &value = jsoncons::jsonpointer::get(payload, path);
    *result = json_value_to_string(value);
    return true;
  } catch (jsoncons::json_exception &) {
    return false;
  }
}

bool json_pointer_get_int(const jsoncons::json &payload, const std::string &path,
                          int *result) {
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
        *result = std::stoi(value.as<std::string>());
        return true;
      } catch (std::exception &) {
        return false;
      }
    }
  } catch (jsoncons::json_exception &) {
  }

  return false;
}

bool json_pointer_get_uint64(const jsoncons::json &payload,
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
        std::string text = trim_copy(value.as<std::string>());
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

bool json_pointer_get_double(const jsoncons::json &payload,
                             const std::string &path, double *result) {
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
        std::string text = trim_copy(value.as<std::string>());
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

unsigned _supla_int64_t scale_meter_total_to_counter(double total) {
  if (total < 0) total = 0;

  double scaled = total * 1000.0;
  if (scaled >
      static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max())) {
    scaled =
        static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max());
  }

  return static_cast<unsigned _supla_int64_t>(scaled + 0.5);
}

_supla_int_t scale_price_per_unit(double price) {
  if (price < 0) price = 0;

  double scaled = price * 10000.0;
  if (scaled > std::numeric_limits<_supla_int_t>::max()) {
    scaled = std::numeric_limits<_supla_int_t>::max();
  }

  return static_cast<_supla_int_t>(scaled + 0.5);
}

_supla_int_t scale_total_cost(double cost) {
  if (cost < 0) cost = 0;

  double scaled = cost * 100.0;
  if (scaled > std::numeric_limits<_supla_int_t>::max()) {
    scaled = std::numeric_limits<_supla_int_t>::max();
  }

  return static_cast<_supla_int_t>(scaled + 0.5);
}

unsigned _supla_int64_t scale_energy_total_kwh(double total) {
  if (total < 0) total = 0;

  double scaled = total * 100000.0;
  if (scaled >
      static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max())) {
    scaled =
        static_cast<double>(std::numeric_limits<unsigned _supla_int64_t>::max());
  }

  return static_cast<unsigned _supla_int64_t>(scaled + 0.5);
}

unsigned _supla_int_t scale_energy_total_kwh_short(double total) {
  if (total < 0) total = 0;

  double scaled = total * 100.0;
  if (scaled >
      static_cast<double>(std::numeric_limits<unsigned _supla_int_t>::max())) {
    scaled =
        static_cast<double>(std::numeric_limits<unsigned _supla_int_t>::max());
  }

  return static_cast<unsigned _supla_int_t>(scaled + 0.5);
}

unsigned _supla_int16_t scale_frequency_hz(double value) {
  if (value < 0) value = 0;

  double scaled = value * 100.0;
  if (scaled > std::numeric_limits<unsigned _supla_int16_t>::max()) {
    scaled = std::numeric_limits<unsigned _supla_int16_t>::max();
  }

  return static_cast<unsigned _supla_int16_t>(scaled + 0.5);
}

unsigned _supla_int16_t scale_voltage_v(double value) {
  if (value < 0) value = 0;

  double scaled = value * 100.0;
  if (scaled > std::numeric_limits<unsigned _supla_int16_t>::max()) {
    scaled = std::numeric_limits<unsigned _supla_int16_t>::max();
  }

  return static_cast<unsigned _supla_int16_t>(scaled + 0.5);
}

unsigned _supla_int16_t scale_current_a(double value) {
  if (value < 0) value = 0;

  double scaled = value * 1000.0;
  if (scaled > std::numeric_limits<unsigned _supla_int16_t>::max()) {
    scaled = std::numeric_limits<unsigned _supla_int16_t>::max();
  }

  return static_cast<unsigned _supla_int16_t>(scaled + 0.5);
}

_supla_int_t scale_power_w(double value) {
  double scaled = value * 100.0;

  if (scaled > std::numeric_limits<_supla_int_t>::max()) {
    scaled = std::numeric_limits<_supla_int_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int_t>::min()) {
    scaled = std::numeric_limits<_supla_int_t>::min();
  }

  return static_cast<_supla_int_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

_supla_int16_t scale_power_factor(double value) {
  double scaled = value * 1000.0;

  if (scaled > std::numeric_limits<_supla_int16_t>::max()) {
    scaled = std::numeric_limits<_supla_int16_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int16_t>::min()) {
    scaled = std::numeric_limits<_supla_int16_t>::min();
  }

  return static_cast<_supla_int16_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

_supla_int16_t scale_phase_angle(double value) {
  double scaled = value * 10.0;

  if (scaled > std::numeric_limits<_supla_int16_t>::max()) {
    scaled = std::numeric_limits<_supla_int16_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int16_t>::min()) {
    scaled = std::numeric_limits<_supla_int16_t>::min();
  }

  return static_cast<_supla_int16_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

void set_impulse_counter_default_unit(int function, char unit[9]) {
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

bool parse_impulse_counter_state_payload(
    int function, const std::string &templateValue, bool hasJsonPayload,
    const jsoncons::json &payload,
    TSC_ImpulseCounter_ExtendedValue *impulseCounterValue) {
  if (impulseCounterValue == NULL) return false;

  memset(impulseCounterValue, 0, sizeof(TSC_ImpulseCounter_ExtendedValue));
  set_impulse_counter_default_unit(function, impulseCounterValue->custom_unit);

  bool hasCounter = false;
  bool hasHumanReadableTotal = false;
  unsigned _supla_int64_t counter = 0;
  double total = 0;

  if (hasJsonPayload && payload.is_object()) {
    hasCounter = json_pointer_get_uint64(payload, "/counter", &counter);

    if (!hasCounter) {
      hasHumanReadableTotal =
          json_pointer_get_double(payload, "/total_kwh", &total) ||
          json_pointer_get_double(payload, "/total_m3", &total) ||
          json_pointer_get_double(payload, "/total", &total) ||
          json_pointer_get_double(payload, "/value", &total);
    }

    int impulsesPerUnit = 0;
    if (json_pointer_get_int(payload, "/impulses_per_unit", &impulsesPerUnit) &&
        impulsesPerUnit > 0) {
      impulseCounterValue->impulses_per_unit = impulsesPerUnit;
    }

    double pricePerUnit = 0;
    if (json_pointer_get_double(payload, "/price_per_unit", &pricePerUnit)) {
      impulseCounterValue->price_per_unit = scale_price_per_unit(pricePerUnit);
    }

    double totalCost = 0;
    if (json_pointer_get_double(payload, "/total_cost", &totalCost)) {
      impulseCounterValue->total_cost = scale_total_cost(totalCost);
    }

    std::string currency;
    if (json_pointer_get_string(payload, "/currency", &currency) &&
        currency.length() >= 3) {
      memcpy(impulseCounterValue->currency, currency.c_str(), 3);
    }

    std::string unit;
    if (json_pointer_get_string(payload, "/unit", &unit) &&
        unit.length() > 0) {
      memset(impulseCounterValue->custom_unit, 0,
             sizeof(impulseCounterValue->custom_unit));
      strncpy(impulseCounterValue->custom_unit, unit.c_str(),
              sizeof(impulseCounterValue->custom_unit) - 1);
    }
  }

  if (!hasCounter && !hasHumanReadableTotal) {
    std::string trimmed = trim_copy(templateValue);
    if (trimmed.length() == 0) return false;

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
    counter = scale_meter_total_to_counter(total);
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
    double price =
        impulseCounterValue->price_per_unit / 10000.0;
    impulseCounterValue->total_cost =
        scale_total_cost(calculatedUnits * price);
  }

  return true;
}

bool parse_electricity_meter_state_payload(
    const std::string &templateValue, bool hasJsonPayload,
    const jsoncons::json &payload, TElectricityMeter_Value *channelValue,
    TElectricityMeter_ExtendedValue *electricityMeterValue) {
  if (channelValue == NULL || electricityMeterValue == NULL) return false;

  memset(channelValue, 0, sizeof(TElectricityMeter_Value));
  memset(electricityMeterValue, 0, sizeof(TElectricityMeter_ExtendedValue));

  bool hasAnyData = false;
  bool hasMeasurement = false;
  bool hasTotal = false;
  double totalKwh = 0;

  if (hasJsonPayload && payload.is_object()) {
    hasTotal = json_pointer_get_double(payload, "/total_kwh", &totalKwh) ||
               json_pointer_get_double(payload, "/energy_kwh", &totalKwh) ||
               json_pointer_get_double(payload, "/total_energy_kwh", &totalKwh) ||
               json_pointer_get_double(payload, "/total_forward_active_energy_kwh",
                                       &totalKwh) ||
               json_pointer_get_double(payload, "/value", &totalKwh) ||
               json_pointer_get_double(payload, "/total", &totalKwh);

    if (hasTotal) {
      electricityMeterValue->total_forward_active_energy[0] =
          scale_energy_total_kwh(totalKwh);
      channelValue->total_forward_active_energy =
          scale_energy_total_kwh_short(totalKwh);
      electricityMeterValue->measured_values |= EM_VAR_FORWARD_ACTIVE_ENERGY;
      hasAnyData = true;
    }

    double reverseTotal = 0;
    if (json_pointer_get_double(payload, "/total_reverse_active_energy_kwh",
                                &reverseTotal)) {
      electricityMeterValue->total_reverse_active_energy[0] =
          scale_energy_total_kwh(reverseTotal);
      electricityMeterValue->measured_values |= EM_VAR_REVERSE_ACTIVE_ENERGY;
      hasAnyData = true;
    }

    double reactiveForward = 0;
    if (json_pointer_get_double(payload, "/total_forward_reactive_energy_kvarh",
                                &reactiveForward)) {
      electricityMeterValue->total_forward_reactive_energy[0] =
          scale_energy_total_kwh(reactiveForward);
      electricityMeterValue->measured_values |= EM_VAR_FORWARD_REACTIVE_ENERGY;
      hasAnyData = true;
    }

    double reactiveReverse = 0;
    if (json_pointer_get_double(payload, "/total_reverse_reactive_energy_kvarh",
                                &reactiveReverse)) {
      electricityMeterValue->total_reverse_reactive_energy[0] =
          scale_energy_total_kwh(reactiveReverse);
      electricityMeterValue->measured_values |= EM_VAR_REVERSE_REACTIVE_ENERGY;
      hasAnyData = true;
    }

    double frequency = 0;
    if (json_pointer_get_double(payload, "/frequency_hz", &frequency) ||
        json_pointer_get_double(payload, "/frequency", &frequency)) {
      electricityMeterValue->m[0].freq = scale_frequency_hz(frequency);
      electricityMeterValue->measured_values |= EM_VAR_FREQ;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double voltage = 0;
    if (json_pointer_get_double(payload, "/voltage_v", &voltage) ||
        json_pointer_get_double(payload, "/voltage", &voltage)) {
      electricityMeterValue->m[0].voltage[0] = scale_voltage_v(voltage);
      electricityMeterValue->measured_values |= EM_VAR_VOLTAGE;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double current = 0;
    if (json_pointer_get_double(payload, "/current_a", &current) ||
        json_pointer_get_double(payload, "/current", &current)) {
      electricityMeterValue->m[0].current[0] = scale_current_a(current);
      electricityMeterValue->measured_values |= EM_VAR_CURRENT;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double powerActive = 0;
    if (json_pointer_get_double(payload, "/power_w", &powerActive) ||
        json_pointer_get_double(payload, "/power_active_w", &powerActive) ||
        json_pointer_get_double(payload, "/active_power_w", &powerActive) ||
        json_pointer_get_double(payload, "/power", &powerActive)) {
      electricityMeterValue->m[0].power_active[0] = scale_power_w(powerActive);
      electricityMeterValue->measured_values |= EM_VAR_POWER_ACTIVE;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double powerReactive = 0;
    if (json_pointer_get_double(payload, "/power_reactive_var", &powerReactive) ||
        json_pointer_get_double(payload, "/reactive_power_var", &powerReactive)) {
      electricityMeterValue->m[0].power_reactive[0] =
          scale_power_w(powerReactive);
      electricityMeterValue->measured_values |= EM_VAR_POWER_REACTIVE;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double powerApparent = 0;
    if (json_pointer_get_double(payload, "/power_apparent_va", &powerApparent) ||
        json_pointer_get_double(payload, "/apparent_power_va", &powerApparent)) {
      electricityMeterValue->m[0].power_apparent[0] =
          scale_power_w(powerApparent);
      electricityMeterValue->measured_values |= EM_VAR_POWER_APPARENT;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double powerFactor = 0;
    if (json_pointer_get_double(payload, "/power_factor", &powerFactor)) {
      electricityMeterValue->m[0].power_factor[0] =
          scale_power_factor(powerFactor);
      electricityMeterValue->measured_values |= EM_VAR_POWER_FACTOR;
      hasMeasurement = true;
      hasAnyData = true;
    }

    double phaseAngle = 0;
    if (json_pointer_get_double(payload, "/phase_angle_deg", &phaseAngle) ||
        json_pointer_get_double(payload, "/phase_angle", &phaseAngle)) {
      electricityMeterValue->m[0].phase_angle[0] = scale_phase_angle(phaseAngle);
      electricityMeterValue->measured_values |= EM_VAR_PHASE_ANGLE;
      hasMeasurement = true;
      hasAnyData = true;
    }

    int period = 0;
    if (json_pointer_get_int(payload, "/period_sec", &period) ||
        json_pointer_get_int(payload, "/period", &period)) {
      if (period < 0) period = 0;
      electricityMeterValue->period = period;
    }
  }

  if (!hasAnyData) {
    std::string trimmed = trim_copy(templateValue);
    if (trimmed.length() == 0) return false;

    try {
      size_t pos = 0;
      totalKwh = std::stod(trimmed, &pos);
      if (pos != trimmed.length()) return false;

      electricityMeterValue->total_forward_active_energy[0] =
          scale_energy_total_kwh(totalKwh);
      channelValue->total_forward_active_energy =
          scale_energy_total_kwh_short(totalKwh);
      electricityMeterValue->measured_values |= EM_VAR_FORWARD_ACTIVE_ENERGY;
      hasTotal = true;
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

bool load_current_electricity_meter_state(
    client_device_channel *channel, TElectricityMeter_Value *channelValue,
    TElectricityMeter_ExtendedValue *electricityMeterValue) {
  if (channel == NULL || channelValue == NULL || electricityMeterValue == NULL) {
    return false;
  }

  memset(channelValue, 0, sizeof(TElectricityMeter_Value));
  memset(electricityMeterValue, 0, sizeof(TElectricityMeter_ExtendedValue));

  char value[SUPLA_CHANNELVALUE_SIZE];
  memset(value, 0, sizeof(value));
  channel->getValue(value);
  memcpy(channelValue, value, sizeof(TElectricityMeter_Value));

  TSuplaChannelExtendedValue extendedValue;
  if (channel->getExtendedValue(&extendedValue) &&
      srpc_evtool_v1_extended2emextended(&extendedValue,
                                         electricityMeterValue) == 1) {
    return true;
  }

  if (channelValue->total_forward_active_energy > 0) {
    electricityMeterValue->total_forward_active_energy[0] =
        static_cast<unsigned _supla_int64_t>(
            channelValue->total_forward_active_energy) *
        1000ULL;
    electricityMeterValue->measured_values |= EM_VAR_FORWARD_ACTIVE_ENERGY;
  }

  return true;
}

void finalize_electricity_meter_state(
    client_device_channel *channel, TElectricityMeter_Value *channelValue,
    TElectricityMeter_ExtendedValue *electricityMeterValue) {
  if (channel == NULL || channelValue == NULL || electricityMeterValue == NULL)
    return;

  const _supla_int_t measurementFlags =
      EM_VAR_FREQ | EM_VAR_VOLTAGE | EM_VAR_CURRENT | EM_VAR_POWER_ACTIVE |
      EM_VAR_POWER_REACTIVE | EM_VAR_POWER_APPARENT | EM_VAR_POWER_FACTOR |
      EM_VAR_PHASE_ANGLE;

  electricityMeterValue->m_count =
      (electricityMeterValue->measured_values & measurementFlags) != 0 ? 1 : 0;

  char flags = 0;
  unsigned _supla_int64_t totalForwardEnergy = 0;

  for (unsigned char phase = 0; phase < 3; phase++) {
    bool configured = channel->getVoltageTopic(phase).length() > 0 ||
                      channel->getCurrentTopic(phase).length() > 0 ||
                      channel->getPowerTopic(phase).length() > 0 ||
                      channel->getEnergyTopic(phase).length() > 0 ||
                      channel->getReactivePowerTopic(phase).length() > 0 ||
                      channel->getApparentPowerTopic(phase).length() > 0 ||
                      channel->getPowerFactorTopic(phase).length() > 0 ||
                      channel->getPhaseAngleTopic(phase).length() > 0 ||
                      channel->getReturnedEnergyTopic(phase).length() > 0 ||
                      channel->getInductiveEnergyTopic(phase).length() > 0 ||
                      channel->getCapacitiveEnergyTopic(phase).length() > 0;
    bool hasData =
        electricityMeterValue->total_forward_active_energy[phase] > 0 ||
        electricityMeterValue->total_reverse_active_energy[phase] > 0 ||
        electricityMeterValue->total_forward_reactive_energy[phase] > 0 ||
        electricityMeterValue->total_reverse_reactive_energy[phase] > 0 ||
        electricityMeterValue->m[0].voltage[phase] > 0 ||
        electricityMeterValue->m[0].current[phase] > 0 ||
        electricityMeterValue->m[0].power_active[phase] != 0 ||
        electricityMeterValue->m[0].power_reactive[phase] != 0 ||
        electricityMeterValue->m[0].power_apparent[phase] != 0 ||
        electricityMeterValue->m[0].power_factor[phase] != 0 ||
        electricityMeterValue->m[0].phase_angle[phase] != 0;

    if (configured || hasData) {
      flags |= static_cast<char>(EM_VALUE_FLAG_PHASE1_ON << phase);
    }

    totalForwardEnergy += electricityMeterValue->total_forward_active_energy[phase];
  }

  if (flags == 0) {
    flags = EM_VALUE_FLAG_PHASE1_ON;
  }

  channelValue->flags = flags;

  unsigned _supla_int64_t rawForwardEnergy = (totalForwardEnergy + 500ULL) / 1000ULL;
  if (rawForwardEnergy >
      static_cast<unsigned _supla_int64_t>(
          std::numeric_limits<unsigned _supla_int_t>::max())) {
    rawForwardEnergy =
        static_cast<unsigned _supla_int64_t>(
            std::numeric_limits<unsigned _supla_int_t>::max());
  }

  channelValue->total_forward_active_energy =
      static_cast<unsigned _supla_int_t>(rawForwardEnergy);
}

unsigned char electricity_phase_mask_for_topic(client_device_channel *channel,
                                               const std::string &topic,
                                               char measurement) {
  if (channel == NULL) return 0;

  unsigned char result = 0;

  for (unsigned char phase = 0; phase < 3; phase++) {
    std::string candidate;

    switch (measurement) {
      case 'v':
        candidate = channel->getVoltageTopic(phase);
        break;
      case 'c':
        candidate = channel->getCurrentTopic(phase);
        break;
      case 'p':
        candidate = channel->getPowerTopic(phase);
        break;
      case 'e':
        candidate = channel->getEnergyTopic(phase);
        break;
      case 'q':
        candidate = channel->getReactivePowerTopic(phase);
        break;
      case 'a':
        candidate = channel->getApparentPowerTopic(phase);
        break;
      case 'o':
        candidate = channel->getPowerFactorTopic(phase);
        break;
      case 'g':
        candidate = channel->getPhaseAngleTopic(phase);
        break;
      case 'r':
        candidate = channel->getReturnedEnergyTopic(phase);
        break;
      case 'i':
        candidate = channel->getInductiveEnergyTopic(phase);
        break;
      case 'k':
        candidate = channel->getCapacitiveEnergyTopic(phase);
        break;
      default:
        return 0;
    }

    if (candidate.length() > 0 && candidate.compare(topic) == 0) {
      result |= static_cast<unsigned char>(1U << phase);
    }
  }

  return result;
}

char scale_255_to_100(int value) {
  if (value < 0) value = 0;
  if (value > 255) value = 255;

  return static_cast<char>((value * 100 + 127) / 255);
}

bool parse_int_in_range(const std::string &value, int minValue, int maxValue,
                        int *result) {
  if (result == NULL) return false;

  std::string trimmed = trim_copy(value);
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

bool parse_on_off_payload(const std::string &value, const std::string &payloadOn,
                          const std::string &payloadOff, bool *hasOnOff,
                          char *onOff) {
  if (hasOnOff == NULL || onOff == NULL) return false;

  *hasOnOff = false;

  if (payloadOn.length() > 0 && value.compare(payloadOn) == 0) {
    *hasOnOff = true;
    *onOff = 1;
    return true;
  }

  if (payloadOff.length() > 0 && value.compare(payloadOff) == 0) {
    *hasOnOff = true;
    *onOff = 0;
    return true;
  }

  std::string lowered = to_lower_copy(trim_copy(value));
  if (lowered == "1" || lowered == "on" || lowered == "true") {
    *hasOnOff = true;
    *onOff = 1;
    return true;
  }

  if (lowered == "0" || lowered == "off" || lowered == "false") {
    *hasOnOff = true;
    *onOff = 0;
    return true;
  }

  return false;
}

bool parse_color_payload(const std::string &value, int *color) {
  if (color == NULL) return false;

  std::string trimmed = trim_copy(value);
  if (trimmed.length() == 0) return false;

  std::string normalized;
  for (size_t idx = 0; idx < trimmed.length(); idx++) {
    char chr = trimmed[idx];
    if (chr != '#' && chr != ',' && chr != ';') {
      normalized.push_back(chr);
    } else if (chr == ',' || chr == ';') {
      normalized.push_back(' ');
    }
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

  if (!(stream >> red >> green >> blue)) {
    return false;
  }

  if (red < 0) red = 0;
  if (red > 255) red = 255;
  if (green < 0) green = 0;
  if (green > 255) green = 255;
  if (blue < 0) blue = 0;
  if (blue > 255) blue = 255;

  *color = ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
  return true;
}

bool parse_rgblighting_state_payload(const std::string &value, int *color,
                                     char *colorBrightness, bool *hasColor,
                                     bool *hasColorBrightness, bool *hasOnOff,
                                     char *onOff) {
  if (color == NULL || colorBrightness == NULL || hasColor == NULL ||
      hasColorBrightness == NULL || hasOnOff == NULL || onOff == NULL) {
    return false;
  }

  *hasColor = false;
  *hasColorBrightness = false;
  *hasOnOff = false;

  std::stringstream stream(trim_copy(value));
  std::string token;
  if (!(stream >> token)) return false;

  if (parse_color_payload(token, color)) {
    *hasColor = true;
  } else {
    return false;
  }

  int parsed = 0;
  if (stream >> token) {
    if (parse_int_in_range(token, 0, 100, &parsed)) {
      *colorBrightness = parsed;
      *hasColorBrightness = true;
    } else {
      std::string lowered = to_lower_copy(token);
      if (lowered == "on" || lowered == "off" || lowered == "1" ||
          lowered == "0" || lowered == "true" || lowered == "false") {
        parse_on_off_payload(token, "", "", hasOnOff, onOff);
      }
    }
  }

  if (stream >> token) {
    parse_on_off_payload(token, "", "", hasOnOff, onOff);
  }

  return *hasColor || *hasColorBrightness || *hasOnOff;
}

bool parse_dimmerandrgb_state_payload(const std::string &value, char *brightness,
                                      char *colorBrightness, int *color,
                                      bool *hasBrightness,
                                      bool *hasColorBrightness, bool *hasColor,
                                      bool *hasOnOff, char *onOff) {
  if (brightness == NULL || colorBrightness == NULL || color == NULL ||
      hasBrightness == NULL || hasColorBrightness == NULL ||
      hasColor == NULL || hasOnOff == NULL || onOff == NULL) {
    return false;
  }

  *hasBrightness = false;
  *hasColorBrightness = false;
  *hasColor = false;
  *hasOnOff = false;

  std::string normalized = trim_copy(value);
  for (size_t idx = 0; idx < normalized.length(); idx++) {
    if (normalized[idx] == ',' || normalized[idx] == ';') {
      normalized[idx] = ' ';
    }
  }

  std::stringstream stream(normalized);
  int parsedBrightness = 0;
  int parsedColorBrightness = 0;
  int red = 0;
  int green = 0;
  int blue = 0;
  int parsedOnOff = 0;

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

  *brightness = parsedBrightness;
  *colorBrightness = parsedColorBrightness;
  *color = ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
  *hasBrightness = true;
  *hasColorBrightness = true;
  *hasColor = true;

  if (stream >> parsedOnOff) {
    *onOff = parsedOnOff > 0 ? 1 : 0;
    *hasOnOff = true;
  }

  return true;
}

bool parse_esphome_light_json_state(
    const jsoncons::json &payload, const std::string &payloadOn,
    const std::string &payloadOff, bool useBrightness,
    bool useColorBrightness, bool useColor, char *brightness,
    char *colorBrightness, int *color, bool *hasBrightness,
    bool *hasColorBrightness, bool *hasColor, bool *hasOnOff, char *onOff) {
  if (brightness == NULL || colorBrightness == NULL || color == NULL ||
      hasBrightness == NULL || hasColorBrightness == NULL ||
      hasColor == NULL || hasOnOff == NULL || onOff == NULL) {
    return false;
  }

  std::string stateValue;
  if (json_pointer_get_string(payload, "/state", &stateValue)) {
    parse_on_off_payload(stateValue, payloadOn, payloadOff, hasOnOff, onOff);

    if (*hasOnOff) {
      bool isOn = *onOff > 0;
      *onOff = 0;

      if (isOn) {
        if (useBrightness) {
          *onOff |= RGBW_BRIGHTNESS_ONOFF;
        }

        if (useColorBrightness || useColor) {
          *onOff |= RGBW_COLOR_ONOFF;
        }
      } else {
        if (useBrightness) {
          *brightness = 0;
          *hasBrightness = true;
        }

        if (useColorBrightness) {
          *colorBrightness = 0;
          *hasColorBrightness = true;
        }
      }
    }
  }

  int rawBrightness = 0;
  int rawWhiteValue = 0;
  bool hasRawBrightness = json_pointer_get_int(payload, "/brightness", &rawBrightness);
  bool hasRawWhiteValue = json_pointer_get_int(payload, "/white_value", &rawWhiteValue);

  // For ESPHome RGBW lights:
  // - `/brightness` is the RGB/color brightness
  // - `/white_value` is the white channel level
  // Map those to SUPLA's DIMMERANDRGB sliders when both are available.
  if (useBrightness && useColorBrightness && hasRawWhiteValue) {
    if (!*hasOnOff || *onOff > 0) {
      *brightness = scale_255_to_100(rawWhiteValue);
      *hasBrightness = true;

      if (hasRawBrightness) {
        *colorBrightness = scale_255_to_100(rawBrightness);
        *hasColorBrightness = true;
      }
    }
  } else if (hasRawBrightness) {
    char scaledBrightness = scale_255_to_100(rawBrightness);

    if (!*hasOnOff || *onOff > 0) {
      if (useBrightness) {
        *brightness = scaledBrightness;
        *hasBrightness = true;
      }

      if (useColorBrightness) {
        *colorBrightness = scaledBrightness;
        *hasColorBrightness = true;
      }
    }
  }

  if (useColor) {
    int red = 0;
    int green = 0;
    int blue = 0;

    if (json_pointer_get_int(payload, "/color/r", &red) &&
        json_pointer_get_int(payload, "/color/g", &green) &&
        json_pointer_get_int(payload, "/color/b", &blue)) {
      if (red < 0) red = 0;
      if (red > 255) red = 255;
      if (green < 0) green = 0;
      if (green > 255) green = 255;
      if (blue < 0) blue = 0;
      if (blue > 255) blue = 255;

      *color = ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
      *hasColor = true;
    }
  }

  return *hasBrightness || *hasColorBrightness || *hasColor || *hasOnOff;
}

bool handle_subscribed_message(client_device_channel* channel,
                               std::string topic, std::string message,
                               _func_channelio_valuechanged cb,
                               void* user_data) {
  if (message.length() == 0) return false;

  if (!cb) {
    supla_log(LOG_DEBUG,
              "ignoring message received before registration result");
    return false;
  }

  supla_log(LOG_DEBUG, "handling message %s", message.c_str());

  char value[SUPLA_CHANNELVALUE_SIZE];

  channel->getValue(value);
  int channelNumber = channel->getNumber();

  jsoncons::json payload;
  std::string template_value = message;
  bool hasJsonPayload = false;

  try {
    payload = jsoncons::json::parse(message);
    hasJsonPayload = true;

    if (channel->getPayloadValue().length() > 0) {
      std::string payloadValue = std::string(channel->getPayloadValue());
      if (!json_pointer_get_string(payload, payloadValue, &template_value)) {
        return false;
      }

      if (template_value.length() == 0) return false;
    }

    if (channel->getIdTemplate().length() > 0) {
      std::string idTemplate = channel->getIdTemplate();
      std::string id;
      if (!json_pointer_get_string(payload, idTemplate, &id)) {
        return false;
      }
      std::string idValue = channel->getIdValue();

      if (id.length() == 0 || id.compare(idValue) != 0) {
        return false;
      }
    }

  } catch (jsoncons::ser_error& ser) {
  } catch (jsoncons::jsonpointer::jsonpointer_error& error) {
  }

  if (template_value.length() == 0) return false;

  std::string payloadOn = channel->getPayloadOn();
  std::string payloadOff = channel->getPayloadOff();
  bool isStateTopic = channel->getStateTopic().compare(topic) == 0;
  bool isTemperatureTopic = channel->getTemperatureTopic().compare(topic) == 0;
  bool isHumidityTopic = channel->getHumidityTopic().compare(topic) == 0;
  unsigned char voltagePhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'v');
  unsigned char currentPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'c');
  unsigned char powerPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'p');
  unsigned char energyPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'e');
  unsigned char reactivePowerPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'q');
  unsigned char apparentPowerPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'a');
  unsigned char powerFactorPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'o');
  unsigned char phaseAnglePhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'g');
  unsigned char returnedEnergyPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'r');
  unsigned char inductiveEnergyPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'i');
  unsigned char capacitiveEnergyPhaseMask =
      electricity_phase_mask_for_topic(channel, topic, 'k');
  bool isVoltageTopic = voltagePhaseMask != 0;
  bool isCurrentTopic = currentPhaseMask != 0;
  bool isPowerTopic = powerPhaseMask != 0;
  bool isEnergyTopic = energyPhaseMask != 0;
  bool isReactivePowerTopic = reactivePowerPhaseMask != 0;
  bool isApparentPowerTopic = apparentPowerPhaseMask != 0;
  bool isPowerFactorTopic = powerFactorPhaseMask != 0;
  bool isPhaseAngleTopic = phaseAnglePhaseMask != 0;
  bool isReturnedEnergyTopic = returnedEnergyPhaseMask != 0;
  bool isInductiveEnergyTopic = inductiveEnergyPhaseMask != 0;
  bool isCapacitiveEnergyTopic = capacitiveEnergyPhaseMask != 0;
  bool isFrequencyTopic = channel->getFrequencyTopic().compare(topic) == 0;
  bool isBrightnessTopic = channel->getBrightnessTopic().compare(topic) == 0;
  bool isColorBrightnessTopic =
      channel->getColorBrightnessTopic().compare(topic) == 0;
  bool isColorTopic = channel->getColorTopic().compare(topic) == 0;
  bool isMeasuredTemperatureTopic =
      channel->getMeasuredTemperatureTopic().compare(topic) == 0;
  bool isPresetTemperatureTopic =
      channel->getPresetTemperatureTopic().compare(topic) == 0;

  if (payloadOn.length() == 0) payloadOn = "1";
  if (payloadOff.length() == 0) payloadOff = "0";

  supla_log(LOG_DEBUG, "handling incomming message: %s",
            template_value.c_str());
  try {
    /* raw payload simple value */
    switch (channel->getFunction()) {
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
      case SUPLA_CHANNELFNC_MAILSENSOR:            // ver. >= 8
      {
        bool hasChanged = false;

        if (payloadOn.compare(template_value) == 0) {
          value[0] = 1;
          hasChanged = true;
        } else if (payloadOff.compare(template_value) == 0) {
          value[0] = 0;
          hasChanged = true;
        };

        if (hasChanged) {
          channel->transformIncomingState(value);
          channel->setValue(value);
          channel->setLastSeconds();
          channel->setToggled(false);

          if (cb) cb(channelNumber, value, user_data);
        };

        return hasChanged;
      }
      case SUPLA_CHANNELFNC_DISTANCESENSOR:
      case SUPLA_CHANNELFNC_DEPTHSENSOR:
      case SUPLA_CHANNELFNC_WINDSENSOR:
      case SUPLA_CHANNELFNC_PRESSURESENSOR:
      case SUPLA_CHANNELFNC_RAINSENSOR:
      case SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT:
      case SUPLA_CHANNELFNC_WEIGHTSENSOR: {
        double dbval = std::stod(template_value);
        memcpy(value, &dbval, sizeof(double));
        channel->setValue(value);

        if (cb) cb(channel->getNumber(), value, user_data);

        return true;
      };
      case SUPLA_CHANNELFNC_THERMOMETER: {
        std::string::size_type sz;  // alias of size_t
        double temp = std::stod(template_value, &sz);

        channel->setDouble(temp);
        channel->getValue(value);

        if (cb) cb(channelNumber, value, user_data);

        return true;

      } break;
      case SUPLA_CHANNELFNC_HUMIDITY: {
        double hum = std::stod(template_value);

        channel->setTempHum(-275, hum);
        channel->getValue(value);

        if (cb) cb(channelNumber, value, user_data);

        return true;

      } break;
      case SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE: {
        double temp = 0;
        double hum = 0;
        bool isTemp = false;
        bool isHum = false;

        channel->getTempHum(&temp, &hum, &isTemp, &isHum);

        if (isTemperatureTopic) {
          temp = std::stod(template_value);
          if (!isHum) hum = 0;
        } else if (isHumidityTopic) {
          hum = std::stod(template_value);
          if (!isTemp) temp = 0;
        } else {
          std::string::size_type sz;  // alias of size_t
          temp = std::stod(template_value, &sz);
          hum = std::stod(template_value.substr(sz));
        }

        channel->setTempHum(temp, hum);
        channel->getValue(value);

        if (cb) cb(channelNumber, value, user_data);

        return true;

      } break;
      case SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER: {
        auto temp = atoi(template_value.c_str());
        if (temp < 0) temp = 0;
        if (temp > 100) temp = 100;

        value[0] = temp;

        channel->transformIncomingState(value);
        channel->setValue(value);

        if (cb) cb(channel->getNumber(), value, user_data);
        return true;
      } break;
      case SUPLA_CHANNELFNC_DIMMER:
      case SUPLA_CHANNELFNC_RGBLIGHTING:
      case SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING: {
        int color = 0;
        char colorBrightness = 0;
        char brightness = 0;
        char onOff = 0;
        bool hasColor = false;
        bool hasColorBrightness = false;
        bool hasBrightness = false;
        bool hasOnOff = false;

        channel->getRGBW(&color, &colorBrightness, &brightness, &onOff);

        if (isBrightnessTopic) {
          int parsed = 0;
          if (!parse_int_in_range(template_value, 0, 100, &parsed)) {
            return false;
          }
          brightness = parsed;
          hasBrightness = true;
        } else if (isColorBrightnessTopic) {
          int parsed = 0;
          if (!parse_int_in_range(template_value, 0, 100, &parsed)) {
            return false;
          }
          colorBrightness = parsed;
          hasColorBrightness = true;
        } else if (isColorTopic) {
          if (!parse_color_payload(template_value, &color)) {
            return false;
          }
          hasColor = true;
        } else if (isStateTopic) {
          if (hasJsonPayload && payload.is_object() &&
              channel->getPayloadValue().length() == 0) {
            if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER) {
              parse_esphome_light_json_state(
                  payload, payloadOn, payloadOff, true, false, false,
                  &brightness, &colorBrightness, &color, &hasBrightness,
                  &hasColorBrightness, &hasColor, &hasOnOff, &onOff);
            } else if (channel->getFunction() ==
                       SUPLA_CHANNELFNC_RGBLIGHTING) {
              parse_esphome_light_json_state(
                  payload, payloadOn, payloadOff, false, true, true,
                  &brightness, &colorBrightness, &color, &hasBrightness,
                  &hasColorBrightness, &hasColor, &hasOnOff, &onOff);
            } else if (channel->getFunction() ==
                       SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
              parse_esphome_light_json_state(
                  payload, payloadOn, payloadOff, true, true, true,
                  &brightness, &colorBrightness, &color, &hasBrightness,
                  &hasColorBrightness, &hasColor, &hasOnOff, &onOff);
            }
          } else {
            parse_on_off_payload(template_value, payloadOn, payloadOff,
                                 &hasOnOff, &onOff);

            if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER) {
              int parsed = 0;
              if (!hasOnOff &&
                  parse_int_in_range(template_value, 0, 100, &parsed)) {
                brightness = parsed;
                hasBrightness = true;
              }
            } else if (channel->getFunction() ==
                       SUPLA_CHANNELFNC_RGBLIGHTING) {
              if (!hasOnOff) {
                parse_rgblighting_state_payload(
                    template_value, &color, &colorBrightness, &hasColor,
                    &hasColorBrightness, &hasOnOff, &onOff);
              }
            } else if (channel->getFunction() ==
                       SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
              if (!hasOnOff) {
                parse_dimmerandrgb_state_payload(
                    template_value, &brightness, &colorBrightness, &color,
                    &hasBrightness, &hasColorBrightness, &hasColor, &hasOnOff,
                    &onOff);
              }
            }
          }
        }

        if (!hasColor && !hasColorBrightness && !hasBrightness && !hasOnOff) {
          return false;
        }

        if (!hasOnOff) {
          if ((channel->getFunction() == SUPLA_CHANNELFNC_DIMMER ||
               channel->getFunction() == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) &&
              hasBrightness) {
            onOff = brightness > 0 ? 1 : onOff;
            if (brightness == 0 &&
                channel->getFunction() != SUPLA_CHANNELFNC_RGBLIGHTING &&
                (!hasColorBrightness || colorBrightness == 0)) {
              onOff = 0;
            }
          }

          if ((channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING ||
               channel->getFunction() == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) &&
              (hasColor || hasColorBrightness)) {
            if (colorBrightness > 0 || hasColor) {
              onOff = 1;
            } else if ((!hasBrightness || brightness == 0) &&
                       colorBrightness == 0) {
              onOff = 0;
            }
          }
        }

        if (hasOnOff) {
          bool isOn = onOff > 0;

          if (!isOn && isStateTopic) {
            if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER ||
                channel->getFunction() ==
                    SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
              brightness = 0;
              hasBrightness = true;
            }

            if (channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING ||
                channel->getFunction() ==
                    SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
              colorBrightness = 0;
              hasColorBrightness = true;
            }
          }

          if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMER) {
            onOff = isOn ? RGBW_BRIGHTNESS_ONOFF : 0;
          } else if (channel->getFunction() == SUPLA_CHANNELFNC_RGBLIGHTING) {
            onOff = isOn ? RGBW_COLOR_ONOFF : 0;
          } else if (channel->getFunction() ==
                     SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
            onOff = isOn ? (RGBW_BRIGHTNESS_ONOFF | RGBW_COLOR_ONOFF) : 0;
          }
        }

        channel->setRGBW(color, colorBrightness, brightness, onOff);
        channel->getValue(value);

        if (cb) cb(channelNumber, value, user_data);
        return true;
      } break;
      case SUPLA_CHANNELFNC_THERMOSTAT:
      case SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS: {
        bool hasOn = false;
        bool on = false;
        bool hasMeasuredTemperature = false;
        double measuredTemperature = 0;
        bool hasPresetTemperature = false;
        double presetTemperature = 0;

        if (isStateTopic) {
          if (payloadOn.compare(template_value) == 0) {
            hasOn = true;
            on = true;
          } else if (payloadOff.compare(template_value) == 0) {
            hasOn = true;
            on = false;
          }
        }

        if (isMeasuredTemperatureTopic) {
          measuredTemperature = std::stod(template_value);
          hasMeasuredTemperature = true;
        }

        if (isPresetTemperatureTopic) {
          presetTemperature = std::stod(template_value);
          hasPresetTemperature = true;
        }

        if (!hasOn && !hasMeasuredTemperature && !hasPresetTemperature) {
          return false;
        }

        channel->updateThermostatState(hasOn, on, hasMeasuredTemperature,
                                       measuredTemperature,
                                       hasPresetTemperature,
                                       presetTemperature);
        channel->getValue(value);

        if (cb) cb(channelNumber, value, user_data);

        return true;
      } break;
      case SUPLA_CHANNELFNC_IC_ELECTRICITY_METER:
      case SUPLA_CHANNELFNC_IC_GAS_METER:
      case SUPLA_CHANNELFNC_IC_WATER_METER: {
        TSC_ImpulseCounter_ExtendedValue impulseCounterValue;
        if (!parse_impulse_counter_state_payload(channel->getFunction(),
                                                 template_value, hasJsonPayload,
                                                 payload,
                                                 &impulseCounterValue)) {
          return false;
        }

        TDS_ImpulseCounter_Value channelValue;
        memset(&channelValue, 0, sizeof(channelValue));
        channelValue.counter = impulseCounterValue.counter;

        memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
        memcpy(value, &channelValue, sizeof(channelValue));
        channel->setValue(value);

        TSuplaChannelExtendedValue extendedValue;
        if (srpc_evtool_v1_icextended2extended(&impulseCounterValue,
                                               &extendedValue) == 0) {
          return false;
        }

        channel->setExtendedValue(&extendedValue);
        channelio_raise_valuechanged(channel);
        return true;
      } break;
      case SUPLA_CHANNELFNC_ELECTRICITY_METER: {
        TElectricityMeter_Value channelValue;
        TElectricityMeter_ExtendedValue electricityMeterValue;

        if (isVoltageTopic || isCurrentTopic || isPowerTopic || isEnergyTopic ||
            isFrequencyTopic || isReactivePowerTopic ||
            isApparentPowerTopic || isPowerFactorTopic || isPhaseAngleTopic ||
            isReturnedEnergyTopic || isInductiveEnergyTopic ||
            isCapacitiveEnergyTopic) {
          double parsed = std::stod(template_value);

          load_current_electricity_meter_state(channel, &channelValue,
                                               &electricityMeterValue);

          if (isVoltageTopic) {
            electricityMeterValue.measured_values |= EM_VAR_VOLTAGE;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((voltagePhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].voltage[phase] =
                    scale_voltage_v(parsed);
              }
            }
          } else if (isCurrentTopic) {
            electricityMeterValue.measured_values |= EM_VAR_CURRENT;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((currentPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].current[phase] =
                    scale_current_a(parsed);
              }
            }
          } else if (isPowerTopic) {
            electricityMeterValue.measured_values |= EM_VAR_POWER_ACTIVE;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((powerPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].power_active[phase] =
                    scale_power_w(parsed);
              }
            }
          } else if (isReactivePowerTopic) {
            electricityMeterValue.measured_values |= EM_VAR_POWER_REACTIVE;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((reactivePowerPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].power_reactive[phase] =
                    scale_power_w(parsed);
              }
            }
          } else if (isApparentPowerTopic) {
            electricityMeterValue.measured_values |= EM_VAR_POWER_APPARENT;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((apparentPowerPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].power_apparent[phase] =
                    scale_power_w(parsed);
              }
            }
          } else if (isPowerFactorTopic) {
            electricityMeterValue.measured_values |= EM_VAR_POWER_FACTOR;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((powerFactorPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].power_factor[phase] =
                    scale_power_factor(parsed);
              }
            }
          } else if (isPhaseAngleTopic) {
            electricityMeterValue.measured_values |= EM_VAR_PHASE_ANGLE;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((phaseAnglePhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.m[0].phase_angle[phase] =
                    scale_phase_angle(parsed);
              }
            }
          } else if (isEnergyTopic) {
            electricityMeterValue.measured_values |= EM_VAR_FORWARD_ACTIVE_ENERGY;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((energyPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.total_forward_active_energy[phase] =
                    scale_energy_total_kwh(parsed);
              }
            }
          } else if (isReturnedEnergyTopic) {
            electricityMeterValue.measured_values |= EM_VAR_REVERSE_ACTIVE_ENERGY;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((returnedEnergyPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.total_reverse_active_energy[phase] =
                    scale_energy_total_kwh(parsed);
              }
            }
          } else if (isInductiveEnergyTopic) {
            electricityMeterValue.measured_values |= EM_VAR_FORWARD_REACTIVE_ENERGY;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((inductiveEnergyPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.total_forward_reactive_energy[phase] =
                    scale_energy_total_kwh(parsed);
              }
            }
          } else if (isCapacitiveEnergyTopic) {
            electricityMeterValue.measured_values |= EM_VAR_REVERSE_REACTIVE_ENERGY;
            for (unsigned char phase = 0; phase < 3; phase++) {
              if ((capacitiveEnergyPhaseMask & (1U << phase)) != 0) {
                electricityMeterValue.total_reverse_reactive_energy[phase] =
                    scale_energy_total_kwh(parsed);
              }
            }
          } else if (isFrequencyTopic) {
            electricityMeterValue.measured_values |= EM_VAR_FREQ;
            electricityMeterValue.m[0].freq = scale_frequency_hz(parsed);
          }

          finalize_electricity_meter_state(channel, &channelValue,
                                           &electricityMeterValue);
        } else if (!parse_electricity_meter_state_payload(
                       template_value, hasJsonPayload, payload, &channelValue,
                       &electricityMeterValue)) {
          return false;
        }

        memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
        memcpy(value, &channelValue, sizeof(channelValue));
        channel->setValue(value);

        TSuplaChannelExtendedValue extendedValue;
        if (srpc_evtool_v1_emextended2extended(&electricityMeterValue,
                                               &extendedValue) == 0) {
          return false;
        }

        channel->setExtendedValue(&extendedValue);
        channelio_raise_valuechanged(channel);
        return true;
      } break;
      case SUPLA_CHANNELFNC_HVAC_THERMOSTAT: {
        bool hasOn = false;
        bool on = false;
        bool hasSetpointTemperature = false;
        double setpointTemperature = 0;

        if (isStateTopic) {
          if (payloadOn.compare(template_value) == 0) {
            hasOn = true;
            on = true;
          } else if (payloadOff.compare(template_value) == 0) {
            hasOn = true;
            on = false;
          }
        }

        if (isPresetTemperatureTopic) {
          setpointTemperature = std::stod(template_value);
          hasSetpointTemperature = true;
        }

        if (!hasOn && !hasSetpointTemperature) {
          return false;
        }

        channel->updateHvacState(hasOn, on, hasSetpointTemperature,
                                 setpointTemperature);
        channel->getValue(value);

        if (cb) cb(channelNumber, value, user_data);

        return true;
      } break;
    };

    return false;

  } catch (jsoncons::json_exception& je) {
    supla_log(LOG_ERR, "error while trying get value from payload [error: %s]",
              je.what());
    return false;
  } catch (std::exception& exception) {
    supla_log(LOG_ERR, "general error %s", exception.what());
    return false;
  }
}

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

#include "client_device.h"

#include <limits>

#include "supla-client-lib/srpc.h"

namespace {

bool is_thermostat_function(int function) {
  return function == SUPLA_CHANNELFNC_THERMOSTAT ||
         function == SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS;
}

bool is_hvac_thermostat_function(int function) {
  return function == SUPLA_CHANNELFNC_HVAC_THERMOSTAT ||
         function == SUPLA_CHANNELFNC_HVAC_THERMOSTAT_DIFFERENTIAL;
}

bool is_hvac_channel_function(int function) {
  return is_hvac_thermostat_function(function) ||
         function == SUPLA_CHANNELFNC_HVAC_FAN;
}

_supla_int16_t thermostat_temperature_to_raw(double temperature) {
  double scaled = temperature * 100.0;

  if (scaled > std::numeric_limits<_supla_int16_t>::max()) {
    scaled = std::numeric_limits<_supla_int16_t>::max();
  } else if (scaled < std::numeric_limits<_supla_int16_t>::min()) {
    scaled = std::numeric_limits<_supla_int16_t>::min();
  }

  return static_cast<_supla_int16_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

void push_topic_if_set(std::vector<std::string> *vect, const std::string &topic) {
  if (vect == NULL || topic.length() == 0) return;

  for (const std::string &existing : *vect) {
    if (existing == topic) return;
  }

  vect->push_back(topic);
}

bool topic_matches(const std::string &candidate, const std::string &topic) {
  return candidate.length() > 0 && candidate.compare(topic) == 0;
}

}  // namespace

client_device_channel::client_device_channel(int number) {
  this->number = number;
  this->type = 0;
  this->function = 0;
  this->intervalSec = 10; /* default 10 seconds*/
  this->toggleSec = 0;

  this->fileName = "";
  this->payloadOn = "";
  this->payloadOff = "";
  this->payloadValue = "";
  this->stateTopic = "";
  this->commandTopic = "";
  this->temperatureTopic = "";
  this->humidityTopic = "";
  this->voltageTopic = "";
  this->voltageTopicL2 = "";
  this->voltageTopicL3 = "";
  this->currentTopic = "";
  this->currentTopicL2 = "";
  this->currentTopicL3 = "";
  this->powerTopic = "";
  this->powerTopicL2 = "";
  this->powerTopicL3 = "";
  this->energyTopic = "";
  this->energyTopicL2 = "";
  this->energyTopicL3 = "";
  this->frequencyTopic = "";
  for (int idx = 0; idx < 3; idx++) {
    this->reactivePowerTopic[idx] = "";
    this->apparentPowerTopic[idx] = "";
    this->powerFactorTopic[idx] = "";
    this->phaseAngleTopic[idx] = "";
    this->returnedEnergyTopic[idx] = "";
    this->inductiveEnergyTopic[idx] = "";
    this->capacitiveEnergyTopic[idx] = "";
  }
  this->brightnessTopic = "";
  this->colorBrightnessTopic = "";
  this->colorTopic = "";
  this->measuredTemperatureTopic = "";
  this->presetTemperatureTopic = "";
  this->presetTemperatureHighTopic = "";
  this->actionTopic = "";
  this->positionCommandTopic = "";
  this->presetTemperatureCommandTopic = "";
  this->presetTemperatureHighCommandTopic = "";
  this->stateTemplate = "";
  this->commandTemplate = "";
  this->commandTemplateOn = "";
  this->commandTemplateOff = "";
  this->presetTemperatureCommandTemplate = "";
  this->presetTemperatureHighCommandTemplate = "";
  this->execute = "";
  this->executeOn = "";
  this->executeOff = "";
  this->retain = true;
  this->online = true;
  this->toggled = true;
  this->fileWriteCheckSeck = 0;
  this->batteryPowered = false;
  this->invertState = false;
  this->esphomeCover = false;
  this->esphomeRgbw = false;
  this->hvacSubfunctionCool = false;
  this->hvacReportAsThermostat = false;
  this->generalNoSpaceBeforeValue = false;
  this->generalNoSpaceAfterValue = false;
  this->generalKeepHistory = false;
  this->hvacConfigInitialized = false;
  this->hvacMainThermometerChannelNo = 0;
  this->generalValuePrecision = 2;
  this->generalChartType =
      SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_LINEAR;
  this->batteryLevel = 0;
  this->generalValueDivider = 1000;
  this->generalValueMultiplier = 1000;
  this->generalValueAdded = 0;
  this->generalRefreshIntervalMs = 0;
  this->generalUnitBeforeValue = "";
  this->generalUnitAfterValue = "";
  this->rememberedRgbwColor = 0xFFFFFF;
  this->rememberedRgbwColorBrightness = 100;
  this->rememberedRgbwBrightness = 100;

  this->last = (struct timeval){0};
  memset(this->value, 0, sizeof(this->value));
  this->extendedValue = NULL;
  memset(&this->hvacConfig, 0, sizeof(this->hvacConfig));

  this->lck = lck_init();
}
client_device_channel::~client_device_channel() {
  if (extendedValue != NULL) {
    delete extendedValue;
    extendedValue = NULL;
  }

  lck_free(lck);
}

bool client_device_channel::getToggled(void) { return this->toggled; }

void client_device_channel::setToggled(bool toggled) {
  this->toggled = toggled;
}

void client_device_channel::setBatteryPowered(bool value) {
  this->batteryPowered = value;
}

void client_device_channel::setBatteryLevel(unsigned char level) {
  this->batteryLevel = level;
}

int client_device_channel::getToggleSec(void) { return this->toggleSec; }

void client_device_channel::setToggleSec(int interval) {
  this->toggleSec = interval;
}

bool client_device_channel::isBatteryPowered(void) {
  return this->batteryPowered;
}

bool client_device_channel::getHvacReportAsThermostat(void) {
  return this->hvacReportAsThermostat;
}

unsigned char client_device_channel::getBatteryLevel(void) {
  return this->batteryLevel;
}

bool client_device_channel::getExtendedValue(TSuplaChannelExtendedValue *ev) {
  if (ev == NULL) {
    return false;
  }

  if (extendedValue == NULL) {
    memset(ev, 0, sizeof(TSuplaChannelExtendedValue));
    return false;
  }

  memcpy(ev, extendedValue, sizeof(TSuplaChannelExtendedValue));
  return true;
}

void client_device_channel::setExtendedValue(TSuplaChannelExtendedValue *ev) {
  if (ev == NULL) {
    if (extendedValue != NULL) {
      delete extendedValue;
      extendedValue = NULL;
    }
  } else {
    if (extendedValue == NULL) {
      extendedValue = new TSuplaChannelExtendedValue;
    }
    memcpy(extendedValue, ev, sizeof(TSuplaChannelExtendedValue));
  }
}

void client_device_channel::toggleValue(void) {
  switch (this->function) {
    case SUPLA_CHANNELFNC_POWERSWITCH:
    case SUPLA_CHANNELFNC_LIGHTSWITCH:
    case SUPLA_CHANNELFNC_STAIRCASETIMER:
    case SUPLA_CHANNELFNC_PUMPSWITCH:
    case SUPLA_CHANNELFNC_HEATORCOLDSOURCESWITCH:
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
      lck_lock(lck);
      value[0] = value[0] > 0 ? 0 : 1;
      toggled = true;
      lck_unlock(lck);
    };
  }
}

bool client_device_channel::isSensorNONC(void) {
  switch (this->function) {
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GATEWAY:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GARAGEDOOR:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_DOOR:
    case SUPLA_CHANNELFNC_NOLIQUIDSENSOR:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_ROLLERSHUTTER:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW:
    case SUPLA_CHANNELFNC_MAILSENSOR:
      return true;
    default:
      return false;
  }
}
void client_device_channel::getValue(char value[SUPLA_CHANNELVALUE_SIZE]) {
  memcpy(value, this->value, SUPLA_CHANNELVALUE_SIZE);
}
void client_device_channel::getDouble(double *result) {
  if (result == NULL) return;

  switch (this->function) {
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GATEWAY:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GARAGEDOOR:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_DOOR:
    case SUPLA_CHANNELFNC_NOLIQUIDSENSOR:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_ROLLERSHUTTER:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW:
    case SUPLA_CHANNELFNC_MAILSENSOR:
      *result = this->value[0] > 0 ? 1 : 0;
      break;
    case SUPLA_CHANNELFNC_THERMOMETER:
    case SUPLA_CHANNELFNC_DISTANCESENSOR:
    case SUPLA_CHANNELFNC_DEPTHSENSOR:
    case SUPLA_CHANNELFNC_WINDSENSOR:
    case SUPLA_CHANNELFNC_PRESSURESENSOR:
    case SUPLA_CHANNELFNC_RAINSENSOR:
    case SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT:
    case SUPLA_CHANNELFNC_WEIGHTSENSOR:
      memcpy(result, this->value, sizeof(double));
      break;
    default:
      *result = 0;
  }
}
void client_device_channel::getTempHum(double *temp, double *humidity,
                                       bool *isTemperature, bool *isHumidity) {
  *isTemperature = false;
  *isHumidity = false;
  if (temp == NULL || humidity == NULL) return;

  double tmp_temp = 0;
  double tmp_humidity = 0;

  switch (this->function) {
    case SUPLA_CHANNELFNC_THERMOMETER: {
      getDouble(&tmp_temp);
      if (tmp_temp > -273 && tmp_temp <= 1000) *isTemperature = true;
    } break;
    case SUPLA_CHANNELFNC_HUMIDITY:
    case SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE: {
      int n;
      char value[SUPLA_CHANNELVALUE_SIZE];

      getValue(value);
      memcpy(&n, value, 4);
      tmp_temp = n / 1000.00;

      memcpy(&n, &value[4], 4);
      tmp_humidity = n / 1000.00;

      if (tmp_temp > -273 && tmp_temp <= 1000) *isTemperature = true;

      if (tmp_humidity >= 0 && tmp_humidity <= 100) *isHumidity = true;
    } break;
  }

  if (*isTemperature) *temp = tmp_temp;
  if (*isHumidity) *humidity = tmp_humidity;
}
bool client_device_channel::getRGBW(int *color, char *color_brightness,
                                    char *brightness, char *on_off) {
  if (color != NULL) *color = 0;

  if (color_brightness != NULL) *color_brightness = 0;

  if (brightness != NULL) *brightness = 0;

  if (on_off != NULL) *on_off = 0;

  bool result = false;

  if (this->function == SUPLA_CHANNELFNC_DIMMER ||
      this->function == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
    if (brightness != NULL) {
      *brightness = this->value[0];

      if (*brightness < 0 || *brightness > 100) *brightness = 0;
    }

    result = true;
  }

  if (this->function == SUPLA_CHANNELFNC_RGBLIGHTING ||
      this->function == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
    if (color_brightness != NULL) {
      *color_brightness = this->value[1];

      if (*color_brightness < 0 || *color_brightness > 100)
        *color_brightness = 0;
    }

    if (color != NULL) {
      *color = 0;

      *color = this->value[4] & 0xFF;
      (*color) <<= 8;

      *color |= this->value[3] & 0xFF;
      (*color) <<= 8;

      (*color) |= this->value[2] & 0xFF;
    }

    result = true;
  }

  if (result && on_off != NULL) {
    *on_off = this->value[5];
  }

  return result;
}
void client_device_channel::getRememberedRGBW(int *color,
                                              char *color_brightness,
                                              char *brightness) {
  if (color != NULL) *color = this->rememberedRgbwColor;
  if (color_brightness != NULL)
    *color_brightness = this->rememberedRgbwColorBrightness;
  if (brightness != NULL) *brightness = this->rememberedRgbwBrightness;
}
char client_device_channel::getChar(void) { return this->value[0]; }
void client_device_channel::setValue(char value[SUPLA_CHANNELVALUE_SIZE]) {
  lck_lock(lck);

  memcpy(this->value, value, SUPLA_CHANNELVALUE_SIZE);

  lck_unlock(lck);
}
void client_device_channel::setDouble(double value) {
  lck_lock(lck);
  memcpy(this->value, &value, sizeof(double));
  lck_unlock(lck);
}
void client_device_channel::setTempHum(double temp, double humidity) {
  switch (this->function) {
    case SUPLA_CHANNELFNC_THERMOMETER: {
      setDouble(temp);
    } break;
    case SUPLA_CHANNELFNC_HUMIDITY:
    case SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE: {
      int n;
      char tmp_value[SUPLA_CHANNELVALUE_SIZE];

      n = temp * 1000;
      memcpy(tmp_value, &n, 4);

      n = humidity * 1000;
      memcpy(&tmp_value[4], &n, 4);

      setValue(tmp_value);
    } break;
  }
}
void client_device_channel::setRGBW(int color, char color_brightness,
                                    char brightness, char on_off) {
  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  memset(tmp_value, 0, sizeof(tmp_value));

  if (this->function == SUPLA_CHANNELFNC_DIMMER ||
      this->function == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
    if (brightness < 0 || brightness > 100) brightness = 0;

    tmp_value[0] = brightness;

    if (brightness > 0) {
      this->rememberedRgbwBrightness = brightness;
    }
  }

  if (this->function == SUPLA_CHANNELFNC_RGBLIGHTING ||
      this->function == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING) {
    if (color_brightness < 0 || color_brightness > 100) color_brightness = 0;

    tmp_value[1] = color_brightness;
    tmp_value[2] = (char)((color & 0x000000FF));
    tmp_value[3] = (char)((color & 0x0000FF00) >> 8);
    tmp_value[4] = (char)((color & 0x00FF0000) >> 16);

    if (color_brightness > 0) {
      this->rememberedRgbwColorBrightness = color_brightness;
    }

    if (color != 0) {
      this->rememberedRgbwColor = color;
    }
  }

  tmp_value[5] = on_off;

  setValue(tmp_value);
}
void client_device_channel::setChar(char chr) { value[0] = chr; }

bool client_device_channel::getThermostat(TThermostat_Value *thermostatValue) {
  if (thermostatValue == NULL || !is_thermostat_function(this->function)) {
    return false;
  }

  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  getValue(tmp_value);
  memcpy(thermostatValue, tmp_value, sizeof(TThermostat_Value));

  return true;
}

bool client_device_channel::getHvac(THVACValue *hvacValue) {
  if (hvacValue == NULL || !is_hvac_channel_function(this->function)) {
    return false;
  }

  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  getValue(tmp_value);
  memcpy(hvacValue, tmp_value, sizeof(THVACValue));

  return true;
}

void client_device_channel::updateThermostatState(bool hasOn, bool on,
                                                  bool hasMeasuredTemperature,
                                                  double measuredTemperature,
                                                  bool hasPresetTemperature,
                                                  double presetTemperature) {
  if (!is_thermostat_function(this->function)) return;

  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  TThermostat_Value thermostatValue;
  TSuplaChannelExtendedValue extendedValue;
  TThermostat_ExtendedValue thermostatExtendedValue;

  memset(tmp_value, 0, sizeof(tmp_value));
  memset(&thermostatValue, 0, sizeof(thermostatValue));
  memset(&extendedValue, 0, sizeof(extendedValue));
  memset(&thermostatExtendedValue, 0, sizeof(thermostatExtendedValue));

  getValue(tmp_value);
  memcpy(&thermostatValue, tmp_value, sizeof(TThermostat_Value));

  if (hasOn) {
    thermostatValue.IsOn = on ? 1 : 0;
  }

  if (hasMeasuredTemperature) {
    thermostatValue.MeasuredTemperature =
        thermostat_temperature_to_raw(measuredTemperature);
  }

  if (hasPresetTemperature) {
    thermostatValue.PresetTemperature =
        thermostat_temperature_to_raw(presetTemperature);
  }

  memset(tmp_value, 0, sizeof(tmp_value));
  memcpy(tmp_value, &thermostatValue, sizeof(TThermostat_Value));
  setValue(tmp_value);

  if (getExtendedValue(&extendedValue)) {
    if (!srpc_evtool_v1_extended2thermostatextended(
            &extendedValue, &thermostatExtendedValue)) {
      memset(&thermostatExtendedValue, 0, sizeof(thermostatExtendedValue));
    }
  }

  thermostatExtendedValue.Fields |= THERMOSTAT_FIELD_Flags |
                                    THERMOSTAT_FIELD_MeasuredTemperatures |
                                    THERMOSTAT_FIELD_PresetTemperatures;
  thermostatExtendedValue.MeasuredTemperature[0] =
      thermostatValue.MeasuredTemperature;
  thermostatExtendedValue.PresetTemperature[0] = thermostatValue.PresetTemperature;

  thermostatExtendedValue.Flags[4] &= ~SUPLA_THERMOSTAT_VALUE_FLAG_ON;
  if (thermostatValue.IsOn) {
    thermostatExtendedValue.Flags[4] |= SUPLA_THERMOSTAT_VALUE_FLAG_ON;
  }

  if (srpc_evtool_v1_thermostatextended2extended(&thermostatExtendedValue,
                                                 &extendedValue)) {
    setExtendedValue(&extendedValue);
  }
}

void client_device_channel::updateHvacState(bool hasOn, bool on,
                                            bool hasSetpointTemperature,
                                            double setpointTemperature) {
  if (!is_hvac_thermostat_function(this->function)) return;

  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  THVACValue hvacValue;

  memset(tmp_value, 0, sizeof(tmp_value));
  memset(&hvacValue, 0, sizeof(hvacValue));

  getValue(tmp_value);
  memcpy(&hvacValue, tmp_value, sizeof(THVACValue));

  hvacValue.Flags &= ~(SUPLA_HVAC_VALUE_FLAG_HEATING |
                       SUPLA_HVAC_VALUE_FLAG_COOLING |
                       SUPLA_HVAC_VALUE_FLAG_COOL);

  if (this->hvacSubfunctionCool) {
    hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_COOL;
  }

  if (hasOn) {
    hvacValue.IsOn = on ? 1 : 0;
    if (!on) {
      hvacValue.Mode = SUPLA_HVAC_MODE_OFF;
    } else if (hvacValue.Mode == SUPLA_HVAC_MODE_OFF ||
               hvacValue.Mode == SUPLA_HVAC_MODE_NOT_SET) {
      hvacValue.Mode =
          this->hvacSubfunctionCool ? SUPLA_HVAC_MODE_COOL
                                    : SUPLA_HVAC_MODE_HEAT;
    }
  }

  if (hasSetpointTemperature) {
    _supla_int16_t rawSetpoint =
        thermostat_temperature_to_raw(setpointTemperature);

    if (this->hvacSubfunctionCool) {
      hvacValue.SetpointTemperatureHeat = 0;
      hvacValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET;
      hvacValue.SetpointTemperatureCool = rawSetpoint;
      hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET;
    } else {
      hvacValue.SetpointTemperatureCool = 0;
      hvacValue.Flags &= ~SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET;
      hvacValue.SetpointTemperatureHeat = rawSetpoint;
      hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET;
    }
  }

  memset(tmp_value, 0, sizeof(tmp_value));
  memcpy(tmp_value, &hvacValue, sizeof(THVACValue));
  setValue(tmp_value);
}

void client_device_channel::updateHvacFanState(bool hasOn, bool on,
                                               bool hasMode,
                                               unsigned char mode) {
  if (this->function != SUPLA_CHANNELFNC_HVAC_FAN) return;

  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  THVACValue hvacValue;

  memset(tmp_value, 0, sizeof(tmp_value));
  memset(&hvacValue, 0, sizeof(hvacValue));

  getValue(tmp_value);
  memcpy(&hvacValue, tmp_value, sizeof(THVACValue));

  hvacValue.Flags &= ~(SUPLA_HVAC_VALUE_FLAG_HEATING |
                       SUPLA_HVAC_VALUE_FLAG_COOLING |
                       SUPLA_HVAC_VALUE_FLAG_COOL |
                       SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET |
                       SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET);
  hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_FAN_ENABLED;
  hvacValue.SetpointTemperatureHeat = 0;
  hvacValue.SetpointTemperatureCool = 0;

  if (hasMode) {
    hvacValue.Mode = mode;
    hvacValue.IsOn = mode == SUPLA_HVAC_MODE_OFF ? 0 : 1;
  } else if (hasOn) {
    hvacValue.IsOn = on ? 1 : 0;
    hvacValue.Mode = on ? SUPLA_HVAC_MODE_FAN_ONLY : SUPLA_HVAC_MODE_OFF;
  }

  if (hvacValue.Mode != SUPLA_HVAC_MODE_OFF &&
      hvacValue.Mode != SUPLA_HVAC_MODE_FAN_ONLY) {
    hvacValue.Mode = hvacValue.IsOn ? SUPLA_HVAC_MODE_FAN_ONLY
                                    : SUPLA_HVAC_MODE_OFF;
  }

  memset(tmp_value, 0, sizeof(tmp_value));
  memcpy(tmp_value, &hvacValue, sizeof(THVACValue));
  setValue(tmp_value);
}

void client_device_channel::updateHvacDifferentialState(
    bool hasOn, bool on, bool hasMode, unsigned char mode,
    bool hasSetpointTemperatureLow,
    double setpointTemperatureLow, bool hasSetpointTemperatureHigh,
    double setpointTemperatureHigh, bool hasHeatingState, bool heatingState,
    bool hasCoolingState, bool coolingState) {
  if (this->function != SUPLA_CHANNELFNC_HVAC_THERMOSTAT_DIFFERENTIAL) return;

  char tmp_value[SUPLA_CHANNELVALUE_SIZE];
  THVACValue hvacValue;

  memset(tmp_value, 0, sizeof(tmp_value));
  memset(&hvacValue, 0, sizeof(hvacValue));

  getValue(tmp_value);
  memcpy(&hvacValue, tmp_value, sizeof(THVACValue));

  hvacValue.Flags &= ~(SUPLA_HVAC_VALUE_FLAG_HEATING |
                       SUPLA_HVAC_VALUE_FLAG_COOLING |
                       SUPLA_HVAC_VALUE_FLAG_COOL);

  if (hasMode) {
    hvacValue.Mode = mode;
    hvacValue.IsOn = mode == SUPLA_HVAC_MODE_OFF ? 0 : 1;
  } else if (hasOn) {
    hvacValue.IsOn = on ? 1 : 0;

    if (!on) {
      hvacValue.Mode = SUPLA_HVAC_MODE_OFF;
    } else if (hvacValue.Mode == SUPLA_HVAC_MODE_OFF ||
               hvacValue.Mode == SUPLA_HVAC_MODE_NOT_SET) {
      hvacValue.Mode = SUPLA_HVAC_MODE_HEAT_COOL;
    }
  }

  if (hasSetpointTemperatureLow) {
    hvacValue.SetpointTemperatureHeat =
        thermostat_temperature_to_raw(setpointTemperatureLow);
    hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET;
  }

  if (hasSetpointTemperatureHigh) {
    hvacValue.SetpointTemperatureCool =
        thermostat_temperature_to_raw(setpointTemperatureHigh);
    hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET;
  }

  if (!hasMode) {
    if ((hvacValue.Flags & SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET) != 0 &&
        (hvacValue.Flags & SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET) != 0 &&
        hvacValue.IsOn) {
      hvacValue.Mode = SUPLA_HVAC_MODE_HEAT_COOL;
    } else if ((hvacValue.Flags &
                SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET) != 0 &&
               hvacValue.IsOn) {
      hvacValue.Mode = SUPLA_HVAC_MODE_COOL;
    } else if ((hvacValue.Flags &
                SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET) != 0 &&
               hvacValue.IsOn) {
      hvacValue.Mode = SUPLA_HVAC_MODE_HEAT;
    }
  }

  if (hasHeatingState && heatingState) {
    hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_HEATING;
  }

  if (hasCoolingState && coolingState) {
    hvacValue.Flags |= SUPLA_HVAC_VALUE_FLAG_COOLING;
  }

  memset(tmp_value, 0, sizeof(tmp_value));
  memcpy(tmp_value, &hvacValue, sizeof(THVACValue));
  setValue(tmp_value);
}

int client_device_channel::getType(void) {
  if (this->type == 0) {
    switch (this->function) {
      case SUPLA_CHANNELFNC_OPENINGSENSOR_GATEWAY:
      case SUPLA_CHANNELFNC_OPENINGSENSOR_GATE:
      case SUPLA_CHANNELFNC_OPENINGSENSOR_GARAGEDOOR:
      case SUPLA_CHANNELFNC_OPENINGSENSOR_DOOR:
      case SUPLA_CHANNELFNC_NOLIQUIDSENSOR:
      case SUPLA_CHANNELFNC_OPENINGSENSOR_ROLLERSHUTTER:
      case SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW:  // ver. >= 8
      case SUPLA_CHANNELFNC_MAILSENSOR:            // ver. >= 8
        this->type = SUPLA_CHANNELTYPE_SENSORNO;
        break;

      case SUPLA_CHANNELFNC_CONTROLLINGTHEGATEWAYLOCK:
      case SUPLA_CHANNELFNC_CONTROLLINGTHEGATE:
      case SUPLA_CHANNELFNC_CONTROLLINGTHEGARAGEDOOR:
      case SUPLA_CHANNELFNC_CONTROLLINGTHEDOORLOCK:
      case SUPLA_CHANNELFNC_POWERSWITCH:
      case SUPLA_CHANNELFNC_LIGHTSWITCH:
      case SUPLA_CHANNELFNC_PUMPSWITCH:
      case SUPLA_CHANNELFNC_HEATORCOLDSOURCESWITCH:
      case SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER:
      case SUPLA_CHANNELFNC_CONTROLLINGTHEFACADEBLIND:
      case SUPLA_CHANNELFNC_STAIRCASETIMER:
        this->type = SUPLA_CHANNELTYPE_RELAYG5LA1A;
        break;

      case SUPLA_CHANNELFNC_THERMOMETER:
        this->type = SUPLA_CHANNELTYPE_THERMOMETER;
        break;

      case SUPLA_CHANNELFNC_DIMMER:
        this->type = SUPLA_CHANNELTYPE_DIMMER;
        break;

      case SUPLA_CHANNELFNC_RGBLIGHTING:
        this->type = SUPLA_CHANNELTYPE_RGBLEDCONTROLLER;
        break;

      case SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING:
        this->type = SUPLA_CHANNELTYPE_DIMMERANDRGBLED;
        break;

      case SUPLA_CHANNELFNC_DEPTHSENSOR:     // ver. >= 5
      case SUPLA_CHANNELFNC_DISTANCESENSOR:  // ver. >= 5
        this->type = SUPLA_CHANNELTYPE_DISTANCESENSOR;
        break;

      case SUPLA_CHANNELFNC_HUMIDITY:
        this->type = SUPLA_CHANNELTYPE_HUMIDITYSENSOR;
        break;

      case SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE:
        this->type = SUPLA_CHANNELTYPE_HUMIDITYANDTEMPSENSOR;
        break;

      case SUPLA_CHANNELFNC_WINDSENSOR:
        this->type = SUPLA_CHANNELTYPE_WINDSENSOR;
        break;  // ver. >= 8
      case SUPLA_CHANNELFNC_PRESSURESENSOR:
        this->type = SUPLA_CHANNELTYPE_PRESSURESENSOR;
        break;  // ver. >= 8
      case SUPLA_CHANNELFNC_RAINSENSOR:
        this->type = SUPLA_CHANNELTYPE_RAINSENSOR;
        break;  // ver. >= 8
      case SUPLA_CHANNELFNC_WEIGHTSENSOR:
        this->type = SUPLA_CHANNELTYPE_WEIGHTSENSOR;
        break;
      case SUPLA_CHANNELFNC_WEATHER_STATION:
        this->type = SUPLA_CHANNELTYPE_WEATHER_STATION;
        break;

      case SUPLA_CHANNELFNC_IC_ELECTRICITY_METER:  // ver. >= 12
      case SUPLA_CHANNELFNC_IC_GAS_METER:          // ver. >= 10
      case SUPLA_CHANNELFNC_IC_WATER_METER:
        this->type = SUPLA_CHANNELTYPE_IMPULSE_COUNTER;
        break;  // ver. >= 10

      case SUPLA_CHANNELFNC_ELECTRICITY_METER:
        this->type = SUPLA_CHANNELTYPE_ELECTRICITY_METER;
        break;

      case SUPLA_CHANNELFNC_THERMOSTAT:
        this->type = SUPLA_CHANNELTYPE_THERMOSTAT;
        break;  // ver. >= 11
      case SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS:
        this->type = SUPLA_CHANNELTYPE_THERMOSTAT_HEATPOL_HOMEPLUS;
        break;
      case SUPLA_CHANNELFNC_HVAC_THERMOSTAT:
      case SUPLA_CHANNELFNC_HVAC_FAN:
      case SUPLA_CHANNELFNC_HVAC_THERMOSTAT_DIFFERENTIAL:
        this->type = SUPLA_CHANNELTYPE_HVAC;
        break;

      case SUPLA_CHANNELFNC_GENERAL_PURPOSE_MEASUREMENT:
        this->type = SUPLA_CHANNELTYPE_GENERAL_PURPOSE_MEASUREMENT;
        break;
      case SUPLA_CHANNELFNC_ACTIONTRIGGER:
        this->type = SUPLA_CHANNELTYPE_ACTIONTRIGGER;
    }
  }

  return this->type;
}
int client_device_channel::getFunction(void) { return this->function; }
int client_device_channel::getNumber(void) { return this->number; }
int client_device_channel::getIntervalSec() { return this->intervalSec; }
int client_device_channel::getFileWriteCheckSec() {
  return this->fileWriteCheckSeck;
}
std::string client_device_channel::getFileName(void) { return this->fileName; }
std::string client_device_channel::getPayloadOn(void) {
  return this->payloadOn;
};

void client_device_channel::setFileWriteCheckSec(int value) {
  this->fileWriteCheckSeck = value;
}

std::string client_device_channel::getIdTemplate(void) {
  return this->idTemplate;
}

std::string client_device_channel::getIdValue(void) { return this->idValue; }

std::string client_device_channel::getPayloadOff(void) {
  return this->payloadOff;
};
std::string client_device_channel::getPayloadValue(void) {
  return this->payloadValue;
};
std::string client_device_channel::getStateTopic(void) {
  return this->stateTopic;
}
std::string client_device_channel::getCommandTopic(void) {
  return this->commandTopic;
}
std::string client_device_channel::getTemperatureTopic(void) {
  return this->temperatureTopic;
}
std::string client_device_channel::getHumidityTopic(void) {
  return this->humidityTopic;
}
std::string client_device_channel::getVoltageTopic(void) {
  return this->voltageTopic;
}
std::string client_device_channel::getVoltageTopic(unsigned char phase) {
  switch (phase) {
    case 1:
      return this->voltageTopicL2;
    case 2:
      return this->voltageTopicL3;
    default:
      return this->voltageTopic;
  }
}
std::string client_device_channel::getCurrentTopic(void) {
  return this->currentTopic;
}
std::string client_device_channel::getCurrentTopic(unsigned char phase) {
  switch (phase) {
    case 1:
      return this->currentTopicL2;
    case 2:
      return this->currentTopicL3;
    default:
      return this->currentTopic;
  }
}
std::string client_device_channel::getPowerTopic(void) {
  return this->powerTopic;
}
std::string client_device_channel::getPowerTopic(unsigned char phase) {
  switch (phase) {
    case 1:
      return this->powerTopicL2;
    case 2:
      return this->powerTopicL3;
    default:
      return this->powerTopic;
  }
}
std::string client_device_channel::getEnergyTopic(void) {
  return this->energyTopic;
}
std::string client_device_channel::getEnergyTopic(unsigned char phase) {
  switch (phase) {
    case 1:
      return this->energyTopicL2;
    case 2:
      return this->energyTopicL3;
    default:
      return this->energyTopic;
  }
}
std::string client_device_channel::getFrequencyTopic(void) {
  return this->frequencyTopic;
}
std::string client_device_channel::getReactivePowerTopic(unsigned char phase) {
  return phase < 3 ? this->reactivePowerTopic[phase] : "";
}
std::string client_device_channel::getApparentPowerTopic(unsigned char phase) {
  return phase < 3 ? this->apparentPowerTopic[phase] : "";
}
std::string client_device_channel::getPowerFactorTopic(unsigned char phase) {
  return phase < 3 ? this->powerFactorTopic[phase] : "";
}
std::string client_device_channel::getPhaseAngleTopic(unsigned char phase) {
  return phase < 3 ? this->phaseAngleTopic[phase] : "";
}
std::string client_device_channel::getReturnedEnergyTopic(unsigned char phase) {
  return phase < 3 ? this->returnedEnergyTopic[phase] : "";
}
std::string client_device_channel::getInductiveEnergyTopic(
    unsigned char phase) {
  return phase < 3 ? this->inductiveEnergyTopic[phase] : "";
}
std::string client_device_channel::getCapacitiveEnergyTopic(
    unsigned char phase) {
  return phase < 3 ? this->capacitiveEnergyTopic[phase] : "";
}
std::string client_device_channel::getBrightnessTopic(void) {
  return this->brightnessTopic;
}
std::string client_device_channel::getColorBrightnessTopic(void) {
  return this->colorBrightnessTopic;
}
std::string client_device_channel::getColorTopic(void) {
  return this->colorTopic;
}
std::string client_device_channel::getMeasuredTemperatureTopic(void) {
  return this->measuredTemperatureTopic;
}
std::string client_device_channel::getPresetTemperatureTopic(void) {
  return this->presetTemperatureTopic;
}
std::string client_device_channel::getPresetTemperatureHighTopic(void) {
  return this->presetTemperatureHighTopic;
}
std::string client_device_channel::getActionTopic(void) {
  return this->actionTopic;
}
std::string client_device_channel::getPositionCommandTopic(void) {
  return this->positionCommandTopic;
}
std::string client_device_channel::getPresetTemperatureCommandTopic(void) {
  return this->presetTemperatureCommandTopic;
}
std::string client_device_channel::getPresetTemperatureHighCommandTopic(void) {
  return this->presetTemperatureHighCommandTopic;
}
std::string client_device_channel::getStateTemplate(void) {
  return this->stateTemplate;
}
std::string client_device_channel::getCommandTemplate(void) {
  return this->commandTemplate;
}
std::string client_device_channel::getCommandTemplateOn(void) {
  return this->commandTemplateOn;
}
std::string client_device_channel::getCommandTemplateOff(void) {
  return this->commandTemplateOff;
}
std::string client_device_channel::getPresetTemperatureCommandTemplate(void) {
  return this->presetTemperatureCommandTemplate;
}
std::string client_device_channel::getPresetTemperatureHighCommandTemplate(void) {
  return this->presetTemperatureHighCommandTemplate;
}
std::string client_device_channel::getExecute(void) { return this->execute; }
std::string client_device_channel::getExecuteOn(void) {
  return this->executeOn;
}
std::string client_device_channel::getExecuteOff(void) {
  return this->executeOff;
}
bool client_device_channel::getRetain(void) { return this->retain; }
bool client_device_channel::getInvertState(void) { return this->invertState; }
bool client_device_channel::getEsphomeCover(void) {
  return this->esphomeCover;
}
bool client_device_channel::getEsphomeRgbw(void) {
  return this->esphomeRgbw;
}
bool client_device_channel::getHvacSubfunctionCool(void) {
  return this->hvacSubfunctionCool;
}
unsigned char client_device_channel::getHvacMainThermometerChannelNo(void) {
  return this->hvacMainThermometerChannelNo;
}
bool client_device_channel::getHvacConfig(TChannelConfig_HVAC *hvacConfig) {
  if (hvacConfig == NULL || !this->hvacConfigInitialized) {
    return false;
  }

  memcpy(hvacConfig, &this->hvacConfig, sizeof(TChannelConfig_HVAC));
  return true;
}
_supla_int_t client_device_channel::getGeneralValueDivider(void) {
  return this->generalValueDivider;
}
_supla_int_t client_device_channel::getGeneralValueMultiplier(void) {
  return this->generalValueMultiplier;
}
_supla_int64_t client_device_channel::getGeneralValueAdded(void) {
  return this->generalValueAdded;
}
unsigned char client_device_channel::getGeneralValuePrecision(void) {
  return this->generalValuePrecision;
}
std::string client_device_channel::getGeneralUnitBeforeValue(void) {
  return this->generalUnitBeforeValue;
}
std::string client_device_channel::getGeneralUnitAfterValue(void) {
  return this->generalUnitAfterValue;
}
bool client_device_channel::getGeneralNoSpaceBeforeValue(void) {
  return this->generalNoSpaceBeforeValue;
}
bool client_device_channel::getGeneralNoSpaceAfterValue(void) {
  return this->generalNoSpaceAfterValue;
}
bool client_device_channel::getGeneralKeepHistory(void) {
  return this->generalKeepHistory;
}
unsigned char client_device_channel::getGeneralChartType(void) {
  return this->generalChartType;
}
unsigned short client_device_channel::getGeneralRefreshIntervalMs(void) {
  return this->generalRefreshIntervalMs;
}
bool client_device_channel::getOnline(void) { return this->online; }
long client_device_channel::getLastSeconds(void) { return this->last.tv_sec; }

void client_device_channel::setType(int type) { this->type = type; }
void client_device_channel::setFunction(int function) {
  this->function = function;

  if (function == SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE) {
    setTempHum(-275, -1);
  }
}

void client_device_channel::setIdTemplate(const char *idTemplate) {
  this->idTemplate = std::string(idTemplate);
}

void client_device_channel::setIdValue(const char *idValue) {
  this->idValue = std::string(idValue);
}

void client_device_channel::setNumber(int number) { this->number = number; }
void client_device_channel::setFileName(const char *filename) {
  this->fileName = std::string(filename);
}
void client_device_channel::setPayloadOn(const char *payloadOn) {
  this->payloadOn = std::string(payloadOn);
}
void client_device_channel::setPayloadOff(const char *payloadOff) {
  this->payloadOff = std::string(payloadOff);
}
void client_device_channel::setPayloadValue(const char *payloadValue) {
  this->payloadValue = std::string(payloadValue);
}
void client_device_channel::setStateTopic(const char *stateTopic) {
  this->stateTopic = std::string(stateTopic);
}
void client_device_channel::setCommandTopic(const char *commandTopic) {
  this->commandTopic = std::string(commandTopic);
}
void client_device_channel::setTemperatureTopic(const char *temperatureTopic) {
  this->temperatureTopic = std::string(temperatureTopic);
}
void client_device_channel::setHumidityTopic(const char *humidityTopic) {
  this->humidityTopic = std::string(humidityTopic);
}
void client_device_channel::setVoltageTopic(const char *voltageTopic) {
  this->voltageTopic = std::string(voltageTopic);
}
void client_device_channel::setVoltageTopic(unsigned char phase,
                                            const char *voltageTopic) {
  std::string value = voltageTopic == NULL ? "" : std::string(voltageTopic);

  switch (phase) {
    case 1:
      this->voltageTopicL2 = value;
      break;
    case 2:
      this->voltageTopicL3 = value;
      break;
    default:
      this->voltageTopic = value;
      break;
  }
}
void client_device_channel::setCurrentTopic(const char *currentTopic) {
  this->currentTopic = std::string(currentTopic);
}
void client_device_channel::setCurrentTopic(unsigned char phase,
                                            const char *currentTopic) {
  std::string value = currentTopic == NULL ? "" : std::string(currentTopic);

  switch (phase) {
    case 1:
      this->currentTopicL2 = value;
      break;
    case 2:
      this->currentTopicL3 = value;
      break;
    default:
      this->currentTopic = value;
      break;
  }
}
void client_device_channel::setPowerTopic(const char *powerTopic) {
  this->powerTopic = std::string(powerTopic);
}
void client_device_channel::setPowerTopic(unsigned char phase,
                                          const char *powerTopic) {
  std::string value = powerTopic == NULL ? "" : std::string(powerTopic);

  switch (phase) {
    case 1:
      this->powerTopicL2 = value;
      break;
    case 2:
      this->powerTopicL3 = value;
      break;
    default:
      this->powerTopic = value;
      break;
  }
}
void client_device_channel::setEnergyTopic(const char *energyTopic) {
  this->energyTopic = std::string(energyTopic);
}
void client_device_channel::setEnergyTopic(unsigned char phase,
                                           const char *energyTopic) {
  std::string value = energyTopic == NULL ? "" : std::string(energyTopic);

  switch (phase) {
    case 1:
      this->energyTopicL2 = value;
      break;
    case 2:
      this->energyTopicL3 = value;
      break;
    default:
      this->energyTopic = value;
      break;
  }
}
void client_device_channel::setFrequencyTopic(const char *frequencyTopic) {
  this->frequencyTopic = std::string(frequencyTopic);
}
void client_device_channel::setReactivePowerTopic(unsigned char phase,
                                                  const char *topic) {
  if (phase < 3) {
    this->reactivePowerTopic[phase] =
        topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setApparentPowerTopic(unsigned char phase,
                                                  const char *topic) {
  if (phase < 3) {
    this->apparentPowerTopic[phase] =
        topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setPowerFactorTopic(unsigned char phase,
                                                const char *topic) {
  if (phase < 3) {
    this->powerFactorTopic[phase] = topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setPhaseAngleTopic(unsigned char phase,
                                               const char *topic) {
  if (phase < 3) {
    this->phaseAngleTopic[phase] = topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setReturnedEnergyTopic(unsigned char phase,
                                                   const char *topic) {
  if (phase < 3) {
    this->returnedEnergyTopic[phase] = topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setInductiveEnergyTopic(unsigned char phase,
                                                    const char *topic) {
  if (phase < 3) {
    this->inductiveEnergyTopic[phase] =
        topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setCapacitiveEnergyTopic(unsigned char phase,
                                                     const char *topic) {
  if (phase < 3) {
    this->capacitiveEnergyTopic[phase] =
        topic == NULL ? "" : std::string(topic);
  }
}
void client_device_channel::setBrightnessTopic(const char *brightnessTopic) {
  this->brightnessTopic = std::string(brightnessTopic);
}
void client_device_channel::setColorBrightnessTopic(
    const char *colorBrightnessTopic) {
  this->colorBrightnessTopic = std::string(colorBrightnessTopic);
}
void client_device_channel::setColorTopic(const char *colorTopic) {
  this->colorTopic = std::string(colorTopic);
}
void client_device_channel::setMeasuredTemperatureTopic(
    const char *measuredTemperatureTopic) {
  this->measuredTemperatureTopic = std::string(measuredTemperatureTopic);
}
void client_device_channel::setPresetTemperatureTopic(
    const char *presetTemperatureTopic) {
  this->presetTemperatureTopic = std::string(presetTemperatureTopic);
}
void client_device_channel::setPresetTemperatureHighTopic(
    const char *presetTemperatureHighTopic) {
  this->presetTemperatureHighTopic =
      presetTemperatureHighTopic == NULL ? "" : presetTemperatureHighTopic;
}
void client_device_channel::setActionTopic(const char *actionTopic) {
  this->actionTopic = actionTopic == NULL ? "" : actionTopic;
}
void client_device_channel::setPositionCommandTopic(
    const char *positionCommandTopic) {
  this->positionCommandTopic = std::string(positionCommandTopic);
}
void client_device_channel::setPresetTemperatureCommandTopic(
    const char *presetTemperatureCommandTopic) {
  this->presetTemperatureCommandTopic =
      std::string(presetTemperatureCommandTopic);
}
void client_device_channel::setPresetTemperatureHighCommandTopic(
    const char *presetTemperatureHighCommandTopic) {
  this->presetTemperatureHighCommandTopic =
      presetTemperatureHighCommandTopic == NULL ? ""
                                                : presetTemperatureHighCommandTopic;
}
void client_device_channel::setStateTemplate(const char *stateTemplate) {
  this->stateTemplate = std::string(stateTemplate);
}
void client_device_channel::setCommandTemplate(const char *commandTemplate) {
  this->commandTemplate = std::string(commandTemplate);
}
void client_device_channel::setCommandTemplateOn(
    const char *commandTemplateOn) {
  this->commandTemplateOn = std::string(commandTemplateOn);
}
void client_device_channel::setCommandTemplateOff(
    const char *commandTemplateOff) {
  this->commandTemplateOff = std::string(commandTemplateOff);
}
void client_device_channel::setPresetTemperatureCommandTemplate(
    const char *presetTemperatureCommandTemplate) {
  this->presetTemperatureCommandTemplate =
      std::string(presetTemperatureCommandTemplate);
}
void client_device_channel::setPresetTemperatureHighCommandTemplate(
    const char *presetTemperatureHighCommandTemplate) {
  this->presetTemperatureHighCommandTemplate =
      presetTemperatureHighCommandTemplate == NULL
          ? ""
          : presetTemperatureHighCommandTemplate;
}
void client_device_channel::setExecute(const char *execute) {
  this->execute = std::string(execute);
}
void client_device_channel::setExecuteOn(const char *execute) {
  this->executeOn = std::string(execute);
}
void client_device_channel::setExecuteOff(const char *execute) {
  this->executeOff = std::string(execute);
}
void client_device_channel::setRetain(bool retain) { this->retain = retain; }
void client_device_channel::setInvertState(bool invertState) {
  this->invertState = invertState;
}
void client_device_channel::setEsphomeCover(bool esphomeCover) {
  this->esphomeCover = esphomeCover;
}
void client_device_channel::setEsphomeRgbw(bool esphomeRgbw) {
  this->esphomeRgbw = esphomeRgbw;
}
void client_device_channel::setHvacSubfunctionCool(bool hvacSubfunctionCool) {
  this->hvacSubfunctionCool = hvacSubfunctionCool;
}

void client_device_channel::setHvacReportAsThermostat(bool value) {
  this->hvacReportAsThermostat = value;
}
void client_device_channel::setHvacMainThermometerChannelNo(
    unsigned char channelNo) {
  this->hvacMainThermometerChannelNo = channelNo;
}
void client_device_channel::setHvacConfig(const TChannelConfig_HVAC *hvacConfig) {
  if (hvacConfig == NULL) {
    memset(&this->hvacConfig, 0, sizeof(this->hvacConfig));
    this->hvacConfigInitialized = false;
    return;
  }

  memcpy(&this->hvacConfig, hvacConfig, sizeof(TChannelConfig_HVAC));
  this->hvacConfigInitialized = true;
}
void client_device_channel::setGeneralValueDivider(_supla_int_t value) {
  this->generalValueDivider = value;
}
void client_device_channel::setGeneralValueMultiplier(_supla_int_t value) {
  this->generalValueMultiplier = value;
}
void client_device_channel::setGeneralValueAdded(_supla_int64_t value) {
  this->generalValueAdded = value;
}
void client_device_channel::setGeneralValuePrecision(unsigned char value) {
  this->generalValuePrecision = value > 4 ? 4 : value;
}
void client_device_channel::setGeneralUnitBeforeValue(const char *value) {
  this->generalUnitBeforeValue = value == NULL ? "" : std::string(value);
}
void client_device_channel::setGeneralUnitAfterValue(const char *value) {
  this->generalUnitAfterValue = value == NULL ? "" : std::string(value);
}
void client_device_channel::setGeneralNoSpaceBeforeValue(bool value) {
  this->generalNoSpaceBeforeValue = value;
}
void client_device_channel::setGeneralNoSpaceAfterValue(bool value) {
  this->generalNoSpaceAfterValue = value;
}
void client_device_channel::setGeneralKeepHistory(bool value) {
  this->generalKeepHistory = value;
}
void client_device_channel::setGeneralChartType(unsigned char value) {
  switch (value) {
    case SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_BAR:
    case SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_CANDLE:
      this->generalChartType = value;
      break;
    default:
      this->generalChartType =
          SUPLA_GENERAL_PURPOSE_MEASUREMENT_CHART_TYPE_LINEAR;
      break;
  }
}
void client_device_channel::setGeneralRefreshIntervalMs(unsigned short value) {
  this->generalRefreshIntervalMs = value;
}
void client_device_channel::setOnline(bool value) { this->online = value; }
void client_device_channel::setLastSeconds(void) {
  gettimeofday(&this->last, NULL);
}
void client_device_channel::setIntervalSec(int interval) {
  this->intervalSec = interval;
}

void client_device_channel::transformIncomingState(
    char value[SUPLA_CHANNELVALUE_SIZE]) {
  if (!this->invertState || value == NULL) return;

  switch (this->function) {
    case SUPLA_CHANNELFNC_POWERSWITCH:
    case SUPLA_CHANNELFNC_LIGHTSWITCH:
    case SUPLA_CHANNELFNC_STAIRCASETIMER:
    case SUPLA_CHANNELFNC_PUMPSWITCH:
    case SUPLA_CHANNELFNC_HEATORCOLDSOURCESWITCH:
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
    case SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW:
    case SUPLA_CHANNELFNC_MAILSENSOR:
      value[0] = value[0] > 0 ? 0 : 1;
      break;
    case SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER:
    case SUPLA_CHANNELFNC_CONTROLLINGTHEFACADEBLIND: {
      int shutter = static_cast<unsigned char>(value[0]);

      if (shutter < 0) shutter = 0;
      if (shutter > 100) shutter = 100;

      value[0] = 100 - shutter;
    } break;
  }
}

client_device_channels::~client_device_channels() {
  safe_array_lock(arr);
  for (int i = 0; i < safe_array_count(arr); i++) {
    client_device_channel *channel =
        (client_device_channel *)safe_array_get(arr, i);
    delete channel;
  }
  safe_array_unlock(arr);
  safe_array_free(arr);
}

client_device_channels::client_device_channels() {
  this->initialized = false;
  this->arr = safe_array_init();
  this->on_valuechanged = NULL;
  this->on_valuechanged_user_data = NULL;
}
client_device_channel *client_device_channels::add_channel(int number) {
  safe_array_lock(arr);

  client_device_channel *channel = new client_device_channel(number);

  if (channel != NULL && safe_array_add(arr, channel) == -1) {
    delete channel;
    channel = NULL;
  }

  safe_array_unlock(arr);
  return channel;
}

void client_device_channels::getMqttSubscriptionTopics(
    std::vector<std::string> *vect) {
  client_device_channel *channel;
  int i;

  for (i = 0; i < safe_array_count(arr); i++) {
    channel = (client_device_channel *)safe_array_get(arr, i);
    push_topic_if_set(vect, channel->getStateTopic());
    push_topic_if_set(vect, channel->getTemperatureTopic());
    push_topic_if_set(vect, channel->getHumidityTopic());
    for (unsigned char phase = 0; phase < 3; phase++) {
      push_topic_if_set(vect, channel->getVoltageTopic(phase));
      push_topic_if_set(vect, channel->getCurrentTopic(phase));
      push_topic_if_set(vect, channel->getPowerTopic(phase));
      push_topic_if_set(vect, channel->getEnergyTopic(phase));
      push_topic_if_set(vect, channel->getReactivePowerTopic(phase));
      push_topic_if_set(vect, channel->getApparentPowerTopic(phase));
      push_topic_if_set(vect, channel->getPowerFactorTopic(phase));
      push_topic_if_set(vect, channel->getPhaseAngleTopic(phase));
      push_topic_if_set(vect, channel->getReturnedEnergyTopic(phase));
      push_topic_if_set(vect, channel->getInductiveEnergyTopic(phase));
      push_topic_if_set(vect, channel->getCapacitiveEnergyTopic(phase));
    }
    push_topic_if_set(vect, channel->getFrequencyTopic());
    push_topic_if_set(vect, channel->getBrightnessTopic());
    push_topic_if_set(vect, channel->getColorBrightnessTopic());
    push_topic_if_set(vect, channel->getColorTopic());
    push_topic_if_set(vect, channel->getMeasuredTemperatureTopic());
    push_topic_if_set(vect, channel->getPresetTemperatureTopic());
    push_topic_if_set(vect, channel->getPresetTemperatureHighTopic());
    push_topic_if_set(vect, channel->getActionTopic());
  }
}
int client_device_channels::getChannelCount(void) {
  return safe_array_count(arr);
}
bool client_device_channels::getInitialized(void) { return this->initialized; }
void client_device_channels::setInitialized(bool initialized) {
  this->initialized = initialized;
}
client_device_channel *client_device_channels::getChannel(int idx) {
  return (client_device_channel *)safe_array_get(arr, idx);
}
client_device_channel *client_device_channels::find_channel(int number) {
  int i;

  client_device_channel *channel;

  for (i = 0; i < safe_array_count(arr); i++) {
    channel = (client_device_channel *)safe_array_get(arr, i);

    if (channel->getNumber() == number) {
      return channel;
    }
  };

  channel = add_channel(number);

  return channel;
}

void client_device_channels::get_channels_for_topic(
    std::string topic, std::vector<client_device_channel *> *vect) {
  int i;

  client_device_channel *channel;

  for (i = 0; i < safe_array_count(arr); i++) {
    channel = (client_device_channel *)safe_array_get(arr, i);
    if (topic_matches(channel->getStateTopic(), topic) ||
        topic_matches(channel->getTemperatureTopic(), topic) ||
        topic_matches(channel->getHumidityTopic(), topic) ||
        topic_matches(channel->getFrequencyTopic(), topic) ||
        topic_matches(channel->getBrightnessTopic(), topic) ||
        topic_matches(channel->getColorBrightnessTopic(), topic) ||
        topic_matches(channel->getColorTopic(), topic) ||
        topic_matches(channel->getMeasuredTemperatureTopic(), topic) ||
        topic_matches(channel->getPresetTemperatureTopic(), topic) ||
        topic_matches(channel->getPresetTemperatureHighTopic(), topic) ||
        topic_matches(channel->getActionTopic(), topic)) {
      vect->push_back(channel);
      continue;
    }

    for (unsigned char phase = 0; phase < 3; phase++) {
      if (topic_matches(channel->getVoltageTopic(phase), topic) ||
          topic_matches(channel->getCurrentTopic(phase), topic) ||
          topic_matches(channel->getPowerTopic(phase), topic) ||
          topic_matches(channel->getEnergyTopic(phase), topic) ||
          topic_matches(channel->getReactivePowerTopic(phase), topic) ||
          topic_matches(channel->getApparentPowerTopic(phase), topic) ||
          topic_matches(channel->getPowerFactorTopic(phase), topic) ||
          topic_matches(channel->getPhaseAngleTopic(phase), topic) ||
          topic_matches(channel->getReturnedEnergyTopic(phase), topic) ||
          topic_matches(channel->getInductiveEnergyTopic(phase), topic) ||
          topic_matches(channel->getCapacitiveEnergyTopic(phase), topic)) {
        vect->push_back(channel);
        break;
      }
    }
  };
}

client_device_channel *client_device_channels::find_channel_by_topic(
    std::string topic) {
  int i;

  client_device_channel *channel;

  for (i = 0; i < safe_array_count(arr); i++) {
    channel = (client_device_channel *)safe_array_get(arr, i);
    if (topic_matches(channel->getStateTopic(), topic) ||
        topic_matches(channel->getTemperatureTopic(), topic) ||
        topic_matches(channel->getHumidityTopic(), topic) ||
        topic_matches(channel->getFrequencyTopic(), topic) ||
        topic_matches(channel->getBrightnessTopic(), topic) ||
        topic_matches(channel->getColorBrightnessTopic(), topic) ||
        topic_matches(channel->getColorTopic(), topic) ||
        topic_matches(channel->getMeasuredTemperatureTopic(), topic) ||
        topic_matches(channel->getPresetTemperatureTopic(), topic) ||
        topic_matches(channel->getPresetTemperatureHighTopic(), topic) ||
        topic_matches(channel->getActionTopic(), topic)) {
      return channel;
    }

    for (unsigned char phase = 0; phase < 3; phase++) {
      if (topic_matches(channel->getVoltageTopic(phase), topic) ||
          topic_matches(channel->getCurrentTopic(phase), topic) ||
          topic_matches(channel->getPowerTopic(phase), topic) ||
          topic_matches(channel->getEnergyTopic(phase), topic) ||
          topic_matches(channel->getReactivePowerTopic(phase), topic) ||
          topic_matches(channel->getApparentPowerTopic(phase), topic) ||
          topic_matches(channel->getPowerFactorTopic(phase), topic) ||
          topic_matches(channel->getPhaseAngleTopic(phase), topic) ||
          topic_matches(channel->getReturnedEnergyTopic(phase), topic) ||
          topic_matches(channel->getInductiveEnergyTopic(phase), topic) ||
          topic_matches(channel->getCapacitiveEnergyTopic(phase), topic)) {
        return channel;
      }
    }
  };

  return NULL;
}

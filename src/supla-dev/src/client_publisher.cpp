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

#include "client_publisher.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace {

void publish_to_mqtt_topic(const std::string &topic, const std::string &payload,
                           bool retain) {
  if (topic.length() == 0) return;

  mqtt_client_publish(topic.c_str(), payload.c_str(), retain ? 1 : 0, 0);
}

std::string format_decimal_value(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2) << value;

  std::string formatted = stream.str();
  while (formatted.length() > 0 && formatted.find('.') != std::string::npos &&
         formatted.back() == '0') {
    formatted.pop_back();
  }

  if (formatted.length() > 0 && formatted.back() == '.') {
    formatted.pop_back();
  }

  return formatted;
}

int scale_percentage_to_255(char value) {
  int scaled = static_cast<int>(value);

  if (scaled < 0) scaled = 0;
  if (scaled > 100) scaled = 100;

  return (scaled * 255 + 50) / 100;
}

std::string get_channel_state_payload(client_device_channel *channel,
                                      char on_off) {
  std::string payloadOn = channel != NULL ? channel->getPayloadOn() : "";
  std::string payloadOff = channel != NULL ? channel->getPayloadOff() : "";

  if (payloadOn.length() == 0) payloadOn = "1";
  if (payloadOff.length() == 0) payloadOff = "0";

  return on_off > 0 ? payloadOn : payloadOff;
}

void replace_rgbw_placeholders(std::string *payload,
                               client_device_channel *channel, int color,
                               char color_brightness, char brightness,
                               char on_off) {
  if (payload == NULL) return;

  unsigned char blue = (unsigned char)((color & 0x000000FF));
  unsigned char green = (unsigned char)((color & 0x0000FF00) >> 8);
  unsigned char red = (unsigned char)((color & 0x00FF0000) >> 16);

  char hex_color[11];
  char hash_color[8];
  int brightness255 = scale_percentage_to_255(brightness);
  int colorBrightness255 = scale_percentage_to_255(color_brightness);
  snprintf(hex_color, sizeof(hex_color), "0x%02X%02X%02X", red, green, blue);
  snprintf(hash_color, sizeof(hash_color), "#%02X%02X%02X", red, green, blue);

  replace_string_in_place(payload, "$red$", std::to_string(red));
  replace_string_in_place(payload, "$green$", std::to_string(green));
  replace_string_in_place(payload, "$blue$", std::to_string(blue));
  replace_string_in_place(payload, "$color$", hex_color);
  replace_string_in_place(payload, "$hex_color$", hash_color);
  replace_string_in_place(payload, "$color_brightness$",
                          std::to_string(color_brightness));
  replace_string_in_place(payload, "$color_brightness_255$",
                          std::to_string(colorBrightness255));
  replace_string_in_place(payload, "$brightness$", std::to_string(brightness));
  replace_string_in_place(payload, "$brightness_255$",
                          std::to_string(brightness255));
  replace_string_in_place(payload, "$value$",
                          get_channel_state_payload(channel, on_off));
  replace_string_in_place(payload, "$state$",
                          get_channel_state_payload(channel, on_off));
  replace_string_in_place(payload, "$on_off$", std::to_string(on_off > 0 ? 1 : 0));
}

bool publish_esphome_cover_message_for_channel(client_device_channel *channel) {
  char cv[SUPLA_CHANNELVALUE_SIZE];
  channel->getValue(cv);

  int rawValue = static_cast<unsigned char>(cv[0]);
  std::string commandTopic = channel->getCommandTopic();
  std::string positionCommandTopic = channel->getPositionCommandTopic();
  bool retain = channel->getRetain();

  if (rawValue < 0) rawValue = 0;
  if (rawValue > 110) rawValue = 110;

  if (rawValue == 0) {
    publish_to_mqtt_topic(commandTopic, "STOP", retain);
    return true;
  }

  if (rawValue == 1 || rawValue == 110) {
    publish_to_mqtt_topic(commandTopic, "CLOSE", retain);
    return true;
  }

  if (rawValue == 2 || rawValue == 10) {
    publish_to_mqtt_topic(commandTopic, "OPEN", retain);
    return true;
  }

  if (rawValue >= 10 && rawValue <= 110) {
    int openPercent = 110 - rawValue;

    if (openPercent < 0) openPercent = 0;
    if (openPercent > 100) openPercent = 100;

    publish_to_mqtt_topic(positionCommandTopic, std::to_string(openPercent),
                          retain);
    return true;
  }

  return false;
}

std::string get_hvac_on_mode_name(client_device_channel *channel) {
  if (channel != NULL && channel->getHvacSubfunctionCool()) {
    return "cool";
  }

  return "heat";
}

std::string build_hvac_mode_payload(client_device_channel *channel, bool isOn) {
  std::string payload =
      isOn ? channel->getCommandTemplateOn() : channel->getCommandTemplateOff();
  std::string modeName = isOn ? get_hvac_on_mode_name(channel) : "off";

  if (payload.length() == 0) {
    payload = channel->getCommandTemplate();
  }

  if (payload.length() == 0) {
    std::string fallbackOn = channel->getPayloadOn();
    std::string fallbackOff = channel->getPayloadOff();

    if (isOn) {
      payload = fallbackOn.length() > 0 ? fallbackOn : modeName;
    } else {
      payload = fallbackOff.length() > 0 ? fallbackOff : modeName;
    }
  }

  replace_string_in_place(&payload, "$value$", std::to_string(isOn ? 1 : 0));
  replace_string_in_place(&payload, "$state$", std::to_string(isOn ? 1 : 0));
  replace_string_in_place(&payload, "$mode$", modeName);

  return payload;
}

double get_hvac_publish_temperature(client_device_channel *channel,
                                    const THVACValue &hvacValue,
                                    bool *available) {
  if (available != NULL) {
    *available = false;
  }

  bool useCool = channel != NULL && channel->getHvacSubfunctionCool();

  if (useCool) {
    if ((hvacValue.Flags & SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_COOL_SET) == 0) {
      return 0;
    }

    if (available != NULL) {
      *available = true;
    }

    return hvacValue.SetpointTemperatureCool / 100.0;
  }

  if ((hvacValue.Flags & SUPLA_HVAC_VALUE_FLAG_SETPOINT_TEMP_HEAT_SET) == 0) {
    return 0;
  }

  if (available != NULL) {
    *available = true;
  }

  return hvacValue.SetpointTemperatureHeat / 100.0;
}

bool publish_hvac_thermostat_messages_for_channel(client_device_channel *channel) {
  if (channel == NULL) return false;

  THVACValue hvacValue;
  if (!channel->getHvac(&hvacValue)) return false;

  bool published = false;

  if (channel->getCommandTopic().length() > 0) {
    switch (hvacValue.Mode) {
      case SUPLA_HVAC_MODE_OFF:
        publish_to_mqtt_topic(channel->getCommandTopic(),
                              build_hvac_mode_payload(channel, false),
                              channel->getRetain());
        published = true;
        break;
      case SUPLA_HVAC_MODE_HEAT:
      case SUPLA_HVAC_MODE_COOL:
      case SUPLA_HVAC_MODE_CMD_TURN_ON:
        publish_to_mqtt_topic(channel->getCommandTopic(),
                              build_hvac_mode_payload(channel, true),
                              channel->getRetain());
        published = true;
        break;
    }
  }

  bool hasTemperature = false;
  double temperature =
      get_hvac_publish_temperature(channel, hvacValue, &hasTemperature);

  if (hasTemperature &&
      publish_thermostat_preset_message_for_channel(channel, 0, temperature)) {
    published = true;
  }

  return published;
}

bool publish_rgbw_messages_for_channel(client_device_channel *channel) {
  if (channel == NULL || channel->getCommandTopic().length() == 0) return false;

  int color = 0;
  char color_brightness = 0;
  char on_off = 0;
  char brightness = 0;

  if (!channel->getRGBW(&color, &color_brightness, &brightness, &on_off)) {
    return false;
  }

  std::string payload = channel->getCommandTemplate();
  if (payload.length() == 0) {
    payload = "$state$";
  }

  replace_rgbw_placeholders(&payload, channel, color, color_brightness,
                            brightness, on_off);
  publish_to_mqtt_topic(channel->getCommandTopic(), payload,
                        channel->getRetain());
  return true;
}

std::string build_esphome_rgbw_brightness_payload(
    client_device_channel *channel, int brightness255, char on_off) {
  std::ostringstream payload;
  payload << "{\"state\":\"" << get_channel_state_payload(channel, on_off)
          << "\",\"brightness\":" << brightness255 << "}";
  return payload.str();
}

std::string build_esphome_rgbw_color_payload(
    client_device_channel *channel, int red, int green, int blue, int white,
    char on_off) {
  std::ostringstream payload;
  payload << "{\"state\":\"" << get_channel_state_payload(channel, on_off)
          << "\",\"color\":{\"r\":" << red << ",\"g\":" << green
          << ",\"b\":" << blue << ",\"w\":" << white << "}}";
  return payload.str();
}

bool publish_esphome_rgbw_messages_for_channel(client_device_channel *channel) {
  if (channel == NULL || channel->getCommandTopic().length() == 0) return false;

  int color = 0;
  char color_brightness = 0;
  char on_off = 0;
  char brightness = 0;

  if (!channel->getRGBW(&color, &color_brightness, &brightness, &on_off)) {
    return false;
  }

  bool whiteActive =
      (on_off & RGBW_BRIGHTNESS_ONOFF) != 0 && brightness > 0;
  bool colorActive =
      (on_off & RGBW_COLOR_ONOFF) != 0 && color_brightness > 0;

  if (!whiteActive && !colorActive) {
    publish_to_mqtt_topic(
        channel->getCommandTopic(),
        std::string("{\"state\":\"") + get_channel_state_payload(channel, 0) +
            "\"}",
        channel->getRetain());
    return true;
  }

  unsigned char blue = static_cast<unsigned char>(color & 0x000000FF);
  unsigned char green =
      static_cast<unsigned char>((color & 0x0000FF00) >> 8);
  unsigned char red = static_cast<unsigned char>((color & 0x00FF0000) >> 16);

  if (!colorActive) {
    red = 0;
    green = 0;
    blue = 0;
  }

  int white255 = whiteActive ? scale_percentage_to_255(brightness) : 0;
  int colorBrightness255 =
      colorActive ? scale_percentage_to_255(color_brightness) : 0;

  if (colorActive) {
    publish_to_mqtt_topic(
        channel->getCommandTopic(),
        build_esphome_rgbw_brightness_payload(channel, colorBrightness255, 1),
        channel->getRetain());
  }

  publish_to_mqtt_topic(
      channel->getCommandTopic(),
      build_esphome_rgbw_color_payload(channel, red, green, blue, white255, 1),
      channel->getRetain());

  return true;
}

}  // namespace

bool publish_thermostat_power_message_for_channel(client_device_channel *channel,
                                                  unsigned char isOn) {
  if (channel == NULL || channel->getCommandTopic().length() == 0) return false;

  std::string payload =
      isOn > 0 ? channel->getCommandTemplateOn() : channel->getCommandTemplateOff();

  if (payload.length() == 0) {
    payload = channel->getCommandTemplate();
  }

  if (payload.length() == 0) {
    std::string fallbackOn = channel->getPayloadOn();
    std::string fallbackOff = channel->getPayloadOff();

    payload = isOn > 0 ? (fallbackOn.length() > 0 ? fallbackOn : "1")
                       : (fallbackOff.length() > 0 ? fallbackOff : "0");
  }

  replace_string_in_place(&payload, "$value$", std::to_string(isOn > 0 ? 1 : 0));
  replace_string_in_place(&payload, "$state$", std::to_string(isOn > 0 ? 1 : 0));

  publish_to_mqtt_topic(channel->getCommandTopic(), payload, channel->getRetain());
  return true;
}

bool publish_thermostat_preset_message_for_channel(
    client_device_channel *channel, unsigned char index, double temperature) {
  if (channel == NULL ||
      channel->getPresetTemperatureCommandTopic().length() == 0) {
    return false;
  }

  std::string payload = channel->getPresetTemperatureCommandTemplate();
  if (payload.length() == 0) {
    payload = "$value$";
  }

  replace_string_in_place(&payload, "$index$", std::to_string(index));
  replace_string_in_place(&payload, "$value$", format_decimal_value(temperature));
  replace_string_in_place(&payload, "$temperature$",
                          format_decimal_value(temperature));

  publish_to_mqtt_topic(channel->getPresetTemperatureCommandTopic(), payload,
                        channel->getRetain());
  return true;
}

void publish_mqtt_message_for_channel(client_device_channel* channel) {
  std::string topic = channel->getCommandTopic();
  std::string payload = channel->getCommandTemplate();
  std::string payloadOn = channel->getCommandTemplateOn();
  std::string payloadOff = channel->getCommandTemplateOff();

  double value;
  bool publish = false;
  bool publishedInternally = false;
  switch (channel->getFunction()) {
    case SUPLA_CHANNELFNC_THERMOMETER:
    case SUPLA_CHANNELFNC_HUMIDITY:
    case SUPLA_CHANNELFNC_HUMIDITYANDTEMPERATURE: {
      double temp;
      double hum;
      bool isTemp;
      bool isHum;

      channel->getTempHum(&temp, &hum, &isTemp, &isHum);

      if (isTemp)
        replace_string_in_place(&payload, "$temperature$",
                                std::to_string(temp));
      if (isHum)
        replace_string_in_place(&payload, "$humidity$", std::to_string(hum));

      publish = true;

    } break;
    case SUPLA_CHANNELFNC_WINDSENSOR:
    case SUPLA_CHANNELFNC_PRESSURESENSOR:
    case SUPLA_CHANNELFNC_RAINSENSOR:
    case SUPLA_CHANNELFNC_WEIGHTSENSOR:
    case SUPLA_CHANNELFNC_DEPTHSENSOR:
    case SUPLA_CHANNELFNC_DISTANCESENSOR: {
      channel->getDouble(&value);
      replace_string_in_place(&payload, "$value$", std::to_string(value));
      publish = true;
    } break;
    case SUPLA_CHANNELFNC_CONTROLLINGTHEGATEWAYLOCK:
    case SUPLA_CHANNELFNC_CONTROLLINGTHEGATE:
    case SUPLA_CHANNELFNC_CONTROLLINGTHEGARAGEDOOR:
    case SUPLA_CHANNELFNC_CONTROLLINGTHEDOORLOCK: {
      char value[SUPLA_CHANNELVALUE_SIZE];
      channel->getValue(value);
      bool hi = value[0] > 0;

      replace_string_in_place(&payload, "$value$", std::to_string(hi));
      publish = true;
    } break;
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GATEWAY:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GATE:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_GARAGEDOOR:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_DOOR:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_ROLLERSHUTTER:
    case SUPLA_CHANNELFNC_OPENINGSENSOR_WINDOW:
    case SUPLA_CHANNELFNC_MAILSENSOR:
    case SUPLA_CHANNELFNC_NOLIQUIDSENSOR:
    case SUPLA_CHANNELFNC_POWERSWITCH:
    case SUPLA_CHANNELFNC_LIGHTSWITCH:
    case SUPLA_CHANNELFNC_STAIRCASETIMER: {
      char cv[SUPLA_CHANNELVALUE_SIZE];
      channel->getValue(cv);
      bool hi = cv[0] > 0;

      if (hi && payloadOn.length() > 0) {
        payload = payloadOn;
      } else if (payloadOff.length() > 0) {
        payload = payloadOff;
      }
      replace_string_in_place(&payload, "$value$", std::to_string(hi));

      publish = true;
    } break;
    case SUPLA_CHANNELFNC_CONTROLLINGTHEROLLERSHUTTER: {
      if (channel->getEsphomeCover()) {
        publishedInternally = publish_esphome_cover_message_for_channel(channel);
        break;
      }

      char cv[SUPLA_CHANNELVALUE_SIZE];
      channel->getValue(cv);
      char shut = cv[0];
      replace_string_in_place(&payload, "$value$", std::to_string(shut));

      // replace_string_in_place(&payload, "$relay_1$", std::to_string)

      // replace_string_in_place(&payload, "$sensor_1$",
      //                        std::to_string(sub_value[0]));
      // replace_string_in_place(&payload, "$sensor_2$",
      //                        std::to_string(sub_value[1]));

      publish = true;
    } break;
    case SUPLA_CHANNELFNC_THERMOSTAT:
    case SUPLA_CHANNELFNC_THERMOSTAT_HEATPOL_HOMEPLUS: {
      TThermostat_Value thermostatValue;

      if (channel->getThermostat(&thermostatValue)) {
        publishedInternally = publish_thermostat_power_message_for_channel(
            channel, thermostatValue.IsOn);
      }
    } break;
    case SUPLA_CHANNELFNC_HVAC_THERMOSTAT:
      publishedInternally = publish_hvac_thermostat_messages_for_channel(channel);
      break;
    case SUPLA_CHANNELFNC_DIMMER:
    case SUPLA_CHANNELFNC_RGBLIGHTING:
    case SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING: {
      if (channel->getFunction() == SUPLA_CHANNELFNC_DIMMERANDRGBLIGHTING &&
          channel->getEsphomeRgbw()) {
        publishedInternally = publish_esphome_rgbw_messages_for_channel(channel);
      } else {
        publishedInternally = publish_rgbw_messages_for_channel(channel);
      }
    } break;
    case SUPLA_CHANNELFNC_IC_ELECTRICITY_METER:
    case SUPLA_CHANNELFNC_ELECTRICITY_METER: {
      // TSuplaChannelExtendedValue* value =
      // (TSuplaChannelExtendedValue*)malloc(
      //    sizeof(TSuplaChannelExtendedValue));

      //      if (!channel->getExtendedValue(value)) {
      //      free(value);
      //    break;
      //}
      /*
      TElectricityMeter_ExtendedValue em_ev;
      TSC_ImpulseCounter_ExtendedValue ic_ev;

      if (channel->getType() == SUPLA_CHANNELTYPE_IMPULSE_COUNTER) {
        if (srpc_evtool_v1_extended2icextended(value, &ic_ev)) {
          std::string currency(ic_ev.currency, 3);
          std::string custom_unit(ic_ev.custom_unit, 9);

          replace_string_in_place(&payload, "$currency$", currency);
          replace_string_in_place(
              &payload, "$pricePerUnit$",
              std::to_string(ic_ev.price_per_unit * 0.0001));
          replace_string_in_place(&payload, "$totalCost$",
                                  std::to_string(ic_ev.total_cost * 0.01));
          replace_string_in_place(&payload, "$impulsesPerUnit$",
                                  std::to_string(ic_ev.impulses_per_unit));
          replace_string_in_place(&payload, "$counter$",
                                  std::to_string(ic_ev.counter));
          replace_string_in_place(&payload, "$calculatedValue$",
                                  std::to_string(ic_ev.calculated_value));
          replace_string_in_place(&payload, "$unit$", custom_unit);

          publish = true;
        }
      } else if (channel->getType() == SUPLA_CHANNELTYPE_ELECTRICITY_METER) {
        if (srpc_evtool_v1_extended2emextended(value, &em_ev) == 1) {
          std::string currency(em_ev.currency, 3);

          replace_string_in_place(&payload, "$currency$", currency);
          replace_string_in_place(
              &payload, "$pricePerUnit$",
              std::to_string(em_ev.price_per_unit * 0.0001));
          replace_string_in_place(&payload, "$totalCost$",
                                  std::to_string(em_ev.total_cost * 0.01));

          for (int a = 0; a < 3; a++) {
            if (em_ev.m_count > 0 && em_ev.m[0].voltage[a] > 0) {
              std::string stra = std::to_string(a) + "$";
              replace_string_in_place(&payload, "$number_" + stra,
                                      std::to_string(a + 1));
              replace_string_in_place(&payload, "$frequency_" + stra,
                                      std::to_string(em_ev.m[0].freq * 0.01));
              replace_string_in_place(
                  &payload, "$voltage_" + stra,
                  std::to_string(em_ev.m[0].voltage[a] * 0.01));
              replace_string_in_place(
                  &payload, "$current_" + stra,
                  std::to_string(em_ev.m[0].current[a] * 0.001));
              replace_string_in_place(
                  &payload, "$powerActive_" + stra,
                  std::to_string(em_ev.m[0].power_active[a] * 0.00001));
              replace_string_in_place(
                  &payload, "$powerReactive_" + stra,
                  std::to_string(em_ev.m[0].power_reactive[a] * 0.00001));
              replace_string_in_place(
                  &payload, "$powerApparent_" + stra,
                  std::to_string(em_ev.m[0].power_apparent[a] * 0.00001));
              replace_string_in_place(
                  &payload, "$powerFactor_" + stra,
                  std::to_string(em_ev.m[0].power_factor[a] * 0.001));
              replace_string_in_place(
                  &payload, "$phaseAngle_" + stra,
                  std::to_string(em_ev.m[0].phase_angle[a] * 0.1));
              replace_string_in_place(
                  &payload, "$totalForwardActiveEnergy_" + stra,
                  std::to_string(em_ev.total_forward_active_energy[a] *
                                 0.00001));
              replace_string_in_place(
                  &payload, "$totalReverseActiveEnergy_" + stra,
                  std::to_string(em_ev.total_reverse_active_energy[a] *
                                 0.00001));
              replace_string_in_place(
                  &payload, "$totalForwardReactiveEnergy_" + stra,
                  std::to_string(em_ev.total_forward_reactive_energy[a] *
                                 0.0001));
              replace_string_in_place(
                  &payload, "$totalReverseReactiveEnergy_" + stra,
                  std::to_string(em_ev.total_reverse_reactive_energy[a] *
                                 0.0001));
            }
          }

          publish = true;
        }

      }
      free(value);*/
    } break;
  }
  if (publishedInternally) {
    return;
  }

  if (publish) {
    publish_to_mqtt_topic(topic, payload, channel->getRetain());
  }
}

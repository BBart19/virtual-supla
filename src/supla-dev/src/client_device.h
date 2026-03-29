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

#ifndef CLIENT_DEVICE_H
#define CLIENT_DEVICE_H

#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "device.h"
#include "supla-client-lib/devicechannel.h"
#include "supla-client-lib/log.h"
#include "supla-client-lib/safearray.h"

class client_device_channel {
 private:
  int type;
  int function;
  int number;
  int intervalSec;
  int fileWriteCheckSeck;
  int toggleSec;
  std::string fileName;
  std::string payloadOn;
  std::string payloadOff;
  std::string payloadValue;
  std::string stateTopic;
  std::string commandTopic;
  std::string temperatureTopic;
  std::string humidityTopic;
  std::string voltageTopic;
  std::string voltageTopicL2;
  std::string voltageTopicL3;
  std::string currentTopic;
  std::string currentTopicL2;
  std::string currentTopicL3;
  std::string powerTopic;
  std::string powerTopicL2;
  std::string powerTopicL3;
  std::string energyTopic;
  std::string energyTopicL2;
  std::string energyTopicL3;
  std::string frequencyTopic;
  std::string reactivePowerTopic[3];
  std::string apparentPowerTopic[3];
  std::string powerFactorTopic[3];
  std::string phaseAngleTopic[3];
  std::string returnedEnergyTopic[3];
  std::string inductiveEnergyTopic[3];
  std::string capacitiveEnergyTopic[3];
  std::string brightnessTopic;
  std::string colorBrightnessTopic;
  std::string colorTopic;
  std::string measuredTemperatureTopic;
  std::string presetTemperatureTopic;
  std::string positionCommandTopic;
  std::string presetTemperatureCommandTopic;
  std::string stateTemplate;
  std::string commandTemplate;
  std::string commandTemplateOn;
  std::string commandTemplateOff;
  std::string presetTemperatureCommandTemplate;
  std::string execute;
  std::string executeOn;
  std::string executeOff;

  std::string idTemplate;
  std::string idValue;

  bool retain;
  bool online;
  bool toggled;
  bool batteryPowered;
  bool invertState;
  bool esphomeCover;
  bool esphomeRgbw;
  bool hvacSubfunctionCool;
  bool generalNoSpaceBeforeValue;
  bool generalNoSpaceAfterValue;
  bool generalKeepHistory;
  unsigned char hvacMainThermometerChannelNo;
  unsigned char generalValuePrecision;
  unsigned char generalChartType;
  unsigned char batteryLevel;
  _supla_int_t generalValueDivider;
  _supla_int_t generalValueMultiplier;
  _supla_int64_t generalValueAdded;
  unsigned short generalRefreshIntervalMs;
  std::string generalUnitBeforeValue;
  std::string generalUnitAfterValue;
  int rememberedRgbwColor;
  char rememberedRgbwColorBrightness;
  char rememberedRgbwBrightness;

  void *lck;
  struct timeval last;
  char value[SUPLA_CHANNELVALUE_SIZE];
  TSuplaChannelExtendedValue *extendedValue;

  bool isSensorNONC(void);

 public:
  client_device_channel(int number);
  ~client_device_channel();

  /* properties */
  int getType(void);
  int getFunction(void);
  int getNumber(void);
  int getIntervalSec(void);
  int getToggleSec(void);
  std::string getFileName(void);
  std::string getPayloadOn(void);
  std::string getPayloadOff(void);
  std::string getPayloadValue(void);
  std::string getStateTopic(void);
  std::string getCommandTopic(void);
  std::string getTemperatureTopic(void);
  std::string getHumidityTopic(void);
  std::string getVoltageTopic(void);
  std::string getVoltageTopic(unsigned char phase);
  std::string getCurrentTopic(void);
  std::string getCurrentTopic(unsigned char phase);
  std::string getPowerTopic(void);
  std::string getPowerTopic(unsigned char phase);
  std::string getEnergyTopic(void);
  std::string getEnergyTopic(unsigned char phase);
  std::string getFrequencyTopic(void);
  std::string getReactivePowerTopic(unsigned char phase);
  std::string getApparentPowerTopic(unsigned char phase);
  std::string getPowerFactorTopic(unsigned char phase);
  std::string getPhaseAngleTopic(unsigned char phase);
  std::string getReturnedEnergyTopic(unsigned char phase);
  std::string getInductiveEnergyTopic(unsigned char phase);
  std::string getCapacitiveEnergyTopic(unsigned char phase);
  std::string getBrightnessTopic(void);
  std::string getColorBrightnessTopic(void);
  std::string getColorTopic(void);
  std::string getMeasuredTemperatureTopic(void);
  std::string getPresetTemperatureTopic(void);
  std::string getPositionCommandTopic(void);
  std::string getPresetTemperatureCommandTopic(void);
  std::string getStateTemplate(void);
  std::string getCommandTemplate(void);
  std::string getCommandTemplateOn(void);
  std::string getCommandTemplateOff(void);
  std::string getPresetTemperatureCommandTemplate(void);
  std::string getExecute(void);
  std::string getExecuteOn(void);
  std::string getExecuteOff(void);
  std::string getIdTemplate(void);
  std::string getIdValue(void);
  bool getRetain(void);
  bool getInvertState(void);
  bool getEsphomeCover(void);
  bool getEsphomeRgbw(void);
  bool getHvacSubfunctionCool(void);
  unsigned char getHvacMainThermometerChannelNo(void);
  _supla_int_t getGeneralValueDivider(void);
  _supla_int_t getGeneralValueMultiplier(void);
  _supla_int64_t getGeneralValueAdded(void);
  unsigned char getGeneralValuePrecision(void);
  std::string getGeneralUnitBeforeValue(void);
  std::string getGeneralUnitAfterValue(void);
  bool getGeneralNoSpaceBeforeValue(void);
  bool getGeneralNoSpaceAfterValue(void);
  bool getGeneralKeepHistory(void);
  unsigned char getGeneralChartType(void);
  unsigned short getGeneralRefreshIntervalMs(void);
  bool getOnline(void);
  bool getToggled(void);
  long getLastSeconds(void);
  int getFileWriteCheckSec(void);

  void setType(int type);
  void setFunction(int function);
  void setNumber(int number);
  void setIntervalSec(int interval);
  void setToggleSec(int interval);
  void setFileName(const char *filename);
  void setPayloadOn(const char *payloadOn);
  void setPayloadOff(const char *payloadOff);
  void setPayloadValue(const char *payloadValue);
  void setStateTopic(const char *stateTopic);
  void setCommandTopic(const char *commandTopic);
  void setTemperatureTopic(const char *temperatureTopic);
  void setHumidityTopic(const char *humidityTopic);
  void setVoltageTopic(const char *voltageTopic);
  void setVoltageTopic(unsigned char phase, const char *voltageTopic);
  void setCurrentTopic(const char *currentTopic);
  void setCurrentTopic(unsigned char phase, const char *currentTopic);
  void setPowerTopic(const char *powerTopic);
  void setPowerTopic(unsigned char phase, const char *powerTopic);
  void setEnergyTopic(const char *energyTopic);
  void setEnergyTopic(unsigned char phase, const char *energyTopic);
  void setFrequencyTopic(const char *frequencyTopic);
  void setReactivePowerTopic(unsigned char phase, const char *topic);
  void setApparentPowerTopic(unsigned char phase, const char *topic);
  void setPowerFactorTopic(unsigned char phase, const char *topic);
  void setPhaseAngleTopic(unsigned char phase, const char *topic);
  void setReturnedEnergyTopic(unsigned char phase, const char *topic);
  void setInductiveEnergyTopic(unsigned char phase, const char *topic);
  void setCapacitiveEnergyTopic(unsigned char phase, const char *topic);
  void setBrightnessTopic(const char *brightnessTopic);
  void setColorBrightnessTopic(const char *colorBrightnessTopic);
  void setColorTopic(const char *colorTopic);
  void setMeasuredTemperatureTopic(const char *measuredTemperatureTopic);
  void setPresetTemperatureTopic(const char *presetTemperatureTopic);
  void setPositionCommandTopic(const char *positionCommandTopic);
  void setPresetTemperatureCommandTopic(
      const char *presetTemperatureCommandTopic);
  void setStateTemplate(const char *stateTemplate);
  void setCommandTemplate(const char *commandTemplate);
  void setCommandTemplateOn(const char *commandTemplateOn);
  void setCommandTemplateOff(const char *commandTemplateOff);
  void setPresetTemperatureCommandTemplate(
      const char *presetTemperatureCommandTemplate);
  void setExecute(const char *execute);
  void setExecuteOn(const char *execute);
  void setExecuteOff(const char *execute);
  void setRetain(bool retain);
  void setInvertState(bool invertState);
  void setEsphomeCover(bool esphomeCover);
  void setEsphomeRgbw(bool esphomeRgbw);
  void setHvacSubfunctionCool(bool hvacSubfunctionCool);
  void setHvacMainThermometerChannelNo(unsigned char channelNo);
  void setGeneralValueDivider(_supla_int_t value);
  void setGeneralValueMultiplier(_supla_int_t value);
  void setGeneralValueAdded(_supla_int64_t value);
  void setGeneralValuePrecision(unsigned char value);
  void setGeneralUnitBeforeValue(const char *value);
  void setGeneralUnitAfterValue(const char *value);
  void setGeneralNoSpaceBeforeValue(bool value);
  void setGeneralNoSpaceAfterValue(bool value);
  void setGeneralKeepHistory(bool value);
  void setGeneralChartType(unsigned char value);
  void setGeneralRefreshIntervalMs(unsigned short value);
  void setOnline(bool online);
  void setToggled(bool toggled);
  void setLastSeconds(void);
  void setFileWriteCheckSec(int value);
  void setIdTemplate(const char *idTemplate);
  void setIdValue(const char *idValue);
  bool getExtendedValue(TSuplaChannelExtendedValue *ev);
  void setExtendedValue(TSuplaChannelExtendedValue *ev);

  /* value handler */
  void getValue(char value[SUPLA_CHANNELVALUE_SIZE]);
  void getDouble(double *result);
  void getTempHum(double *temp, double *humidity, bool *isTemperature,
                  bool *isHumidity);
  bool getRGBW(int *color, char *color_brightness, char *brightness,
               char *on_off);
  void getRememberedRGBW(int *color, char *color_brightness, char *brightness);
  char getChar(void);

  void setValue(char value[SUPLA_CHANNELVALUE_SIZE]);
  void setDouble(double value);
  void setTempHum(double temp, double humidity);
  void setRGBW(int color, char color_brightness, char brightness, char on_off);
  void setChar(char value);
  bool getThermostat(TThermostat_Value *thermostatValue);
  bool getHvac(THVACValue *hvacValue);
  void updateThermostatState(bool hasOn, bool on, bool hasMeasuredTemperature,
                             double measuredTemperature,
                             bool hasPresetTemperature,
                             double presetTemperature);
  void updateHvacState(bool hasOn, bool on, bool hasSetpointTemperature,
                       double setpointTemperature);
  void transformIncomingState(char value[SUPLA_CHANNELVALUE_SIZE]);

  void toggleValue(void);
  bool isBatteryPowered(void);
  unsigned char getBatteryLevel(void);

  void setBatteryLevel(unsigned char level);
  void setBatteryPowered(bool value);
};

class client_device_channels {
 private:
  bool initialized;
  void *arr;

 public:
  client_device_channels();
  ~client_device_channels();
  client_device_channel *add_channel(int number);

  client_device_channel *find_channel(int number);
  client_device_channel *find_channel_by_topic(std::string topic);
  void get_channels_for_topic(std::string topic,
                              std::vector<client_device_channel *> *vect);

  client_device_channel *getChannel(int idx);
  int getChannelCount(void);

  void getMqttSubscriptionTopics(std::vector<std::string> *vect);

  bool getInitialized(void);
  void setInitialized(bool initialized);

  _func_channelio_valuechanged on_valuechanged;
  _func_channelio_extendedValueChanged on_extendedValueChanged;
  void *on_valuechanged_user_data;
};

#endif

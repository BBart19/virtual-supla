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

#ifndef CHANNEL_IO_H_
#define CHANNEL_IO_H_

#include <string.h>

#include "client_device.h"
#include "client_publisher.h"
#include "client_subscriber.h"
#include "devcfg.h"
#include "device.h"
#include "mqtt/mqtt_client.h"
#include "supla-client-lib/proto.h"
#include "w1.h"

//#ifdef __cplusplus
// extern "C" {
//#endif

char channelio_init(void);
void channelio_free(void);
void channelio_channel_init(void);
int channelio_channel_count(void);
int channelio_get_type(unsigned char number);
int channelio_get_function(unsigned char number);

void channelio_set_type(unsigned char number, int type);
void channelio_set_function(unsigned char number, int function);
void channelio_set_filename(unsigned char number, const char *value);
void channelio_set_payload_on(unsigned char number, const char *value);
void channelio_set_payload_off(unsigned char number, const char *value);
void channelio_set_payload_value(unsigned char number, const char *value);
void channelio_set_execute(unsigned char number, const char *value);
void channelio_set_interval(unsigned char number, int interval);
void channelio_set_execute_on(unsigned char number, const char *value);
void channelio_set_execute_off(unsigned char number, const char *value);
void channelio_set_toggle(unsigned char number, int toggle);
void channelio_set_file_write_check(unsigned char number, int value);
void channelio_set_id_template(unsigned char number, const char *value);
void channelio_set_id_value(unsigned char number, const char *value);
void channelio_set_gpio1(unsigned char number, unsigned char gpio1);
void channelio_set_gpio2(unsigned char number, unsigned char gpio2);
void channelio_set_bistable_flag(unsigned char number, unsigned char bistable);
void channelio_set_w1(unsigned char number, const char *w1);
void channelio_set_mcp23008_driver(unsigned char number, int driver);
void channelio_set_mcp23008_addr(unsigned char number, unsigned char addr);
void channelio_set_mcp23008_reset(unsigned char number, unsigned char reset);
void channelio_set_mcp23008_gpio_dir(unsigned char number, unsigned char value);
void channelio_set_mcp23008_gpio_val(unsigned char number, unsigned char value);
void channelio_set_mcp23008_gpio_port(unsigned char number, unsigned char port);
void channelio_set_mqtt_topic_in(unsigned char number, const char *value);
void channelio_set_mqtt_topic_out(unsigned char number, const char *value);
void channelio_set_mqtt_temperature_topic_in(unsigned char number,
                                             const char *value);
void channelio_set_mqtt_humidity_topic_in(unsigned char number,
                                          const char *value);
void channelio_set_mqtt_voltage_topic_in(unsigned char number,
                                         const char *value);
void channelio_set_mqtt_voltage_topic_phase_in(unsigned char number,
                                               unsigned char phase,
                                               const char *value);
void channelio_set_mqtt_current_topic_in(unsigned char number,
                                         const char *value);
void channelio_set_mqtt_current_topic_phase_in(unsigned char number,
                                               unsigned char phase,
                                               const char *value);
void channelio_set_mqtt_power_topic_in(unsigned char number,
                                       const char *value);
void channelio_set_mqtt_power_topic_phase_in(unsigned char number,
                                             unsigned char phase,
                                             const char *value);
void channelio_set_mqtt_energy_topic_in(unsigned char number,
                                        const char *value);
void channelio_set_mqtt_energy_topic_phase_in(unsigned char number,
                                              unsigned char phase,
                                              const char *value);
void channelio_set_mqtt_frequency_topic_in(unsigned char number,
                                           const char *value);
void channelio_set_mqtt_reactive_power_topic_phase_in(unsigned char number,
                                                      unsigned char phase,
                                                      const char *value);
void channelio_set_mqtt_apparent_power_topic_phase_in(unsigned char number,
                                                      unsigned char phase,
                                                      const char *value);
void channelio_set_mqtt_power_factor_topic_phase_in(unsigned char number,
                                                    unsigned char phase,
                                                    const char *value);
void channelio_set_mqtt_phase_angle_topic_phase_in(unsigned char number,
                                                   unsigned char phase,
                                                   const char *value);
void channelio_set_mqtt_returned_energy_topic_phase_in(unsigned char number,
                                                       unsigned char phase,
                                                       const char *value);
void channelio_set_mqtt_inductive_energy_topic_phase_in(unsigned char number,
                                                        unsigned char phase,
                                                        const char *value);
void channelio_set_mqtt_capacitive_energy_topic_phase_in(unsigned char number,
                                                         unsigned char phase,
                                                         const char *value);
void channelio_set_mqtt_brightness_topic_in(unsigned char number,
                                            const char *value);
void channelio_set_mqtt_color_brightness_topic_in(unsigned char number,
                                                  const char *value);
void channelio_set_mqtt_color_topic_in(unsigned char number,
                                       const char *value);
void channelio_set_mqtt_measured_temperature_topic_in(unsigned char number,
                                                      const char *value);
void channelio_set_mqtt_preset_temperature_topic_in(unsigned char number,
                                                    const char *value);
void channelio_set_mqtt_position_topic_out(unsigned char number,
                                           const char *value);
void channelio_set_mqtt_preset_temperature_topic_out(unsigned char number,
                                                     const char *value);

void channelio_set_mqtt_template_in(unsigned char number, const char *value);
void channelio_set_mqtt_template_out(unsigned char number, const char *value);
void channelio_set_mqtt_template_on_out(unsigned char number,
                                        const char *value);
void channelio_set_mqtt_template_off_out(unsigned char number,
                                         const char *value);
void channelio_set_mqtt_preset_temperature_template_out(unsigned char number,
                                                        const char *value);

void channelio_set_mqtt_retain(unsigned char number, unsigned char value);
void channelio_set_invert_state(unsigned char number, unsigned char value);
void channelio_set_esphome_cover(unsigned char number, unsigned char value);
void channelio_set_esphome_rgbw(unsigned char number, unsigned char value);
void channelio_set_hvac_subfunction(unsigned char number, const char *value);
void channelio_set_hvac_main_thermometer_channel(unsigned char number,
                                                 unsigned char channelNo);
void channelio_set_general_value_divider(unsigned char number,
                                         _supla_int_t value);
void channelio_set_general_value_multiplier(unsigned char number,
                                            _supla_int_t value);
void channelio_set_general_value_added(unsigned char number,
                                       _supla_int64_t value);
void channelio_set_general_value_precision(unsigned char number,
                                           unsigned char value);
void channelio_set_general_unit_before_value(unsigned char number,
                                             const char *value);
void channelio_set_general_unit_after_value(unsigned char number,
                                            const char *value);
void channelio_set_general_no_space_before_value(unsigned char number,
                                                 unsigned char value);
void channelio_set_general_no_space_after_value(unsigned char number,
                                                unsigned char value);
void channelio_set_general_keep_history(unsigned char number,
                                        unsigned char value);
void channelio_set_general_chart_type(unsigned char number,
                                      unsigned char value);
void channelio_set_general_refresh_interval_ms(unsigned char number,
                                               unsigned short value);
void channelio_set_battery_powered(unsigned char number, unsigned char value);

void channelio_get_value(unsigned char number,
                         char value[SUPLA_CHANNELVALUE_SIZE]);
char channelio_set_value(unsigned char number, char hi[SUPLA_CHANNELVALUE_SIZE],
                         unsigned int time_ms);
char channelio_handle_calcfg_request(unsigned char number,
                                     TSD_DeviceCalCfgRequest *request);
unsigned char channelio_required_proto_version(void);
void channelio_reset_runtime_config_tracking(void);
void channelio_send_initial_configs_if_needed(void *srpc);
char channelio_handle_runtime_channel_config(
    TSDS_SetChannelConfig *request, TSDS_SetChannelConfigResult *result);
char channelio_handle_runtime_device_config(
    TSDS_SetDeviceConfig *request, TSDS_SetDeviceConfigResult *result);

void channelio_channels_to_srd_c(unsigned char *channel_count,
                                 TDS_SuplaDeviceChannel_C *channels);
void channelio_channels_to_srd_b(unsigned char *channel_count,
                                 TDS_SuplaDeviceChannel_B *channels);

void channelio_get_channel_state(unsigned char number,
                                 TDSC_ChannelState *state);

void mqtt_subscribe_callback(void **state,
                             struct mqtt_response_publish *publish);

const char **channelio_channels_get_topics(int *count);

void channelio_channels_set_mqtt_callback(void (*subscribe_response_callback)(
    const char *topic, const char *payload, char retain, char qos));

void channelio_raise_mqtt_valuechannged(client_device_channel *channel);
void channelio_raise_execute_command(client_device_channel *channel);
void channelio_raise_valuechanged(client_device_channel *channel);

// TMP TEST
void tmp_channelio_raise_valuechanged(unsigned char number);

void channelio_setcalback_on_channel_value_changed(
    _func_channelio_valuechanged on_valuechanged,
    _func_channelio_extendedValueChanged on_extendedValueChanged,
    void *user_data);

#ifdef __SINGLE_THREAD
void channelio_iterate(void);
#endif

//#ifdef __cplusplus/
//}
//#endif

#endif /* CHANNEL_IO_H_ */

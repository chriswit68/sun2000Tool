#pragma once

namespace sun2000::registers {

inline constexpr int inverter_model_name = 30000;
inline constexpr int inverter_model_name_register_count = 15;
inline constexpr int inverter_serial_number = 30015;
inline constexpr int inverter_serial_number_register_count = 10;
inline constexpr int inverter_firmware_version = 30035;
inline constexpr int inverter_firmware_version_register_count = 15;
inline constexpr int inverter_model_id = 30070;
inline constexpr int inverter_pv_string_count = 30071;

inline constexpr int pv_string_1_voltage = 32016;
inline constexpr int pv_string_register_stride = 2;

inline constexpr int inverter_input_power = 32064;
inline constexpr int inverter_line_voltage_ab = 32066;
inline constexpr int inverter_line_voltage_bc = 32067;
inline constexpr int inverter_line_voltage_ca = 32068;
inline constexpr int inverter_phase_voltage_a = 32069;
inline constexpr int inverter_phase_voltage_b = 32070;
inline constexpr int inverter_phase_voltage_c = 32071;
inline constexpr int inverter_phase_current_a = 32072;
inline constexpr int inverter_phase_current_b = 32074;
inline constexpr int inverter_phase_current_c = 32076;
inline constexpr int inverter_active_power = 32080;
inline constexpr int inverter_reactive_power = 32082;
inline constexpr int inverter_power_factor = 32084;
inline constexpr int inverter_grid_frequency = 32085;
inline constexpr int inverter_efficiency = 32086;
inline constexpr int inverter_internal_temperature = 32087;
inline constexpr int inverter_insulation_resistance = 32088;
inline constexpr int inverter_device_status = 32089;
inline constexpr int inverter_fault_code = 32090;
inline constexpr int inverter_active_power_fast = 32095;
inline constexpr int inverter_accumulated_yield_energy = 32106;
inline constexpr int inverter_daily_yield_energy = 32114;
inline constexpr int inverter_monthly_yield_energy = 32116;

inline constexpr int battery_unit_1_status = 37000;
inline constexpr int battery_unit_1_power = 37001;
inline constexpr int battery_unit_1_bus_voltage = 37003;
inline constexpr int battery_unit_1_soc = 37004;
inline constexpr int battery_unit_1_working_mode = 37006;
inline constexpr int battery_unit_1_temperature = 37022;
inline constexpr int battery_soc = 37760;
inline constexpr int battery_power = 37765;
inline constexpr int battery_total_charge = 37780;

}  // namespace sun2000::registers

#include <imgui.h>

#include "pinmapping.h"
#include "Heater.h"

constexpr double absolute_zero_offset = -273.15;
double thermistor_ext_coef[] = {
  7.611428226793945e-04,
  2.011100481838449e-04,
  1.914201231699539e-06,
  1.561937567632929e-08
};

double temperature_to_resistance(double t) {
	double r, u, v, p, q, b, c, d;
	t = t - absolute_zero_offset;
	d = (thermistor_ext_coef[0] - 1.0 / t) / thermistor_ext_coef[3];
	c = thermistor_ext_coef[1] / thermistor_ext_coef[3];
	b = thermistor_ext_coef[2] / thermistor_ext_coef[3];
	q = 2.0 / 27.0 * b * b * b - 1.0 / 3.0 * b * c + d;
	p = c - 1.0 / 3.0 * b * b;
	v = - pow(q / 2.0 + sqrt(q * q / 4.0 + p * p * p / 27.0), 1.0 / 3.0);
	u =   pow(-q / 2.0 + sqrt(q * q / 4.0 + p * p * p / 27.0), 1.0 / 3.0);
	r  = exp(u + v - b / 3.0);
	return r;
}

Heater::Heater(pin_type heater_pin, pin_type adc_pin, heater_data heater, hotend_data hotend, adc_data adc) : VirtualPrinter::Component("Heater"), heater_pin(heater_pin), adc_pin(analogInputToDigitalPin(adc_pin)) {
  heater_resistance = heater.resistance;
  heater_volts = heater.voltage;

  hotend_mass = hotend.mass;
  hotend_surface_area = hotend.surface_area;
  hotend_specific_heat = hotend.specific_heat;

  adc_resolution = adc.resolution;
  adc_pullup_resistance = adc.pullup_resistance;

  Gpio::attach(this->adc_pin, [this](GpioEvent& event){ this->interrupt(event); });
  Gpio::attach(this->heater_pin, [this](GpioEvent& event){ this->interrupt(event); });
  hotend_energy = hotend_ambient_temperature * (hotend_specific_heat * hotend_mass);
  hotend_temperature = hotend_ambient_temperature;
}

Heater::~Heater() {
}

void Heater::update() {

}

void Heater::ui_widget() {
  ImGui::Text("Temperature: %f", hotend_temperature);
}

// models energy transfer but not time lag as it tranfers through the medium.
void Heater::interrupt(GpioEvent& ev) {
  if (ev.event == ev.RISE && ev.pin_id == heater_pin) {
    if (pwm_hightick) pwm_period = ev.timestamp - pwm_hightick;
    pwm_hightick = ev.timestamp;

  } else if ((ev.event == ev.NOP || ev.event == ev.FALL) && ev.pin_id == heater_pin) {
    double time_delta = Kernel::TimeControl::ticksToNanos(ev.timestamp - pwm_last_update) / (double)Kernel::TimeControl::ONE_BILLION;
    double energy_in = pwm_lowtick < pwm_hightick ? ((heater_volts * heater_volts) / heater_resistance) * time_delta : 0;
    double energy_out = ((hotend_convection_transfer * hotend_surface_area * ( hotend_energy / (hotend_specific_heat * hotend_mass) - hotend_ambient_temperature)) * time_delta);
    hotend_energy += energy_in - energy_out;
    pwm_last_update = ev.timestamp;

    hotend_temperature = hotend_energy / (hotend_specific_heat * hotend_mass);

    if (ev.event == ev.FALL) {
      pwm_lowtick = ev.timestamp;
      pwm_duty = ev.timestamp - pwm_hightick;
    }

  } else if (ev.event == ev.GET_VALUE && ev.pin_id == adc_pin) {
    double thermistor_resistance = temperature_to_resistance(hotend_temperature);
    uint32_t adc_reading = (uint32_t)((((1U << adc_resolution) -1)  * thermistor_resistance) / (adc_pullup_resistance + thermistor_resistance));
    Gpio::set_pin_value(adc_pin, adc_reading);
  }
}
/**
 * rpi.h
 *
 * Copyright (c) 2017 Ed Alegrid <ealegrid@gmail.com>
 * GNU General Public License v3.0
 *
 */

/* Defines for RPI */
#ifndef RPI_H
#define RPI_H

#include <stdint.h>

#define RPI_VERSION 100 

#ifdef __cplusplus
extern "C" {
#endif


/* Initialize RPI library */ 
extern void rpi_init(int access); 

/* Close RPI library */ 
extern uint8_t rpi_close();

/**
 *  Time Delays
 */
void nswait(uint64_t ns);  //nanosecond time delay

void uswait(uint32_t us);  //microsecond time delay

void mswait(uint32_t ms);  //millisecond time delay

/**
 *  GPIO
 */
void gpio_config(uint8_t pin, uint8_t mode);

uint8_t get_gpio_config(uint8_t pin);

void gpio_input(uint8_t pin);

void gpio_output(uint8_t pin);

uint8_t gpio_read(uint8_t pin); 

uint8_t gpio_write(uint8_t pin, uint8_t bit);

void gpio_on(uint8_t pin);

void gpio_off(uint8_t pin);

void gpio_pulse(uint8_t pin, int td);

void gpio_reset_all_events(uint8_t pin);

void gpio_enable_high_event(uint8_t pin, uint8_t bit);

void gpio_enable_low_event(uint8_t pin, uint8_t bit);

void gpio_enable_rising_event(uint8_t pin, uint8_t bit);

void gpio_enable_falling_event(uint8_t pin, uint8_t bit);

void gpio_enable_async_rising_event(uint8_t pin, uint8_t bit);

void gpio_enable_async_falling_event(uint8_t pin, uint8_t bit);

uint8_t gpio_detect_input_event(uint8_t pin);

void gpio_reset_event(uint8_t pin);

void gpio_enable_pud(uint8_t pin, uint8_t value);

/**
 *  PWM
 */
void pwm_set_pin(uint8_t pin);

void pwm_reset_pin(uint8_t pin);

void pwm_reset_all_pins();

uint8_t pwm_set_clock_freq(uint32_t divider);

uint8_t pwm_clk_status();

void pwm_enable(uint8_t pin, uint8_t n);

void pwm_set_mode(uint8_t pin, uint8_t n);

void pwm_set_pola(uint8_t pin, uint8_t n);

void pwm_set_data(uint8_t pin, uint32_t data);

void pwm_set_range(uint8_t pin, uint32_t range);

/**
 *  I2C
 */
int i2c_start();

int i2c_init(uint8_t value);

void i2c_stop();

void i2c_select_slave(uint8_t addr); 

void i2c_set_clock_freq(uint16_t divider);

void i2c_data_transfer_speed(uint32_t baud);

uint8_t i2c_write(const char * wbuf, uint8_t len);

uint8_t i2c_read(char * rbuf, uint8_t len);

uint8_t i2c_byte_read();

/**
 *  SPI
 */
int spi_start();

void spi_stop();

void spi_set_clock_freq(uint16_t divider);

void spi_set_data_mode(uint8_t mode);

void spi_set_chip_select_polarity(uint8_t cs, uint8_t active);

void spi_chip_select(uint8_t cs);

void spi_data_transfer(char* wbuf, char* rbuf, uint8_t len);

void _spi_write(char* wbuf, uint8_t len);

void _spi_read(char* rbuf, uint8_t len);


#ifdef __cplusplus
}
#endif

#endif /* RPI_H */
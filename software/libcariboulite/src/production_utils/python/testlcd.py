#!/usr/bin/env python
# encoding: utf8

'''testlcd.py - test application for the USBLCD interface
'''

import random
import time
import usb1

from lcd2usb import LCD

lcd = LCD()

def list_usb():
    '''list all available usb devices'''

    context = usb1.USBContext()
    devices = context.getDeviceList()
    for device in devices:
        vendor = device.getVendorID()
        product = device.getProductID()
        print('ID %04x:%04x' % (vendor, product))
        bus = device.getBusNumber()
        print('->Bus %03i' % bus)
        dev = device.getDeviceAddress()
        print('Device', dev)
        try:
            print(device.getProduct())
            print('by', device.getManufacturer())
        except:
            pass
        print()


ECHO_NUM = 100


def lcd_echo(lcd):
    '''send a number of 16 bit words to the USBLCD interface
    and verify that they are correctly returned by the echo
    command. This may be used to check the reliability of
    the usb interfacing'''

    errors = 0
    for _ in range(ECHO_NUM):
        val = random.randint(0, 0xffff)
        ret = lcd.echo(val)
        if val != ret:
            errors += 1

    if errors:
        print('ERROR: %d out of %d echo transfers failed!' % (errors,
                                                              ECHO_NUM))
    else:
        print('Echo test successful!')


def lcd_get_version(lcd):
    '''get lcd2usb interface firmware version'''

    major, minor = lcd.version
    print('Firmware version %d.%d' % (major, minor))


def lcd_get_controller(lcd):
    '''get the bit mask of installed LCD controllers

    0 = no lcd found, 1 = single controller display, 3 = dual
    controller display'''

    if not lcd.ctrl0 and not lcd.ctrl1:
        print('No controllers installed!')
    else:
        print('Installed controllers:')
        if lcd.ctrl0:
            print('CTRL0')
        if lcd.ctrl1:
            print('CTRL1')
        print()


def lcd_get_keys(lcd):
    '''get state of the two optional buttons'''

    key1, key2 = lcd.keys
    print('Keys: S1:%s S2:%s' % (key1 and 'on' or 'off',
                               key2 and 'on' or 'off'))


def main():
    print('--      USBLCD test application       --')

    list_usb()
    #lcd = LCD.find_or_die()

    # make lcd interface return some bytes to test transfer reliability
    lcd_echo(lcd)

    # read some values from adaptor
    lcd_get_version(lcd)
    lcd_get_controller(lcd)
    lcd_get_keys(lcd)

    # adjust contrast and brightess
    lcd.set_contrast(190)
    lcd.set_brightness(255)

    # clear display
    lcd.clear()

    lcd.goto(0, 0)
    lcd.write("Hello CaribouLite!")
    lcd.goto(0, 1)
    lcd.write("Hello CaribouLite!")
    time.sleep(2.0)
    lcd.clear()

    # write something on the screen
    """
    msg = ('                '
           'The quick brown fox jumps over the lazy dogs back ...'
           '                ')
    for i in range(len(msg) - 15):
        lcd.home()
        lcd.write(msg[i:i + 16])  # write 16 chars to display
        time.sleep(0.1)

    # have some fun with the brightness
    for i in range(256)[::-1]:
        lcd.set_brightness(i)
        time.sleep(0.01)

    lcd.clear()
    lcd.write('Bye bye!!!')

    for i in range(256):
        lcd.set_brightness(i)
        time.sleep(0.01)
    """
    
    lcd.close()


if __name__ == '__main__':
    main()

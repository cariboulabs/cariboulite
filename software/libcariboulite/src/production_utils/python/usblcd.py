from lcd2usb import LCD
from subprocess import *
import threading
from time import sleep, strftime
from datetime import datetime

# ===========================================================================
# Clock Example
# ===========================================================================
lcd = LCD()

cmd = "ip addr show eth0 | grep inet | awk '{print $2}' | cut -d/ -f1"

print ("Press CTRL+Z to exit")

def run_cmd(cmd):
    p = Popen(cmd, shell=True, stdout=PIPE)
    output = p.communicate()[0]
    return output

while(True):
    lcd.clear()
    lcd.set_contrast(190)
    lcd.set_brightness(255)
    ipaddr = run_cmd(cmd)
    lcd.goto(0,0)
    lcd.write(datetime.now().strftime('%b %d  %H:%M:%S\n'))
    lcd.goto(0,1)
    lcd.write('IP %s' % (ipaddr))
    sleep(1)

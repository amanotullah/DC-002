#! /usr/bin/env python3

import time
try:
    import serial
except:
    print("Run the following command to install pyserial:\nsudo easy_install -U pyserial")


def getTime():
    return int(time.time()) - time.timezone

print("Setting Time...")
ser = serial.Serial('/dev/tty.usbmodem123451', 9600)
initialTime = getTime()
while (initialTime == getTime()):
    1+1
    #spin

bytestring = 'T{}'.format(getTime())
success = ser.write(bytestring.encode('utf_8'))
ser.flush()
ser.close()

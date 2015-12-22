#! /usr/bin/env python

import time
try:
    import serial
except:
    print "Run the following command to install pyserial:\nsudo easy_install -U pyserial"


def getTime():
    return int(time.time()) - time.timezone

print "Setting Time..."
ser = serial.Serial('/dev/tty.usbmodem12341', 9600)
initialTime = getTime()
while (initialTime == getTime()):
    1+1
    #spin

success = ser.write('T'+str(getTime()))
ser.flush()
ser.close()

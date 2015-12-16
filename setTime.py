#! /usr/bin/env python

import serial
import time

def getTime():
    return int(time.time()) - time.timezone

print "Setting Time..."
ser = serial.Serial('/dev/tty.usbmodem12341', 9600)
initialTime = getTime()
while (initialTime == getTime()):
    1+1
    #spin

success = ser.write('T'+str(getTime()))
if (success) :
    print "Time Set!"
else:
    print "Setting Time Failed"

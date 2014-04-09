#!/usr/bin/python2.7

from bluetooth import *
import time
import sys

class Dancarino:
    def __init__(self, addr, name):
        self.socket = BluetoothSocket(RFCOMM)
        self.socket.connect((addr, 1))
        
        self.name = name

    def __del__(self):
        self.socket.close()

    def test_mode(self):
        self.socket.send('T')
    
    def reset(self):
        self.socket.send('R')
    
    def analog_write(self, wire, value):
        assert(wire in (0, 1))
        assert(0 <= value <= 255)
        
        self.socket.send('A%c%c' % (chr(wire), chr(value)))
    
    def digital_write(self, wire, value):
        assert(wire in (0, 1))
        value = 1 if value else 0

        self.socket.send('D%c%c' % (chr(wire), chr(value)))

    def toggle_led(self):
        self.socket.send('L')


print 'Discovering devices'
devices = discover_devices(lookup_names=True)

print 'Found %d devices' % len(devices)
for addr, name in devices:
    print 'Trying to connect to %s (%s)...' % (name, addr)
    
    dancarino = Dancarino(addr, name)

    while True:
        print '>>>',
        cmd = raw_input().strip()
        if cmd == 'help':
            print 'Commands: test, reset, analog wire value, digital wire value, led, exit'
            print ' wire: 0 or 1'
            print ' values for analog: 0-255'
            print ' values for digital: 0 = off, anything else: on'
        elif cmd == 'test':
            dancarino.test_mode()
        elif cmd == 'reset':
            dancarino.reset()
        elif cmd.startswith('analog '):
            params = cmd.split(' ')
            dancarino.analog_write(int(params[1]), int(params[2]))
        elif cmd.startswith('digital '):
            params = cmd.split(' ')
            dancarino.digital_write(int(params[1]), int(params[2]))
        elif cmd == 'led':
            dancarino.toggle_led()
        elif cmd.startswith('fade'):
            params = cmd.split(' ')
            wire = int(params[1])
            t = 4.0
            try:
                t = float(params[2])
            except:
                pass

            r = None
            if cmd.startswith('fadein'):
                r = range(0, 255, 5)
            else:
                r = range(255, 0, -5)
            
            for i in r:
                print '---', i
                dancarino.analog_write(wire, i)
                time.sleep(5 * (t / 255.))
        elif cmd == 'exit':
            break
        elif cmd:
            print 'Unknown command:', cmd

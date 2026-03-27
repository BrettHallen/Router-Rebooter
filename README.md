# Router Rebooter
Simple device to reboot my router when the NBN (National Broadband Network) stops working.<br>

This is typically when we're away from home for more than a week which means that the RING cameras & WiFi-enabled lights & powerpoints (to make it look like we're home) stop working.<br>

Yes, yes, I shouldn't be using cloud-enabled devices which will get bricked at the manufacturer's whim.  I'll work on it.<br>

And yes, there are devices that do this - they seemed a bit pricey.<br>

## Idea
The broadband router I have is a Netcomm Wireless NF18ACV that runs on 12VDC.<br>

I'll use an ESP32-type device to ping a reliable Internet address every minute, say a well-known DNS address like 1.1.1.1.<br>

If there is no response for, say, five consecutive pings then I'll energise the relay and power off the router.<br>

After one minute I'll de-energise the relay and power the router back on.<br>

I'll wait five minutes before resuming pinging.  If pinging remains unsuccessful 10min after powering back on then I'll repeat the process.<br>

## YouTube Videos
- [Part 1: Initial build & test](https://youtu.be/Fz1aSylL9KQ)

## Firmware (Arduino Sketch)
Tested.<br>
Possible improvements:
- Round robin or random selection of target IP address from a list - DONE
- Use the ESP32-C3 dev board's inbuilt LED for ... something
- Send a message after the router has been rebooted (via CallMeBot to WhatsApp?)
- Reduce ESP32 power usage by only connecting to WiFi for ping test, then disconnecting? Or not worth the hassle ... ?

## Parts
- OMRON GL5E-1-VD relay (or similar)
- ESP32-C3-Zero development board (Waveshare)
- 10µF capacitor for the ESP32 power
- One 7805-type 5V regulator (i.e. [EzSBC's PSU2-5](https://ezsbc.shop/products/psu2-5-5v-1amp-three-pin-regulator)) to power the ESP. I am using a switching regulator so don't have any provision for heatsinking a linear regulator.
- Two 2.1mm DC power sockets to pass through the 12VDC
- Some other bits (2N2222 transistor, 1KΩ resistor, 1N4148 diode) to connect the relay coil to the ESP32
- 2-pin header to disconnect 5V regulator when ESP32 is connected to USB for testing to avoid connecting connecting two power supplies
- Four M3 screws to mount it somehow

I've sized the PCB (62x49mm) to fit an 83x54x31mm Jiffy Box [(Jaycar HB6015)](https://www.jaycar.com.au/jiffy-box-black-83-x-54-x-31mm/p/HB6015).<br>

Waveshare ESP32-C3-Zero symbol/footprint/3D model from:<br>
https://github.com/jonathanadams/KiCad-Waveshare-ESP32

![Router Rebooter 3D](/Images/Broadband_Router_Rebooter_3D.png)

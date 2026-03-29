# Router Rebooter
Simple device to reboot my router when the NBN (National Broadband Network) stops working.<br>

This is typically when we're away from home for more than a week which means that the RING cameras & WiFi-enabled lights & powerpoints (to make it look like we're home) stop working.<br>

Yes, yes, I shouldn't be using cloud-enabled devices which will get bricked at the manufacturer's whim.  I'll work on it.<br>

And yes, there are devices that do this - they seemed a bit pricey.<br>

## Idea
The broadband router I have is a Netcomm Wireless NF18ACV that runs on 12VDC.  We got it when we originally had FTTN (fibre to the node) which runs optical fibre to a street cabinet/node and then uses the decades-old telephone copper cabling to the house.  Where we live now has FTTP (fibre to the premises) where the optical fibre runs straight to the house.  The same router still works fine so have never replaced it.<br>

I'll use an ESP32-type device to ping a reliable Internet address every minute, say a well-known DNS address like 1.1.1.1.  The ESP32 will be powered from the 12V input supply via a 5V regulator.  The power to the router will run through an NC (normally closed) relay.<br>

If there is no response for a few consecutive pings then I'll energise the relay which switches to open and disconnects the 12V to the router.<br>

After a short delay I'll then de-energise the relay and power the router back on.<br>

After another short delay whilst the router reconnects (2-3min) I'll reconnect to the WiFi network and resume pinging.

## YouTube Videos
- [Part 1: Initial build & test](https://youtu.be/Fz1aSylL9KQ)

## Examples
This is how it works using the serial port output - here we simulate the loss of Internet by using dummy IP addresses:<br>
```
>> Brett's Router Rebooter Starting!
   (28/Mar/2026)
>> Timer/retry settings (DEBUG):
   Ping timer .......... 10s
   Ping retries ........ 3
   Ping retry timer .... 5s
   Power cycle timer ... 30s
   Recovery delay ...... 2min
>> Hardware initialised: Router on, OK LED on, FAILUIRE LED off, Ping LED off.
>> Connecting to WiFi network <RouterRebootTest> ...
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
>> WiFi status = 3 (successfully connected!)
>> Connected to <RouterRebootTest> with address <192.168.1.36>.
>> Pinging 1.1.1.1 ... OK!
>> Pinging 192.168.123.123 ... failed!
>> Pinging 1.0.0.1 ... OK!
>> Pinging 192.168.123.123 ... failed!
>> Pinging 192.168.123.123 ... failed!
>> Pinging 192.168.123.123 ... failed!
>> Pinging 192.168.123.123 ... failed!
!! No response after retries, disconnecting router power.
   Waiting 30s before reconnecting power ...
   30s
   25s
   20s
   15s
   10s
   5s
>> Powering router back on.
>> Starting recovery wait ...
   Waiting 2min
   Waiting 1min
>> Router recovery finished – restarting monitoring.


>> Connecting to WiFi network <RouterRebootTest> ...
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
>> WiFi status = 3 (successfully connected!)
>> Connected to <RouterRebootTest> with address <192.168.1.36>.
>> Pinging 1.1.1.1 ... OK!

... and so on ...
```
And this is what it does when the Internet is fine, which should be 99.9% of the time:
```
>> Brett's Router Rebooter Starting!
   (28/Mar/2026)
>> Timer/retry settings:
   Ping timer .......... 30s
   Ping retries ........ 5
   Ping retry timer .... 15s
   Power cycle timer ... 30s
   Recovery delay ...... 3min
>> Hardware initialised: Router on, OK LED on, FAILUIRE LED off, Ping LED off.
>> Connecting to WiFi network <RouterRebootTest> ...
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
!! WiFi status = 0 (idle, not attempting to connect)
>> WiFi status = 3 (successfully connected!)
>> Connected to <RouterRebootTest> with address <192.168.1.36>.
>> Pinging 1.1.1.1 ... OK!
>> Pinging 1.0.0.1 ... OK!
>> Pinging 8.8.8.8 ... OK!
>> Pinging 8.8.4.4 ... OK!
>> Pinging 9.9.9.9 ... OK!
>> Pinging 149.112.112.112 ... OK!
>> Pinging 1.1.1.1 ... OK!
```

## Firmware (Arduino Sketch)
Use the Arduino IDE to compile & transfer to the ESP32.<br>

### Versions
- [Tested version](/Router-Rebooter)
- [Experimental version](/Experimental_version)

Experimental version: added static IP address and HTTP status/statistics page

![Status page](/Images/Router_Rebooter_status_page.jpg)

### Required Libraries
- [ESPping library](https://github.com/dvarrel/ESPping)
- [Adafruit NeoPixel](https://github.com/adafruit/adafruit_neopixel)

### Possible Improvements
- Round robin or random selection of target IP address from a list - DONE!
- Use the ESP32-C3 dev board's inbuilt LED for ... something - DONE!
- Send a message after the router has been rebooted (via CallMeBot to WhatsApp?)
- Reduce ESP32 power usage by only connecting to WiFi for ping test, then disconnecting? Or not worth the hassle ... ?

## LED Status
There are three LEDs: the OK LED (suggested green), the FAILURE LED (suggested red) and the ESP32's built-in LED (RGB).<br>

| OK LED   | FAILURE LED | ESP32 LED           | Meaning                        |
|----------|-------------|---------------------|--------------------------------|
| On       | Off         | Green/blue flashing | Connecting to WiFi             |
| On       | Off         | Off                 | Everything ok, waiting to ping |
| On       | Off         | Blue pulse          | Pinging                        |
| On       | On          | Red                 | Ping failed                    |
| Off      | On          | Off                 | Ping retries failed            |
| Off      | On          | Green flashing      | Router power cycling           |
| Flashing | On          | Off                 | Waiting for router to recover  |   

## Hardware
There's currently two versions:
- [Rev. B](/KiCad/RevB) based on my original design with 2.1mm DC sockets and vertically mounted regulator 
- [Rev. C](/KiCad/RevC) removes the 2.1mm DC sockets and lies the 5V regulator down 

### Parts
- OMRON GL5E-1-VD relay (or similar)
- ESP32-C3-Zero development board (Waveshare)
- 10µF capacitor for the ESP32 power
- One 7805-type 5V regulator (i.e. [EzSBC's PSU2-5](https://ezsbc.shop/products/psu2-5-5v-1amp-three-pin-regulator)) to power the ESP. I am using a switching regulator so don't have any provision for heatsinking a linear regulator.
- Two 2.1mm DC power sockets to pass through the 12VDC
- Some other bits (2N2222 transistor, 1KΩ resistor, 1N4148 diode) to connect the relay coil to the ESP32
- 2-pin header to disconnect 5V regulator when ESP32 is connected to USB for testing to avoid connecting two power supplies
- Four M3 screws to mount it somehow

I've sized the PCB (62x49mm) to fit an 83x54x31mm Jiffy Box [(Jaycar HB6015)](https://www.jaycar.com.au/jiffy-box-black-83-x-54-x-31mm/p/HB6015).<br>

Waveshare ESP32-C3-Zero symbol/footprint/3D model from:<br>
https://github.com/jonathanadams/KiCad-Waveshare-ESP32

![Router Rebooter 3D](/Images/Broadband_Router_Rebooter_3D.png)

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
   (2/Apr/2026)
>> Monitoring NF18ACV
>> Timer/retry settings (DEBUG):
   Ping timer .......... 10s
   Ping retries ........ 3
   Ping retry timer .... 5s
   Power cycle timer ... 30s
   Recovery delay ...... 120s
>> Hardware initialised: Router on, OK LED on, FAILURE LED off, Ping LED off.
!! [handleHttpClients] WiFi not connected, ignoring HTTP requests.
>> Connecting to WiFi network <RouterRebootTest> ...
>> Using static IP address <192.168.128.129>
>> WiFi status = 3 (successfully connected!)
>> Connected to <RouterRebootTest> with address <192.168.128.129>
>> HTTP status page started on port 80
   Open in browser: http://192.168.128.129
>> Syncing NTP time (pool.ntp.org) ... 02/Apr/2026 18:53:07 (AEDT)
>> Pinging 1.1.1.1          Cloudflare primary   ... OK!
>> Pinging 192.168.123.123  Dummy test failure   ... failed!
>> Pinging 1.0.0.1          Cloudflare secondary ... OK!
>> Pinging 192.168.123.123  Dummy test failure   ... failed!
>> Pinging 192.168.123.123  Dummy test failure   ... failed!
>> Pinging 192.168.123.123  Dummy test failure   ... failed!
>> Pinging 192.168.123.123  Dummy test failure   ... failed!
!! No response after retries, disconnecting router power.
>> Waiting 30s before reconnecting power ...
   30s
   25s
   20s
   15s
   10s
   5s
>> Powering router back on.
>> Waiting 120s before resuming monitoring ...
   120s
   105s
   90s
   75s
   60s
   45s
   30s
   15s
>> Router recovery finished – resuming monitoring.

... and so on ...
```
And this is what it does when the Internet is fine, which should be 99.9% of the time:
```


>> Brett's Router Rebooter Starting!
   (2/Apr/2026)
>> Monitoring NF18ACV
>> Timer/retry settings:
   Ping timer .......... 1s
   Ping retries ........ 5
   Ping retry timer .... 15s
   Power cycle timer ... 30s
   Recovery delay ...... 180s
>> Hardware initialised: Router on, OK LED on, FAILURE LED off, Ping LED off.
!! [handleHttpClients] WiFi not connected, ignoring HTTP requests.
>> Connecting to WiFi network <RouterRebootTest> ...
>> Using static IP address <192.168.128.129>
>> WiFi status = 3 (successfully connected!)
>> Connected to <RouterRebootTest> with address <192.168.128.129>
>> HTTP status page started on port 80
   Open in browser: http://192.168.128.129
>> Syncing NTP time (pool.ntp.org) ... 02/Apr/2026 20:26:19 (AEDT)
>> Pinging 1.1.1.1          Cloudflare primary   ... OK!
>> Pinging 1.0.0.1          Cloudflare secondary ... OK!
>> Pinging 8.8.8.8          Google primary       ... OK!
>> Pinging 8.8.4.4          Google secondary     ... OK!
>> Pinging 9.9.9.9          Quad9 primary        ... OK!
>> Pinging 149.112.112.112  Quad9 secondary      ... OK!
>> Pinging 4.2.2.1          Level 3 primary      ... OK!
>> Pinging 4.2.2.2          Level 3 secondary    ... OK!
>> Pinging 4.2.2.3          Level 3 tertiary     ... OK!
>> Pinging 208.67.222.222   OpenDNS primary      ... OK!
>> Pinging 208.67.220.220   OpenDNS secondary    ... OK!
>> Pinging 1.1.1.1          Cloudflare primary   ... OK!
>> Pinging 1.0.0.1          Cloudflare secondary ... OK!
>> Pinging 8.8.8.8          Google primary       ... OK!
>> Pinging 8.8.4.4          Google secondary     ... OK!

... and so on ...
```

## Firmware (Arduino Sketch)
Use the Arduino IDE to compile & transfer to the ESP32.<br>

### Versions
- [Tested version](/Router-Rebooter)
- [Experimental version](/Experimental_version)

Experimental version - seems to be working fine, testing ongoing:<br>
- added TZ/DST handling, including automatic DST change according to local rule

![Status page](/Images/Router_Rebooter_status_page.png)

### Required Libraries
- [ESPping](https://github.com/dvarrel/ESPping)
- [Adafruit NeoPixel](https://github.com/adafruit/adafruit_neopixel)
- [Timezone](https://github.com/JChristensen/Timezone)

### Possible Improvements
- Send a message after the router has been rebooted (via CallMeBot to WhatsApp?)

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
- [Rev. B](/KiCad/RevB) minor corrections of my original design with 2.1mm DC sockets and vertically mounted regulator 
- [Rev. C](/KiCad/RevC) removes the 2.1mm DC sockets and lies the 5V regulator down 

### Parts
- OMRON GL5E-1-VD relay (or similar)
- ESP32-C3-Zero development board (Waveshare)
- 10µF capacitor for the ESP32 power - could probably be left off (short pins)
- One 7805-type 5V regulator (i.e. [EzSBC's PSU2-5](https://ezsbc.shop/products/psu2-5-5v-1amp-three-pin-regulator)) to power the ESP. I am using a switching regulator so don't have any provision for heatsinking a linear regulator.
- Two 2.1mm DC power sockets to pass through the 12VDC
- Some other bits (2N2222 transistor, 1KΩ resistor, 1N4148 diode) to connect the relay coil to the ESP32
- 2-pin header to disconnect 5V regulator when ESP32 is connected to USB for testing to avoid connecting two power supplies
- Four M3 screws to mount it somehow

I've sized the PCB (62x49mm) to fit an 83x54x31mm Jiffy Box [(Jaycar HB6015)](https://www.jaycar.com.au/jiffy-box-black-83-x-54-x-31mm/p/HB6015).<br>

Waveshare ESP32-C3-Zero symbol/footprint/3D model from:<br>
https://github.com/jonathanadams/KiCad-Waveshare-ESP32

## Images
### Rev A. version built
The green OK LED indicates normal operation of the router and the blue ESP32 LED indicates it's currently sending a test ping to test reachability.

![Rev A under test](/Images/Broadband_Router_Rebooter_RevA.png)

### Rev. C version 3D rendering
![Router Rebooter 3D](/Images/Broadband_Router_Rebooter_3D.png)

/*********************************************************************
 * Router Rebooter for ESP32-C3-Zero                                 *
 *                                                                   *
 * Brett Hallen, 26/Mar/2026, initial                                *
 *               27/Mar/2026, round-robin pinging                    *
 *               28/Mar/2026, ESP32 LED                              *     
 *                                                                   *
 * HARDWARE MAPPING:                                                 *
 *   Relay control: GPIO 5 (pin 9) via 2N2222 transistor             *
 *                  active HIGH to energise                          *
 *   OK LED:        GPIO 0 (pin 4)                                   *
 *   FAILURE LED:   GPIO 1 (pin 5)                                   *
 *                                                                   *
 * RELAY BEHAVIOR:                                                   *
 *   NC contact used, coil de-energised most of the time (router on) *
 *   Only energise coil when we need to cut power (reboot)           *
 *                                                                   *
 * LED BEHAVIOUR:                                                    *
 *   OK LED:      steady ON when router is okay                      *
 *   FAILURE LED: ON when pinging has failed                         *
 *   Recovery:    OK LED flashes every second                        *
 *********************************************************************/

/*
 Improvement Ideas:
 [1] Round robin or random selection of DNS address from a list - DONE!
 [2] Use the ESP32-C3 dev board's inbuilt LED                   - DONE!
 [3] Send message after router has been rebooted 
     (via CallMeBot to WhatsApp?)
 [4] Reduce ESP32 power usage by only connecting to WiFi for    
     ping test, then disconnecting?                             - NO
*/

/* Uncomment for debug/testing \/\/\/\/\/\/\/\/\/ */
// #define DEBUG_MODE // testing settings or normal
/* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\ */
#define RELAY_ENERGISE   HIGH     // Energise coil = cut power to router (NO contact)
#define RELAY_DEENERGISE LOW      // De-energise coil = restore power (NC contact)

#include <WiFi.h>
#include <ESPping.h>
#include <Adafruit_NeoPixel.h>   // Required for the WS2812 RGB LED on the Waveshare ESP32-C3-Zero

/* WiFi configuration \/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/  */
const char* ssid     = "RouterRebootTest";  // Your WiFi AP here
const char* password = "Today123!";         // Your WiFi password here
/* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\ */

#ifdef DEBUG_MODE
  // Round-robin DNS list (cycles through all target IPs)
  const IPAddress dnsList[] = 
  {
    IPAddress(1, 1, 1, 1),      // Cloudflare primary
    IPAddress(192,168,123,123), // dummy to test failure
    IPAddress(1, 0, 0, 1),      // Cloudflare secondary
    IPAddress(192,168,123,123), // dummy to test failure
    IPAddress(192,168,123,123), // dummy to test failure
    IPAddress(192,168,123,123), // dummy to test failure
    IPAddress(192,168,123,123)  // dummy to test failure
  };
  /* Timers for testing */
  const int PING_TIMER       = 10000; // delay between normal pings (ms)
  const int PING_RETRIES     = 3;     // retry ping before failing
  const int PING_RETRY_TIMER = 5000;  // delay between retries (ms)
  const int POWER_CYCLE_TIME = 30;    // delay for power cycling (s)
  const int RECOVERY_DELAY   = 2;     // delay after powering back on (min)
#else
  // Round-robin DNS list (cycles through all target IPs)
  // Add to or modify the list as you wish!
  const IPAddress dnsList[] = 
  {
    IPAddress(1, 1, 1, 1),        // Cloudflare primary
    IPAddress(1, 0, 0, 1),        // Cloudflare secondary
    IPAddress(8, 8, 8, 8),        // Google primary
    IPAddress(8, 8, 4, 4),        // Google secondary
    IPAddress(9, 9, 9, 9),        // Quad9 primary
    IPAddress(149, 112, 112, 112) // Quad9 secondary
  };
  /* Timers */
  const int PING_TIMER       = 30000; // delay between normal pings (ms)
  const int PING_RETRIES     = 5;     // retry ping before failing
  const int PING_RETRY_TIMER = 15000; // delay between retries (ms)
  const int POWER_CYCLE_TIME = 30   ; // delay for power cycling (s)
  const int RECOVERY_DELAY   = 3;     // delay after powering back on (min)
#endif
const int NUM_DNS = sizeof(dnsList) / sizeof(dnsList[0]);
int currentIPIndex = 0;     // round-robin index (automatically advances after every ping)

/* GPIO pins */
const int RELAY_PIN        = 5;   // GPIO5, active HIGH via 2N2222
const int OK_LED_PIN       = 0;   // GPIO0
const int FAILURE_LED_PIN  = 1;   // GPIO1
const int BUILTIN_LED_PIN  = 10;  // WS2812 RGB LED on GPIO10 (Waveshare ESP32-C3-Zero)

/* NeoPixel object for the built-in RGB LED */
Adafruit_NeoPixel pixels(1, BUILTIN_LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() 
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n>> Brett's Router Rebooter Starting!");
  Serial.println("   (28/Mar/2026)");
#ifdef DEBUG_MODE
  Serial.println(">> Timer/retry settings (DEBUG):");
#else
  Serial.println(">> Timer/retry settings:");
#endif
  Serial.print("   Ping timer .......... "); Serial.print(PING_TIMER/1000); Serial.println("s");
  Serial.print("   Ping retries ........ "); Serial.println(PING_RETRIES);
  Serial.print("   Ping retry timer .... "); Serial.print(PING_RETRY_TIMER/1000); Serial.println("s");
  Serial.print("   Power cycle timer ... "); Serial.print(POWER_CYCLE_TIME); Serial.println("s");
  Serial.print("   Recovery delay ...... "); Serial.print(RECOVERY_DELAY); Serial.println("min");

  pinMode(RELAY_PIN,        OUTPUT);
  pinMode(OK_LED_PIN,       OUTPUT);
  pinMode(FAILURE_LED_PIN,  OUTPUT);

  // Safe boot state
  digitalWrite(RELAY_PIN,       RELAY_DEENERGISE);   // Router powered (coil off)
  digitalWrite(OK_LED_PIN,      HIGH);               // OK LED on
  digitalWrite(FAILURE_LED_PIN, LOW);                // FAILURE LED off

  pixels.begin();                                    // Initialise the WS2812 RGB LED
  pixels.setBrightness(80);                          // Nice visible brightness (0-255)
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));    // Built-in LED OFF at boot
  pixels.show();

  Serial.println(">> Hardware initialised: Router on, OK LED on, FAILUIRE LED off, Ping LED off.");
}

/* Connect to the configuraed WiFi AP and get an IP address */
void connectToWiFi() 
{
  Serial.print(">> Connecting to WiFi network <");
  Serial.print(ssid);
  Serial.print("> .");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    // Alternate ESP32 LED green/blue whilst waiting to connect to WiFi
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    delay(250);
    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
    pixels.show();
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("   Connected to <");
  Serial.print(ssid);
  Serial.print("> with address <");
  Serial.print(WiFi.localIP());
  Serial.println(">.");
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

}

/* Ping the well-known IP address to verify we can talk to the Internet */
bool performPing() 
{
  // Set ESP32 LED to blue when the PING is sent
  pixels.setPixelColor(0, pixels.Color(0, 0, 255));
  pixels.show();

  IPAddress currentTarget = dnsList[currentIPIndex];   // round-robin selection

  Serial.print(">> Pinging ");
  Serial.print(currentTarget);
  Serial.print(" ... ");

  if (Ping.ping(currentTarget, 1)) 
  {   // Single ping
    Serial.println("OK!");
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;   // advance to next IP for next ping
    // Turn off ESP32 LED when response is received
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    return true;
  } 
  else 
  {
    Serial.println("failed!");
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;   // still advance even on failure
    // Set ESP32 LED to red on failed ping
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();
    return false;
  }
}

/* Normal state: OK LED on, FAILURE LED off, ESP32 LED off */
void setNormalState() 
{
  digitalWrite(OK_LED_PIN,      HIGH);
  digitalWrite(FAILURE_LED_PIN, LOW);
}

/* Failure state: OK LED off, FAILURE LED on, ESP32 LED off */
void setFailureState() 
{
  digitalWrite(OK_LED_PIN,      LOW);
  digitalWrite(FAILURE_LED_PIN, HIGH);
}

/* Main loop de loop here */
void loop() 
{
  connectToWiFi();   // Reconnect after every power-cycle

  while (true) {
    // Safety check
    if (WiFi.status() != WL_CONNECTED) 
    {
      Serial.println("!! WiFi lost – reconnecting...");
      break;
    }

    // Normal monitoring
    if (performPing()) 
    {
      setNormalState();      // OK LED on, FAILURE off
      delay(PING_TIMER);     // Wait some time
      continue;
    }

    // Ping failed, set FAILURE LED
    setFailureState();

    // Retry ping a few times
    bool recovered = false;
    for (int i = 0; i < PING_RETRIES; i++) 
    {
      delay(PING_RETRY_TIMER);
      if (performPing()) 
      {
        recovered = true;
        break;
      }
    }

    if (recovered) 
    {
      setNormalState();
      delay(PING_TIMER);
      continue;
    }

    // Still dead after retries, power cycle
    Serial.println("!! No response after retries, disconnecting router power.");
    digitalWrite(RELAY_PIN, RELAY_ENERGISE);   // Energise relay, disconnect power
    // Turn off ESP32 LED when response is received
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();

    Serial.print("   Waiting ");
    Serial.print(POWER_CYCLE_TIME);
    Serial.println("s before reconnecting power.");
    for (int seconds = POWER_CYCLE_TIME; seconds > 0; seconds--) 
    {
      Serial.print("   ");
      Serial.print(seconds);
      Serial.println("s");
      // Flash ESP32 LED during wait
      pixels.setPixelColor(0, pixels.Color(255, 0, 0));
      pixels.show();
      delay(500);                  // 0.5s
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
      delay(500);                  // 0.5s
    }
    Serial.println(">> Powering router back on.");
    digitalWrite(RELAY_PIN, RELAY_DEENERGISE); // De-energise relay, reconnect power

    // Recovery countdown with flashing OK LED
    Serial.println(">> Starting recovery wait (OK LED flashing).");
    for (int minutes = RECOVERY_DELAY; minutes > 0; minutes--) 
    {
      Serial.print("   Waiting ");
      Serial.print(minutes);
      Serial.println("min");

      // Flash OK LED for the full minute
      for (int i = 0; i < 60; i++) 
      {
        digitalWrite(OK_LED_PIN, HIGH);
        delay(500); // 0.5s
        digitalWrite(OK_LED_PIN, LOW);
        delay(500); // 0.5s
      }
    }

    Serial.println(">> Router recovery finished – restarting monitoring");
    Serial.println();
    Serial.println();

    setNormalState();   // Back to normal state

    // reconnect WiFi
    WiFi.disconnect(true);
    delay(2000);
    break;
  }
}
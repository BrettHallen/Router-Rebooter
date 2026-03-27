/*********************************************************************
 * Router Rebooter for ESP32-C3-Zero                                 *
 *                                                                   *
 * Brett Hallen, 26/Mar/2026                                         *
 *               27/Mar/2026                                         *
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
 [1] Round robin or random selection of DNS address from a list - DONE
 [2] Use the ESP32-C3 dev board's inbuilt LED
 [3] Send message after router has been rebooted 
     (via CallMeBot to WhatsApp?)
 [4] Reduce ESP32 power usage by only connecting to WiFi for 
     ping test, then disconnecting?
*/

#include <WiFi.h>
#include <ESPping.h>

/* Configuration - CUSTOMISE HERE \/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
const char* ssid     = "RouterRebootTest";  // Your WiFi AP here
const char* password = "Today123!";         // Your WiFi password here
/* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */

// Round-robin DNS list 
const IPAddress dnsList[] = {
  IPAddress(1, 1, 1, 1),        // Cloudflare primary
  IPAddress(1, 0, 0, 1),        // Cloudflare secondary
  IPAddress(8, 8, 8, 8),        // Google primary
  IPAddress(8, 8, 4, 4),        // Google secondary
  IPAddress(9, 9, 9, 9),        // Quad9 primary
  IPAddress(149, 112, 112, 112) // Quad9 secondary
};
const int NUM_DNS = sizeof(dnsList) / sizeof(dnsList[0]);
int currentIPIndex = 0;     // round-robin index (automatically advances after every ping)

/* Timers for testing
const int PING_TIMER       = 15000; // delay being normal pings
const int PING_RETRIES     = 3;     // retry ping before failing
const int PING_RETRY_TIMER = 5000; // delay between retries
const int POWER_CYCLE_TIME = 30000; // delay for power cycling
const int RECOVERY_DELAY   = 2;     // how many minutes after power cycling before retrying
*/

/* Timers */
const int PING_TIMER       = 60000; // delay being normal pings
const int PING_RETRIES     = 5;     // retry ping before failing
const int PING_RETRY_TIMER = 10000; // delay between retries
const int POWER_CYCLE_TIME = 30000; // delay for power cycling
const int RECOVERY_DELAY   = 3;     // how many minutes after power cycling before retrying

/* GPIO pins */
const int RELAY_PIN        = 5;   // GPIO5, active HIGH via 2N2222
const int OK_LED_PIN       = 0;   // GPIO0
const int FAILURE_LED_PIN  = 1;   // GPIO1

#define RELAY_ENERGISE   HIGH     // Energise coil = cut power to router (NO contact)
#define RELAY_DEENERGISE LOW      // De-energise coil = restore power (NC contact)

void setup() 
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n>> Brett's Router Rebooter Starting!");

  pinMode(RELAY_PIN,        OUTPUT);
  pinMode(OK_LED_PIN,       OUTPUT);
  pinMode(FAILURE_LED_PIN,  OUTPUT);

  // Safe boot state
  digitalWrite(RELAY_PIN,       RELAY_DEENERGISE);   // Router powered (coil off)
  digitalWrite(OK_LED_PIN,      HIGH);               // OK LED on
  digitalWrite(FAILURE_LED_PIN, LOW);

  Serial.println(">> Hardware initialised: Router on, OK LED on, FAILURE LED off.");
}

/* Connect to the configured WiFi AP and get an IP address */
void connectToWiFi() 
{
  Serial.print(">> Connecting to WiFi network <");
  Serial.print(ssid);
  Serial.print("> .");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("   Connected to <");
  Serial.print(ssid);
  Serial.print("> with address ");
  Serial.println(WiFi.localIP());
}

/* Ping the well-known IP address to verify we can talk to the Internet */
bool performPing() 
{
  IPAddress currentTarget = dnsList[currentIPIndex];   // round-robin selection

  Serial.print(">> Pinging ");
  Serial.print(currentTarget);
  Serial.print(" ... ");

  if (Ping.ping(currentTarget, 1)) 
  {   // Single ping
    Serial.println("OK!");
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;   // advance to next IP for next ping
    return true;
  } 
  else 
  {
    Serial.println("failed!");
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;   // still advance even on failure
    return false;
  }
}

/* Normal state: OK LED on, FAILURE LED off */
void setNormalState() 
{
  digitalWrite(OK_LED_PIN,      HIGH);
  digitalWrite(FAILURE_LED_PIN, LOW);
}

/* Failure state: OK LED off, FAILURE LED on */
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
    for (int i = 0; i < PING_RETRIES; i++) {
      delay(PING_RETRY_TIMER);
      if (performPing()) {
        recovered = true;
        break;
      }
    }

    if (recovered) {
      setNormalState();
      delay(PING_TIMER);
      continue;
    }

    // Still dead after retries, power cycle
    Serial.println("!! No response after retries, disconnecting router power.");
    digitalWrite(RELAY_PIN, RELAY_ENERGISE);   // Energise relay, disconnect power

    Serial.println("   Waiting 30s before reconnecting power.");
    delay(POWER_CYCLE_TIME);                  // Delay before powering back on

    Serial.println("   Powering router back on.");
    digitalWrite(RELAY_PIN, RELAY_DEENERGISE); // De-energise relay, reconnect power

    // Recovery countdown with flashing OK LED
    Serial.println(">> Starting recovery wait (OK LED flashing).");
    for (int minutes = RECOVERY_DELAY; minutes > 0; minutes--) {
      Serial.print("   Waiting ");
      Serial.print(minutes);
      Serial.println("min");

      // Flash OK LED for the full minute (1 Hz)
      for (int i = 0; i < 60; i++) {
        digitalWrite(OK_LED_PIN, HIGH);
        delay(500);
        digitalWrite(OK_LED_PIN, LOW);
        delay(500);
      }
    }

    Serial.println(">> Router recovery finished – restarting monitoring.");
    Serial.println();
    Serial.println();

    setNormalState();   // Back to normal state

    // reconnect WiFi
    WiFi.disconnect(true);
    delay(2000);
    break;
  }
}
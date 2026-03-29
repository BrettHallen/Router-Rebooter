/*********************************************************************
 * Router Rebooter for ESP32-C3-Zero                                 *
 *                                                                   *
 * Brett Hallen, 26/Mar/2026, initial                                *
 *               27/Mar/2026, round-robin pinging                    *
 *               28/Mar/2026, ESP32 LED                              * 
 *               29/Mar/2026, Static IP, HTTP stats/status page      *
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
 [5] Status/statistics via HTTP page                            - DONE!
*/

/* Uncomment for debug/testing \/\/\/\/\/\/\/\/\/\/\ */
//#define DEBUG_MODE   /* testing settings or normal */
/* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */

/* Static IP address \/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
#define STATIC_IP    /* assign yourself an IP address for easy access */
/* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\ */

#define RELAY_ENERGISE   HIGH     // Energise coil = cut power to router (NO contact)
#define RELAY_DEENERGISE LOW      // De-energise coil = restore power (NC contact)

#include <WiFi.h>
#include <ESPping.h>
#include <Adafruit_NeoPixel.h>   // Required for the WS2812 RGB LED on the Waveshare ESP32-C3-Zero

/* Modify WiFi configuration \/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\ */
const char* ssid     = "RouterRebootTest";  /* Your WiFi AP here       */
const char* password = "Today123!";         /* Your WiFi password here */
/* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */

#ifdef STATIC_IP
const IPAddress staticIP(192, 168, 128, 129);   /* your static IP address */
const IPAddress gateway(192, 168, 1, 1);        /* your router/gateway IP address */
const IPAddress subnet(255, 255, 0, 0);         /* your netmask */
const IPAddress dns(1, 1, 1, 1);                /* DNS IP address */
#endif

#ifdef DEBUG_MODE
  // Round-robin DNS list (DEBUG mode – includes dummy IPs to force failures)
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
  // Round-robin DNS list (normal production mode)
  const IPAddress dnsList[] = 
  {
    IPAddress(1, 1, 1, 1),         // Cloudflare primary
    IPAddress(1, 0, 0, 1),         // Cloudflare secondary
    IPAddress(8, 8, 8, 8),         // Google primary
    IPAddress(8, 8, 4, 4),         // Google secondary
    IPAddress(9, 9, 9, 9),         // Quad9 primary
    IPAddress(149, 112, 112, 112), // Quad9 secondary
    IPAddress(4, 2, 2, 1),         // Level 3 primary
    IPAddress(4, 2, 2, 2),         // Level 3 secondary
    IPAddress(4, 2, 2, 3),         // Level 3 tertiary
    IPAddress(208, 67, 222, 222),  // OpenDNS primary
    IPAddress(208, 67, 220, 220)   // OpenDNS secondary
  };
  /* Timers */
  const int PING_TIMER       = 30000; // delay between normal pings (ms)
  const int PING_RETRIES     = 5;     // retry ping before failing
  const int PING_RETRY_TIMER = 15000; // delay between retries (ms)
  const int POWER_CYCLE_TIME = 30;    // delay for power cycling (s)
  const int RECOVERY_DELAY   = 3;     // delay after powering back on (min)
#endif
const int NUM_DNS = sizeof(dnsList) / sizeof(dnsList[0]);
int currentIPIndex = 0;     // round-robin index

/* Ping statistics for each DNS address */
uint32_t okCount[NUM_DNS]   = {0};
uint32_t failCount[NUM_DNS] = {0};
uint32_t lastRebootTime     = 0;   // millis() when last router reboot occurred
uint32_t esp32UpTime        = 0;   // millis() since last ESP32 restart

/* GPIO pins */
const int RELAY_PIN        = 5;   // GPIO5, active HIGH via 2N2222
const int OK_LED_PIN       = 0;   // GPIO0
const int FAILURE_LED_PIN  = 1;   // GPIO1
const int BUILTIN_LED_PIN  = 10;  // WS2812 RGB LED on GPIO10 (Waveshare ESP32-C3-Zero)

/* NeoPixel object for the built-in RGB LED */
Adafruit_NeoPixel pixels(1, BUILTIN_LED_PIN, NEO_GRB + NEO_KHZ800);

/* HTTP server on port 80 */
WiFiServer httpServer(80);

/* Helper to print a human-readable WiFi status message */
void printWiFiStatus()
{
  int status = WiFi.status();

  if (status != WL_CONNECTED)
  {
    Serial.print("!! WiFi status = ");
    Serial.print(status);
    Serial.print(" (");

    switch (status)
    {
      case WL_IDLE_STATUS:
        Serial.println("idle, not attempting to connect)");
        break;
      case WL_NO_SSID_AVAIL:
        Serial.println("SSID not found, check network name/range?)");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("connection failed, wrong password/auth. issue?)");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("connection lost)");
        break;
      case WL_DISCONNECTED:
        Serial.println("disconnected, also check SSID and/or password)");
        break;
      case WL_NO_SHIELD:
        Serial.println("I somehow have no WiFi hardware!)");
        break;
      default:
        Serial.println("unknown error)");
        break;
    }
  }
  else
  {
    Serial.println(">> WiFi status = 3 (successfully connected!)");
  }
}

/* Helper to format time since last reboot */
String timeAgo(uint32_t lastTime)
{
  if (lastTime == 0) return "Never";

  uint32_t ms = millis() - lastTime;
  uint32_t seconds = ms / 1000;
  uint32_t days    = seconds / 86400;  seconds %= 86400;
  uint32_t hours   = seconds / 3600;   seconds %= 3600;
  uint32_t minutes = seconds / 60;     seconds %= 60;

  String s = "";
  if (days)    { s += String(days)    + "d "; }
  if (hours)   { s += String(hours)   + "h "; }
  if (minutes) { s += String(minutes) + "m "; }
  s += String(seconds) + "s ago";
  return s;
}

/* Serve the HTTP status page */
void serveHttpPage(WiFiClient &client)
{
  Serial.print(">> Sending status page to ");
  Serial.println(client.remoteIP());

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE HTML><html>");
  client.println("<head><title>Brett's Router Rebooter</title>");
  
  /* Dynamic refresh based on PING_TIMER + 3 seconds */
  client.print("<meta http-equiv=\"refresh\" content=\"");
  client.print( (PING_TIMER / 1000) + 3 );
  client.println("\">"); 

  client.println("<style>table{border-collapse:collapse;width:100%;}th,td{border:1px solid #ccc;padding:8px;text-align:left;}</style>");
  client.println("</head><body>");

  client.print("<h1><u>Brett's Router Rebooter (29/Mar/2026)</u></h1>");
  
  client.print("<h2>Connection</h2>");
  client.println("<table><tr><th>WiFi AP name</th><th>IP address</th><th>Last router reboot</th><th>Last ESP32 reboot</th></tr>");
  client.print("<tr><td>");
  client.print(ssid);
  client.print("</td><td>");
  client.print(WiFi.localIP());
  client.print("</td><td>");
  client.print(timeAgo(lastRebootTime));
  client.print("</td><td>");
  client.print(timeAgo(esp32UpTime));
  client.print("</td></tr>");
  client.print("</table></p>");

  client.println("<h2>Rechability statistics</h2>");
  client.println("<table><tr><th>Target address</th><th>OK pings</th><th>Failed pings</th></tr>");

  for (int i = 0; i < NUM_DNS; i++)
  {
    client.print("<tr><td>");
    client.print(dnsList[i]);
    client.print("</td><td>");
    client.print(okCount[i]);
    client.print("</td><td>");
    client.print(failCount[i]);
    client.println("</td></tr>");
  }
  client.println("</table></body></html>");
  
  client.flush();   // ensure all data is sent before closing
}

/* Check for browser requests without blocking */
void handleHttpClients()
{
  WiFiClient client = httpServer.available();
  if (client) 
  {
    Serial.print(">> Received HTTP GET from ");  Serial.println(client.remoteIP());
    serveHttpPage(client);
    client.stop();
  }
}

/* Non-blocking wait that keeps the HTTP server responsive */
void waitWithHttpCheck(long ms)
{
  unsigned long start = millis();
  while (millis() - start < (unsigned long)ms)
  {
    handleHttpClients();
    delay(200);   // check every 200 ms
  }
}

void setup() 
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n>> Brett's Router Rebooter Starting!");
  Serial.println("   (29/Mar/2026)");

  esp32UpTime = millis();

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
  digitalWrite(RELAY_PIN,       RELAY_DEENERGISE);
  digitalWrite(OK_LED_PIN,      HIGH);
  digitalWrite(FAILURE_LED_PIN, LOW);

  pixels.begin();
  pixels.setBrightness(80);
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  Serial.println(">> Hardware initialised: Router on, OK LED on, FAILURE LED off, Ping LED off.");
}

void connectToWiFi() 
{
  Serial.print(">> Connecting to WiFi network <"); Serial.print(ssid); Serial.println("> ...");

#ifdef STATIC_IP
  WiFi.config(staticIP, gateway, subnet, dns);
  Serial.print(">> Using static IP address <"); Serial.print(staticIP); Serial.println(">.");
#endif

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    delay(250);
    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
    pixels.show();
    delay(250);
    printWiFiStatus(); 
  }

  Serial.print(">> Connected to <"); Serial.print(ssid); Serial.print("> with address <"); Serial.print(WiFi.localIP()); Serial.println(">.");

  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  httpServer.begin();
  Serial.println(">> HTTP status page started on port 80");
  Serial.print("   Open in browser: http://");
  Serial.println(WiFi.localIP());
}

/* Ping the well-known IP address to verify we can talk to the Internet */
bool performPing() 
{
  pixels.setPixelColor(0, pixels.Color(0, 0, 255));
  pixels.show();

  IPAddress currentTarget = dnsList[currentIPIndex];

  Serial.print(">> Pinging "); Serial.print(currentTarget); Serial.print(" ... ");

  if (Ping.ping(currentTarget, 1)) 
  {
    Serial.println("OK!");
    okCount[currentIPIndex]++;                     // ← statistics
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    return true;
  } 
  else 
  {
    Serial.println("failed!");
    failCount[currentIPIndex]++;                   // ← statistics
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();
    return false;
  }
}

void setNormalState() 
{
  digitalWrite(OK_LED_PIN,      HIGH);
  digitalWrite(FAILURE_LED_PIN, LOW);
}

void setFailureState() 
{
  digitalWrite(OK_LED_PIN,      LOW);
  digitalWrite(FAILURE_LED_PIN, HIGH);
}

void loop() 
{
  handleHttpClients();   // check for browser requests at the start of every loop

  /* Handle any incoming HTTP port 80 requests */
  WiFiClient client = httpServer.available();
  if (client) 
  {
    serveHttpPage(client);
    client.stop();
  }

  connectToWiFi();   // Reconnect after every power-cycle

  while (true) 
  {
    handleHttpClients();   // keep the web page responsive inside the monitoring loop

    if (WiFi.status() != WL_CONNECTED) 
    {
      Serial.println("!! WiFi lost – reconnecting ...");
      printWiFiStatus(); 
      break;
    }

    if (performPing()) 
    {
      setNormalState();
      waitWithHttpCheck(PING_TIMER);   // non-blocking wait instead of delay
      continue;
    }

    setFailureState();

    bool recovered = false;
    for (int i = 0; i < PING_RETRIES; i++) 
    {
      waitWithHttpCheck(PING_RETRY_TIMER);   // non-blocking wait
      if (performPing()) 
      {
        recovered = true;
        break;
      }
    }

    if (recovered) 
    {
      setNormalState();
      waitWithHttpCheck(PING_TIMER);   // non-blocking wait
      continue;
    }

    // Still dead after retries → power cycle
    Serial.println("!! No response after retries, disconnecting router power.");
    digitalWrite(RELAY_PIN, RELAY_ENERGISE);
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();

    // Record the exact moment we triggered a reboot
    lastRebootTime = millis();

    Serial.print("   Waiting "); Serial.print(POWER_CYCLE_TIME); Serial.println("s before reconnecting power ...");
    int COUNT_DOWN_STEP = 5;
    for (int seconds = POWER_CYCLE_TIME; seconds > 0; seconds-=COUNT_DOWN_STEP) 
    {
      handleHttpClients();   // keep web page alive during countdown
      Serial.print("   "); Serial.print(seconds); Serial.println("s");
      for (int i = 0; i < COUNT_DOWN_STEP; i++)
      {
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));
        pixels.show();
        delay(500);
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.show();
        delay(500);
      }
    }
    Serial.println(">> Powering router back on.");
    digitalWrite(RELAY_PIN, RELAY_DEENERGISE);

    Serial.println(">> Starting recovery wait ...");
    for (int minutes = RECOVERY_DELAY; minutes > 0; minutes--) 
    {
      handleHttpClients();   // keep web page alive during recovery
      Serial.print("   Waiting ");  Serial.print(minutes);  Serial.println("min");

      for (int i = 0; i < 60; i++) 
      {
        digitalWrite(OK_LED_PIN, HIGH);
        delay(500);
        digitalWrite(OK_LED_PIN, LOW);
        delay(500);
      }
    }

    Serial.println(">> Router recovery finished – restarting monitoring.");
    Serial.println();
    Serial.println();

    setNormalState();

    WiFi.disconnect(true);
    delay(2000);
    break;
  }
}

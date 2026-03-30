/*********************************************************************/
/* Router Rebooter for ESP32-C3-Zero                                 */
/*                                                                   */
/* Brett Hallen, 26/Mar/2026: initial                                */
/*               27/Mar/2026: round-robin pinging                    */
/*               28/Mar/2026: ESP32 LED                              */
/*               29/Mar/2026: Static IP, HTTP stats/status page      */
/*               30/Mar/2026: added DNS names, NTP time keeping      */
/*               31/Mar/2026: code cleanup & commenting              */
/*                            added F() macro to reduce RAM/heap use */
/*                                                                   */
/* HARDWARE MAPPING:                                                 */
/*   Relay control: GPIO 5 (pin 9) via 2N2222 transistor             */
/*                  active HIGH to energise                          */
/*   OK LED:        GPIO 0 (pin 4)                                   */
/*   FAILURE LED:   GPIO 1 (pin 5)                                   */
/*                                                                   */
/* RELAY BEHAVIOR:                                                   */
/*   NC contact used, coil de-energised most of the time (router on) */
/*   Only energise coil when we need to cut power (reboot)           */
/*                                                                   */
/* LED BEHAVIOUR:                                                    */
/*   OK LED:      steady when router is okay,                        */
/*                flashing when recovery in progress                 */
/*   FAILURE LED: steady when pinging has failed                     */
/*   ESP32:       flashes blue when pinging,                         */
/*                red when ping failed,                              */
/*                flashes green during recovery                      */
/*                blue/green when connecting to WiFi                 */
/*********************************************************************/

#include <WiFi.h>
#include <ESPping.h>             /* Required for pings                                             */
#include <Adafruit_NeoPixel.h>   /* Required for the WS2812 RGB LED on the Waveshare ESP32-C3-Zero */
#include <time.h>                /* Required for NTP real-time clock support                       */

/* CUSTOMISATIONS ******************************************************************/
/* DEBUG_MODE ........... shorter timers & dodgy addresses to test the monitioring */
/* STATIC_IP  ........... set a static IP address to make it easier to find the    */
/*                        HTTP status page                                         */
/* NTP configuration .... if you want time stamps                                  */
/* NTP_GMT_OFFSET ....... your timezone for NTP                                    */
/* formatNTPTime ........ customise the timestamp output format if you like        */
/* WIFi configuration ... obviously very important, configure your home WiFi here  */
/* dnsList & dnsName .... list of target IP addresses/names to test reachability   */
/*                        to, customise if you want!                               */
/* myRouter ............. just a name to help identify the rebooter                */
/***********************************************************************************/

/* Uncomment for debug/testing ***********************/
#define DEBUG_MODE   /* testing settings or normal */
/*****************************************************/

/* Static IP address **************************************************/
#define STATIC_IP    /* assign yourself an IP address for easy access */
/**********************************************************************/

#define RELAY_ENERGISE   HIGH  /* Energise coil = cut power to router (NO contact) */
#define RELAY_DEENERGISE LOW   /* De-energise coil = restore power (NC contact)    */
#define NTP_GMT_OFFSET   10    /* GMT+10 (AEST)                                    */

const char* versionDate = "31/Mar/2026"; /* Version date for the firmware  */
const char* myRouter = "NF18ACV";        /* Customise to help identify me! */

/* NTP configuration ******************************************/
const char* ntpServer = "pool.ntp.org"; /* NTP server         */                        
const long  gmtOffset_sec = 3600 * NTP_GMT_OFFSET; /* AEDT    */
const int   daylightOffset_sec = 0; /* not bothering with DST */
/**************************************************************/

/* WiFi configuration **************************************************/
const char* ssid     = "RouterRebootTest";  /* Your WiFi SSID here     */
const char* password = "Today123!";         /* Your WiFi password here */
/***********************************************************************/

#ifdef STATIC_IP /* Using a static IP makes it simple to load the status page       */
  const IPAddress staticIP(192, 168, 128, 129);   /* your static IP address         */
  const IPAddress gateway(192, 168, 1, 1);        /* your router/gateway IP address */
  const IPAddress subnet(255, 255, 0, 0);         /* your netmask                   */
  const IPAddress dns(1, 1, 1, 1);                /* DNS IP address                 */
#endif

#ifdef DEBUG_MODE
  /* Round-robin DNS list (DEBUG mode – includes dummy IPs to force failures) */
  const IPAddress dnsList[] = 
  {
    IPAddress(1, 1, 1, 1),      /* Cloudflare primary    */
    IPAddress(192,168,123,123), /* dummy to test failure */
    IPAddress(1, 0, 0, 1),      /* Cloudflare secondary  */
    IPAddress(192,168,123,123), /* dummy to test failure */
    IPAddress(192,168,123,123), /* dummy to test failure */
    IPAddress(192,168,123,123), /* dummy to test failure */
    IPAddress(192,168,123,123)  /* dummy to test failure */
  };
  const char* dnsName[] =
  {
    "Cloudflare primary",    /* 1.1.1.1 */
    "Dummy test failure",    
    "Cloudflare secondary",  /* 1.0.0.1 */
    "Dummy test failure",
    "Dummy test failure",
    "Dummy test failure",
    "Dummy test failure"
  };
  /* Timers for testing ****************************************************/
  const int PING_TIMER       = 10000; /* delay between normal pings (ms)   */
  const int PING_RETRIES     = 3;     /* retry ping before failing         */
  const int PING_RETRY_TIMER = 5000;  /* delay between retries (ms)        */
  const int POWER_CYCLE_TIME = 30;    /* delay for power cycling (s)       */
  const int RECOVERY_DELAY   = 120;    /* delay after powering back on (s) */
  /*************************************************************************/
#else
  /* Round-robin DNS list (normal production mode) *********/
  const IPAddress dnsList[] = 
  {
    IPAddress(1, 1, 1, 1),         /* Cloudflare primary   */
    IPAddress(1, 0, 0, 1),         /* Cloudflare secondary */
    IPAddress(8, 8, 8, 8),         /* Google primary       */
    IPAddress(8, 8, 4, 4),         /* Google secondary     */
    IPAddress(9, 9, 9, 9),         /* Quad9 primary        */
    IPAddress(149, 112, 112, 112), /* Quad9 secondary      */
    IPAddress(4, 2, 2, 1),         /* Level 3 primary      */
    IPAddress(4, 2, 2, 2),         /* Level 3 secondary    */
    IPAddress(4, 2, 2, 3),         /* Level 3 tertiary     */
    IPAddress(208, 67, 222, 222),  /* OpenDNS primary      */
    IPAddress(208, 67, 220, 220)   /* OpenDNS secondary    */
  };
  /* Target ames for each IP target (matches the list above) */
  const char* dnsName[] = 
  {
    "Cloudflare primary",   /* 1.1.1.1                       */
    "Cloudflare secondary", /* 1.0.0.1                       */
    "Google primary",       /* 8.8.8.8                       */
    "Google secondary",     /* 8.8.4.4                       */
    "Quad9 primary",        /* 9.9.9.9                       */
    "Quad9 secondary",      /* 149.112.112.112               */
    "Level 3 primary",      /* 4.2.2.1                       */
    "Level 3 secondary",    /* 4.2.2.2                       */
    "Level 3 tertiary",     /* 4.2.2.3                       */
    "OpenDNS primary",      /* 208.67.222.222                */
    "OpenDNS secondary"     /* 208.67.220.220                */
  };
  /* Timers ***************************************************************/
  const int PING_TIMER       = 30000; /* delay between normal pings (ms)  */
  const int PING_RETRIES     = 5;     /* retry ping before failing        */
  const int PING_RETRY_TIMER = 15000; /* delay between retries (ms)       */
  const int POWER_CYCLE_TIME = 30;    /* delay for power cycling (s)      */
  const int RECOVERY_DELAY   = 180;   /* delay after powering back on (s) */
  /************************************************************************/
#endif
const int NUM_DNS = sizeof(dnsList) / sizeof(dnsList[0]); /* count of ping targets */
int currentIPIndex = 0; /* round-robin index */

/* Ping statistics for each DNS address ****************************************************/
uint32_t okCount[NUM_DNS]        = {0}; /* how many OK pings                               */
uint32_t failCount[NUM_DNS]      = {0}; /* how many failed pings                           */
time_t   lastFailedPing[NUM_DNS] = {0}; /* timestamp of last failed ping                   */
uint32_t lastRebootTime          = 0;   /* millis() when last router reboot occurred       */
time_t   lastRebootNTP           = 0;   /* NTP timestamp of last router reboot (0 = never) */
uint32_t esp32UpTime             = 0;   /* millis() since last ESP32 restart               */
/*******************************************************************************************/

/* GPIO pins ****************************************************************************/
const int RELAY_PIN        = 5;   /* GPIO5, active HIGH via 2N2222                      */
const int OK_LED_PIN       = 0;   /* GPIO0                                              */
const int FAILURE_LED_PIN  = 1;   /* GPIO1                                              */
const int BUILTIN_LED_PIN  = 10;  /* WS2812 RGB LED on GPIO10 (Waveshare ESP32-C3-Zero) */
/****************************************************************************************/

/* avoid kernel panic after router reboot if HTTP status page is active */
bool rebootInProgress = false;

/* NeoPixel object for the built-in RGB LED */
Adafruit_NeoPixel pixels(1, BUILTIN_LED_PIN, NEO_GRB + NEO_KHZ800);

/* HTTP server on port 80 for status/statistics */
WiFiServer httpServer(80);

/**************************************/
/* Print WiFi connection error status */
/**************************************/
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

/**********************************/
/* Convert ms to day/hour/min/sec */
/**********************************/
String timeAgo(uint32_t lastTime)
{
  if (lastTime == 0) return "Never";

  uint32_t ms = millis() - lastTime;
  uint32_t seconds = ms / 1000;
  uint32_t days    = seconds / 86400;  seconds %= 86400;
  uint32_t hours   = seconds / 3600;   seconds %= 3600;
  uint32_t minutes = seconds / 60;     seconds %= 60;

  String s = "";
  if (days)    {s += String(days)   +"d ";}
  if (hours)   {s += String(hours)  +"h ";}
  if (minutes) {s += String(minutes)+"m ";}
  s += String(seconds) + "s ago";
  return s;
}

/*************************************/
/* Format NTP timestamp as date/time */
/*************************************/
String formatNTPTime(time_t t)
{
  if (t == 0) return "Never";
  struct tm *tm = localtime(&t);
  char buf[32];
  /* customise output here if you want */
  /*                          DD/Mmm/YYYY HH:MM:SS */
  strftime(buf, sizeof(buf), "%d/%b/%Y %H:%M:%S", tm);
  return String(buf);
}

/*****************/
/* Sync NTP time */
/*****************/
void syncNTPTime()
{
  Serial.print(">> Syncing NTP time (pool.ntp.org) ... ");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  /* Give it a few seconds to get a valid time */
  for (int i = 0; i < 25; i++) 
  {
    time_t now = time(NULL);
    if (now > 1000000000UL) 
    {   /* valid Unix epoch */
      Serial.println(formatNTPTime(now));
      return;
    }
    delay(200);
    handleHttpClients();
  }
  Serial.println("failed, no valid time yet");
}

/******************************/
/* Serve the HTTP status page */
/******************************/
void serveHttpPage(WiFiClient &client)
{
  if (rebootInProgress)
  {
    Serial.println("!! [serveHttpPage] Reboot in progress, ignore HTTP requests.");
    return;
  }
  Serial.print(">> Sending status page to <"); Serial.print(client.remoteIP()); Serial.println(">");

  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-type:text/html"));
  client.println(F("Connection: close"));
  client.println();
  client.println(F("<!DOCTYPE HTML><html>"));
  client.print(F("<head><title>Brett's Router Rebooter ("));
  client.print(myRouter);
  client.println(F(")</title>"));

  /* Dynamic refresh based on PING_TIMER + 3 seconds */
  client.print(F("<meta http-equiv=\"refresh\" content=\""));
  client.print( (PING_TIMER / 1000) + 3 );
  client.println(F("\">")); 

  client.println(F("<style>table{border-collapse:collapse;width:100%;}th,td{border:1px solid #ccc;padding:8px;text-align:left;}</style>"));
  client.println(F("</head><body>"));

  /* actual page text starts here */
  client.print(F("<h1><u>Brett's Router Rebooter (")); client.print(versionDate); client.print(F(")</u>"));
#ifdef DEBUG_MODE
  client.println(F(" *DEBUG MODE*"));
#endif
  client.println(F("</h1>"));
  /* advise about auto-refresh of page */
  client.print(F("(This page will refresh every "));
  client.print((PING_TIMER / 1000) + 3 );
  client.println(F("s)"));
  
  /* table listing our connection info */
  client.print(F("<h2>Connection</h2>"));
  client.println(F("<table>"));
  /* SSID of configured WiFi access point */
  client.print(F("<tr><th>WiFi AP name</th><td>")); client.print(ssid); client.println(F("</td></tr>"));
  /* our IP address */
  client.print(F("<tr><th>IP address</th><td>")); client.print(WiFi.localIP()); client.println(F("</td></tr>"));
  /* the router we are monitoring */
  client.print(F("<tr><th>Monitored router</th><td>")); client.print(myRouter); client.println(F("</td></tr>"));
  /* the current time */
  client.print(F("<tr><th>Current time (NTP)</th><td>")); client.print(formatNTPTime(time(NULL))); client.println(F("</td></tr>"));
  /* when we last rebooted the router (time ago & time stamp) */
  client.print(F("<tr><th>Last router reboot</th><td>"));   client.print(timeAgo(lastRebootTime)); 
  if (lastRebootNTP > 0) {client.print(F(" (")); client.print(formatNTPTime(lastRebootNTP)); client.print(F(")"));} 
  client.println(F("</td></tr>"));
  /* when the ESP32 was last reset/rebooted */
  client.print(F("<tr><th>Last ESP32 reboot</th><td>")); client.print(timeAgo(esp32UpTime)); client.println(F("</td></tr>"));
  client.println(F("</table></p>"));

  /* Our current monitoring configuration */
  client.print(F("<h2>Timers</h2>"));
  client.println(F("<table>"));
  /* time between pings */
  client.print(F("<tr><th>Ping timer</th><td>")); client.print(PING_TIMER/1000); client.println(F("s</td></tr>"));
  /* how many ping retries before we take action */
  client.print(F("<tr><th>Ping retries</th><td>")); client.print(PING_RETRIES); client.println(F("</td></tr>"));
  /* time between ping retries */
  client.print(F("<tr><th>Ping retry timer</th><td>")); client.print(PING_RETRY_TIMER/1000); client.println(F("s</td></tr>"));
  /* how long we keep the router off before powering back on */
  client.print(F("<tr><th>Power cycle timer</th><td>")); client.print(POWER_CYCLE_TIME); client.println(F("s</td></tr>"));
  /* how long we wait after powering the router back on before we resume our monitoring */
  client.print(F("<tr><th>Recovery delay</th><td>")); client.print(RECOVERY_DELAY); client.println(F("s</td></tr>"));
  client.println(F("</table></p>"));

  /* our OK/FAILED ping stats for each target address */
  client.println(F("<h2>Rechability statistics</h2>"));
  client.println(F("<table><tr><th>Target address</th><th>Target name</th><th>OK pings</th><th>Failed pings</th><th>Last failed ping</th></tr>"));

  /* output each target's statistics */
  for (int i = 0; i < NUM_DNS; i++)
  {
    /* target IP address */
    client.print(F("<tr><td>"));
    client.print(dnsList[i]);
    /* target name */
    client.print(F("</td><td>"));
    client.print(dnsName[i]);
    /* how many OK pings */
    client.print(F("</td><td>"));
    client.print(okCount[i]);
    /* how many failed pings */
    client.print(F("</td><td>"));
    client.print(failCount[i]);
    /* only print the failed timestamp if there is one */
    client.print(F("</td><td>"));
    if (lastFailedPing[i] > 0) {client.print(formatNTPTime(lastFailedPing[i]));} 
    client.println(F("</td></tr>"));
  }
  client.println(F("</table></body></html>"));
  
  /* ensure the whole page is sent before closing */
  client.flush();
}

/***************************************/
/* Check for HTTP status page requests */
/***************************************/
void handleHttpClients()
{
  if (rebootInProgress || WiFi.status() != WL_CONNECTED)
  {
    if (!rebootInProgress)
      Serial.println(F("!! [handleHttpClients] WiFi not connected, ignoring HTTP requests."));
    else
      Serial.println(F("!! [handleHttpClients] Reboot in progress, ignoring HTTP requests."));
    return;
  }

  WiFiClient client = httpServer.available();
  if (client) 
  {
    Serial.print(">> Received HTTP GET from <");  Serial.print(client.remoteIP()); Serial.println(">");
    serveHttpPage(client);
    client.stop();
  }
}

/***********************************************************/
/* Non-blocking wait that keeps the HTTP server responsive */
/***********************************************************/
void waitWithHttpCheck(long ms)
{
  unsigned long start = millis();
  while (millis() - start < (unsigned long)ms)
  {
    handleHttpClients();
    /* check every 200ms */
    delay(200);
  }
}

/*********************/
/* Test reachability */
/*********************/
bool performPing() 
{
  pixels.setPixelColor(0, pixels.Color(0, 0, 255));
  pixels.show();

  IPAddress currentTarget = dnsList[currentIPIndex];

  Serial.print(">> Pinging "); Serial.print(currentTarget); Serial.print(" ("); Serial.print(dnsName[currentIPIndex]); Serial.print(") ... ");

  if (Ping.ping(currentTarget, 1)) 
  {
    Serial.println("OK!");
    okCount[currentIPIndex]++;
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    return true;
  } 
  else 
  {
    Serial.println("failed!");
    failCount[currentIPIndex]++;
    lastFailedPing[currentIPIndex] = time(NULL);
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();
    return false;
  }
}

/****************/
/* All is well! */
/****************/
void setNormalState() 
{
  digitalWrite(OK_LED_PIN,      HIGH); /* OK LED is on       */
  digitalWrite(FAILURE_LED_PIN, LOW);  /* FAILURE LED is off */
}

/*********************/
/* Failure detected! */
/*********************/
void setFailureState() 
{
  digitalWrite(OK_LED_PIN,      LOW);  /* OK LED is off     */
  digitalWrite(FAILURE_LED_PIN, HIGH); /* FAILURE LED is on */
}

/*******************************/
/* Connect to the WiFi network */
/*******************************/
void connectToWiFi() 
{
  Serial.print(">> Connecting to WiFi network <"); Serial.print(ssid); Serial.println("> ...");

#ifdef STATIC_IP
  WiFi.config(staticIP, gateway, subnet, dns);
  Serial.print(">> Using static IP address <"); Serial.print(staticIP); Serial.println(">");
#endif

  /* attempt to connect */
  WiFi.begin(ssid, password);

  /* flash the ESP32 LED green/blue until we connect */
  while (WiFi.status() != WL_CONNECTED) 
  {
    /* set green */
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    delay(250);
    /* set blue */
    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
    pixels.show();
    delay(250);
    /* output WiFi status to serial console */ 
    printWiFiStatus(); 
  }

  Serial.print(">> Connected to <"); Serial.print(ssid); Serial.print("> with address <"); Serial.print(WiFi.localIP()); Serial.println(">");

  /* turn off ESP32 built-in LED */
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  /* start up HTTP server for status page requests */
  httpServer.begin();
  Serial.println(">> HTTP status page started on port 80");
  Serial.print("   Open in browser: http://");
  Serial.println(WiFi.localIP());

  rebootInProgress = false;

  /* initial NTP sync after WiFi is up */
  syncNTPTime();
}

/***************************/
/********** SETUP **********/ 
/***************************/
void setup() 
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n>> Brett's Router Rebooter Starting!");
  Serial.print("   ("); Serial.print(versionDate); Serial.println(")");
  Serial.print(">> Monitoring "); Serial.println(myRouter);

  /* capture our start up time */
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
  Serial.print("   Recovery delay ...... "); Serial.print(RECOVERY_DELAY); Serial.println("s");

  /* configure our output pins */
  pinMode(RELAY_PIN,           OUTPUT);           /* relay control pin             */
  digitalWrite(RELAY_PIN,      RELAY_DEENERGISE); /* ... de-energised, power is on */
  pinMode(OK_LED_PIN,          OUTPUT);           /* OK LED                        */
  digitalWrite(OK_LED_PIN,     HIGH);             /* ... is on                     */
  pinMode(FAILURE_LED_PIN,     OUTPUT);           /* FAILURE LED                   */
  digitalWrite(FAILURE_LED_PIN,LOW);              /* ... is off                    */

  /* initialise the ESP32's built-in LED */
  pixels.begin();
  pixels.setBrightness(80);
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  Serial.println(F(">> Hardware initialised: Router on, OK LED on, FAILURE LED off, Ping LED off."));
}

/*******************************/
/********** MAIN LOOP **********/
/*******************************/
void loop() 
{
  /* check for browser requests at the start of every loop */
  handleHttpClients(); 

  /* Reconnect after every power-cycle */
  connectToWiFi();

  /* allow HTTP GET requests again (kernel panic otherwise) */
  rebootInProgress = false;
  while (true) 
  {
    /* keep the web page responsive inside the monitoring loop */
    handleHttpClients(); 

    /* Daily NTP re-sync at 05:00 local time */
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) 
    {
      static bool syncedToday = false;
      if (timeinfo.tm_hour == 5 && timeinfo.tm_min == 0 && !syncedToday) 
      {
        syncNTPTime();
        syncedToday = true;
      }
      if (timeinfo.tm_hour != 5) syncedToday = false;
    }

    /* We lost our WiFi connecting, try to re-connect */
    if (WiFi.status() != WL_CONNECTED) 
    {
      Serial.println("!! WiFi lost – reconnecting ...");
      printWiFiStatus(); 
      break;
    }

    /* test our Internet reachability */
    if (performPing()) 
    {
      setNormalState();

      /* non-blocking so HTTP requests are handled quickly */
      waitWithHttpCheck(PING_TIMER);
      continue;
    }

    /* the last ping failed, indicate via LEDs */
    setFailureState();

    /* start ping re-tries */
    bool recovered = false;
    for (int i = 0; i < PING_RETRIES; i++) 
    {
      /* non-blocking so HTTP requests are handled quickly */
      waitWithHttpCheck(PING_RETRY_TIMER);
      if (performPing()) 
      {
        recovered = true;
        break;
      }
    }

    /* next ping was successful, phew! */
    if (recovered) 
    {
      setNormalState();

      /* non-blocking so HTTP requests are handled quickly */
      waitWithHttpCheck(PING_TIMER); 
      continue;
    }

    /* still dead after retries, power cycle */
    rebootInProgress = true; /* needed to avoid kernel panic after router power cycling & WiFi disconnect/reconnect */
    httpServer.stop();       /* and this */
    Serial.println("!! No response after retries, disconnecting router power.");
    /* energise the relay ... opens ... disconnect router power */
    digitalWrite(RELAY_PIN, RELAY_ENERGISE);
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();

    /* Record both millis and timestamp */
    lastRebootTime = millis();
    lastRebootNTP  = time(NULL);

    /* keep router off for a short period to ensure we clear everything from RAM */
    Serial.print(">> Waiting "); Serial.print(POWER_CYCLE_TIME); Serial.println("s before reconnecting power ...");
    int COUNT_DOWN_STEP = 5; /* update every 5s */
    for (int seconds = POWER_CYCLE_TIME; seconds > 0; seconds-=COUNT_DOWN_STEP)
    {
      /* keep web page alive during countdown */
      //handleHttpClients(); 
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

    /* wait for router to re-connect to ISP before resuming monitoring */
    Serial.print(">> Waiting "); Serial.print(RECOVERY_DELAY); Serial.println("s before resuming monitoring ...");
    for (int seconds = RECOVERY_DELAY; seconds > 0; seconds-=(COUNT_DOWN_STEP*3)) /* update every 15s */
    {
      //handleHttpClients();
      Serial.print("   ");  Serial.print(seconds);  Serial.println("s");
      for (int i = 0; i < (COUNT_DOWN_STEP*3); i++) 
      {
        digitalWrite(OK_LED_PIN, HIGH);
        delay(500);
        digitalWrite(OK_LED_PIN, LOW);
        delay(500);
      }
    }

    Serial.println(">> Router recovery finished – resuming monitoring.");
    Serial.println();
    Serial.println();

    setNormalState();
    
    WiFi.disconnect(true);
    delay(2000);
    break;
  }
}
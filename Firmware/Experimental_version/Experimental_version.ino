/***********************************************************************/
/* Router Rebooter for ESP32-C3-Zero                                   */
/*                                                                     */
/* Brett Hallen, 26/Mar/2026: initial                                  */
/*               27/Mar/2026: round-robin pinging                      */
/*               28/Mar/2026: ESP32 LED                                */
/*               29/Mar/2026: Static IP, HTTP stats/status page        */
/*               30/Mar/2026: added DNS names, NTP time keeping        */
/*               31/Mar/2026: code cleanup & commenting                */
/*                            added F() macro to reduce RAM/heap use   */
/*                1/Apr/2026: added Github link to status page         */
/*                2/Apr/2026: added TZ/DST support                     */
/*                5/Apr/2026: added reboot count & other minor tweaks  */
/*               16/Apr/2026: fixed UTC-AEDT conversion                */
/*                            calculate MIN_VALID_TIME at compile time */
/*                                                                     */
/* HARDWARE MAPPING:                                                   */
/*   Relay control: GPIO 5 (pin 9) via 2N2222 transistor               */
/*                  active HIGH to energise                            */
/*   OK LED:        GPIO 0 (pin 4)                                     */
/*   FAILURE LED:   GPIO 1 (pin 5)                                     */
/*                                                                     */
/* RELAY BEHAVIOR:                                                     */
/*   NC contact used, coil de-energised most of the time (router on)   */
/*   Only energise coil when we need to cut power (reboot)             */
/*                                                                     */
/* LED BEHAVIOUR:                                                      */
/*   OK LED:      steady when router is okay,                          */
/*                flashing when recovery in progress                   */
/*   FAILURE LED: steady when pinging has failed                       */
/*   ESP32:       flashes blue when pinging,                           */
/*                red when ping failed,                                */
/*                flashes green during recovery                        */
/*                blue/green when connecting to WiFi                   */
/***********************************************************************/

/* Required Arduino libraries **********************************************************************/
#include <WiFi.h>                /* Wonder what this is for                                        */
#include <ESPping.h>             /* Required for pings                                             */
#include <Adafruit_NeoPixel.h>   /* Required for the WS2812 RGB LED on the Waveshare ESP32-C3-Zero */
#include <time.h>                /* Required for NTP real-time clock support                       */
#include <Timezone.h>            /* Required for TZ and DST support                                */
/***************************************************************************************************/

/* CUSTOMISATIONS ******************************************************************/
/* DEBUG_MODE ........... Shorter timers & dodgy addresses to test the monitioring */
/* STATIC_IP  ........... set a static IP address to make it easier to find the    */
/*                        HTTP status page                                         */
/* NTP configuration .... If you want time stamps                                  */
/* formatNTPTime ........ Customise the timestamp output format if you like        */
/* WIFi configuration ... Obviously very important, configure your home WiFi here  */
/* dnsList & dnsName .... List of target IP addresses/names to test reachability   */
/*                        to, customise if you want!                               */
/* myRouter ............. Just a name to help identify the rebooter                */
/* myRouterAdminPage .... If you want a link on the status page                    */
/***********************************************************************************/

/* Uncomment for debug/testing ***********************/
//#define DEBUG_MODE   /* Testing settings or normal */
/*****************************************************/

/* Static IP address **************************************************/
#define STATIC_IP    /* Assign yourself an IP address for easy access */
/**********************************************************************/

/* Status page settings *******************************************************************/
const char* versionDate = "16/Apr/2026";                 /* Version date of this firmware */
const char* myRouter = "NF18ACV";                       /* Customise to help identify me! */
const char* myRouterAdminPage = "http://192.168.1.1"; /* Admin/login page for your router */
//const char* myRouterAdminPage = "";              /* No admin/login page for your router */
const char* githubRepo = "https://github.com/BrettHallen/Router-Rebooter";   /* My Github */
/******************************************************************************************/

/* Static IP configuration **********************************************************/
#ifdef STATIC_IP 
  const IPAddress staticIP(192, 168, 128, 128);   /* Your static IP address         */
//const IPAddress staticIP(192, 168, 128, 129);   /* For testing 2nd rebooter       */
  const IPAddress gateway(192, 168, 1, 1);        /* Your router/gateway IP address */
  const IPAddress subnet(255, 255, 0, 0);         /* Your netmask                   */
  const IPAddress dns(1, 1, 1, 1);                /* DNS IP address                 */
#endif
/************************************************************************************/

/* WiFi configuration **************************************************/
const char* ssid     = "RouterRebootTest";  /* Your WiFi SSID here     */
const char* password = "Today123!";         /* Your WiFi password here */
/***********************************************************************/

/* NTP & TZ configuration *********************************************************************************/
const char* ntpServer = "pool.ntp.org";                      /* NTP server                                */
/* TZ/DST setting for NSW, Australia **********************************************************************/
TimeChangeRule myDST = {"AEDT", First, Sun, Oct, 2, 660};    /* GMT+11 hours from 02:00 first Sunday Oct  */
TimeChangeRule mySTD = {"AEST", First, Sun, Apr, 3, 600};    /* GMT+10 hours from 03:00 first Sunday Apr  */
/* Example: TZ setting for non-DST location, i.e. QLD, Australia ******************************************/
// TimeChangeRule myDST = {"AEST", First, Sun, Jan, 0, 600}; /* Always GMT+10                             */
// TimeChangeRule mySTD = {"AEST", First, Sun, Jan, 0, 600}; /* Always GMT+10                             */
/**********************************************************************************************************/
Timezone myTZ(myDST, mySTD);                                 /* Create the timezone object                */
const char* previousTZAbbrev = "";                           /* For serial log to detect when DST changed */
bool ntpTimeSynced = false;                                  /* True after first successful NTP sync      */
/**********************************************************************************************************/
//#define MIN_VALID_TIME  1776297600UL     /* 16/Apr/2026 ... ensure only reasonable timestamps are shown */
/**********************************************************************************************************/

/*******************************************/
/* Generate MIN_VALID_TIME at compile time */
/* to ensure sane timestamps               */
/*******************************************/
#define COMPILE_YEAR   ((__DATE__[7]-'0')*1000 + (__DATE__[8]-'0')*100 + (__DATE__[9]-'0')*10 + (__DATE__[10]-'0'))
#define COMPILE_MONTH  ( __DATE__[0]=='J' && __DATE__[1]=='a' && __DATE__[2]=='n' ? 1 : \
                         __DATE__[0]=='F' ? 2 : \
                         __DATE__[0]=='M' && __DATE__[1]=='a' && __DATE__[2]=='r' ? 3 : \
                         __DATE__[0]=='A' && __DATE__[1]=='p' ? 4 : \
                         __DATE__[0]=='M' && __DATE__[1]=='a' && __DATE__[2]=='y' ? 5 : \
                         __DATE__[0]=='J' && __DATE__[1]=='u' && __DATE__[2]=='n' ? 6 : \
                         __DATE__[0]=='J' && __DATE__[1]=='u' && __DATE__[2]=='l' ? 7 : \
                         __DATE__[0]=='A' && __DATE__[1]=='u' ? 8 : \
                         __DATE__[0]=='S' ? 9 : \
                         __DATE__[0]=='O' ? 10 : \
                         __DATE__[0]=='N' ? 11 : 12)
#define COMPILE_DAY    ((__DATE__[4]==' ' ? 0 : __DATE__[4]-'0')*10 + (__DATE__[5]-'0'))
#define MIN_VALID_TIME (((time_t)(COMPILE_YEAR - 1970) * 31536000UL) + \
                        ((COMPILE_YEAR - 1969)/4 * 86400UL) - \
                        ((COMPILE_YEAR - 1901)/100 * 86400UL) + \
                        ((COMPILE_YEAR - 1601)/400 * 86400UL) + \
                        0UL)

#ifdef DEBUG_MODE
  /*****************************************************/
  /* Round-robin DNS list                              */
  /* DEBUG mode – includes dummy IPs to force failures */
  /*****************************************************/
  const IPAddress dnsList[] = 
  {
    IPAddress(1, 1, 1, 1),      /* Cloudflare primary    */
    IPAddress(192,168,123,123), /* Dummy to test failure */
    IPAddress(1, 0, 0, 1),      /* Cloudflare secondary  */
    IPAddress(192,168,123,123), /* Dummy to test failure */
    IPAddress(192,168,123,123), /* Dummy to test failure */
    IPAddress(192,168,123,123), /* Dummy to test failure */
    IPAddress(192,168,123,123)  /* Dummy to test failure */
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
  /* Timers for testing ***************************************************/
  const int PING_TIMER       = 10000; /* Delay between normal pings (ms)  */
  const int PING_RETRIES     = 3;     /* Retry ping before failing        */
  const int PING_RETRY_TIMER = 5000;  /* Delay between retries (ms)       */
  const int POWER_CYCLE_TIME = 30;    /* Delay for power cycling (s)      */
  const int RECOVERY_DELAY   = 120;   /* Delay after powering back on (s) */
  /*************************************************************************/
#else
  /*****************************************************/
  /* Round-robin DNS list                              */
  /*****************************************************/
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
  /* Target names for each IP target (matches the list above) */
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
  const int PING_TIMER       = 30000; /* Delay between normal pings (ms)  */
  const int PING_RETRIES     = 5;     /* Retry ping before failing        */
  const int PING_RETRY_TIMER = 15000; /* Delay between retries (ms)       */
  const int POWER_CYCLE_TIME = 30;    /* Delay for power cycling (s)      */
  const int RECOVERY_DELAY   = 180;   /* Delay after powering back on (s) */
  /************************************************************************/
#endif
const int NUM_DNS = sizeof(dnsList) / sizeof(dnsList[0]); /* Count of ping targets */
int currentIPIndex = 0;                                       /* Round-robin index */

/* Ping statistics for each DNS address ****************************************************/
uint32_t okCount[NUM_DNS]        = {0}; /* How many OK pings                               */
uint32_t failCount[NUM_DNS]      = {0}; /* How many failed pings                           */
time_t   lastFailedPing[NUM_DNS] = {0}; /* Timestamp of last failed ping                   */
uint32_t lastRebootTime          = 0;   /* millis() when last router reboot occurred       */
time_t   lastRebootNTP           = 0;   /* NTP timestamp of last router reboot (0 = never) */
uint32_t rebootCount             = 0;   /* How many router reboots                         */
uint32_t esp32UpTime             = 0;   /* millis() since last ESP32 restart               */
/*******************************************************************************************/

/* GPIO pins ****************************************************************************/
const int RELAY_PIN        = 5;   /* GPIO5, active HIGH via 2N2222                      */
const int OK_LED_PIN       = 0;   /* GPIO0                                              */
const int FAILURE_LED_PIN  = 1;   /* GPIO1                                              */
const int BUILTIN_LED_PIN  = 10;  /* WS2812 RGB LED on GPIO10 (Waveshare ESP32-C3-Zero) */
#define RELAY_ENERGISE     HIGH   /* Energise coil = cut power to router (NO contact)   */
#define RELAY_DEENERGISE   LOW    /* De-energise coil = restore power (NC contact)      */
/****************************************************************************************/

/*****************************************************/
/* Circular buffer for the last 15 serial debug logs */
/* Not currently used, maybe later                   */
// const int MAX_LOG_LINES = 15;
// String logBuffer[MAX_LOG_LINES];
// int logHead = 0;
/*****************************************************/

/* Avoid kernel panic after router reboot if HTTP status page is active */
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

/********************************/
/* Serial log when DST changed  */
/********************************/
void logTimezoneChange(const char* currentAbbrev)
{
  /* Don't check if we don't have a valid TZ yet */
  if (!ntpTimeSynced) return;
  
  if (currentAbbrev && previousTZAbbrev[0] != '\0' && strcmp(currentAbbrev, previousTZAbbrev) != 0)
  {
    Serial.print(F(">> Timezone changed: "));
    Serial.print(previousTZAbbrev);
    Serial.print(F(" to "));
    Serial.println(currentAbbrev);
  }
  
  /* Update for next time */
  previousTZAbbrev = currentAbbrev;
}

/*********************************/
/* Get current local time and TZ */
/*********************************/
time_t getLocalTimeWithAbbrev(const char** abbrev)
{
  time_t utc = time(NULL);
  TimeChangeRule *tcr = nullptr; /* Pointer to active DST rule */
  time_t local = myTZ.toLocal(utc, &tcr);
  if (abbrev) *abbrev = tcr->abbrev;
  logTimezoneChange(tcr->abbrev); /* Serial log if DST changes */
  return local;
}

/*************************************/
/* Format NTP timestamp as date/time */
/*************************************/
String formatNTPTime(time_t t, const char* tzAbbrev = nullptr)
{
  if (t == 0) return "Never";
  struct tm *tm = localtime(&t);
  char buf[48];
  /* Output time string with TZ abbreviation */
  if (tzAbbrev && tzAbbrev[0] != '\0')
  {
    /* customise output here if you want */
    /*                          DD/Mmm/YYYY HH:MM:SS (AEDT) */
    strftime(buf, sizeof(buf), "%d/%b/%Y %H:%M:%S", tm);
    strcat(buf, " (");
    strcat(buf, tzAbbrev);
    strcat(buf, ")");
  }
  else /* No TZ abbreviation available */
  {
    strftime(buf, sizeof(buf), "%d/%b/%Y %H:%M:%S", tm);
  }
  return String(buf);
}

/*****************/
/* Sync NTP time */
/*****************/
void syncNTPTime()
{
  Serial.print(">> Syncing NTP time (pool.ntp.org) ... ");
  /* Get GMT/UTC only */
  configTime(0, 0, ntpServer);
  /* Give it a few seconds to get a valid time */
  for (int i = 0; i < 25; i++) 
  {
    const char* tzAbbrev = nullptr;
    time_t localTime = getLocalTimeWithAbbrev(&tzAbbrev);
    if (localTime > 1000000000UL) 
    {   /* Valid Unix epoch */
      Serial.print(formatNTPTime(localTime, tzAbbrev));
      Serial.println();
      ntpTimeSynced = true; /* We should have a valid TZ now */
      return;
    }
    delay(200); /* 200ms */
    handleHttpClients();
  }
  Serial.println("failed, no valid time yet");
}

/******************************/
/* Serve the HTTP status page */
/******************************/
void serveHttpPage(WiFiClient &client)
{
  const char* tzAbbrev = nullptr;
  time_t localTime = getLocalTimeWithAbbrev(&tzAbbrev);
  
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

  /* Actual page text starts here */
  client.print(F("<h1><u>Brett's Router Rebooter (")); client.print(versionDate); client.print(F(")</u>"));
#ifdef DEBUG_MODE
  client.println(F(" *DEBUG MODE*"));
#endif
  client.println(F("</h1>"));
  
  /* Advise about auto-refresh of page */
  client.print(F("(This page will refresh every "));
  client.print((PING_TIMER / 1000) + 3 );
  client.println(F("s)"));
  
  /* Link to my Github repository*/
  client.print(F("<p><a href=\""));
  client.print(githubRepo);
  client.print(F("\" target=\"_blank\">Github</a></p>"));

  /* Table listing our connection info */
  client.print(F("<h2>Connection</h2>"));
  client.println(F("<table>"));
  /* SSID of configured WiFi access point */
  client.print(F("<tr><th>WiFi AP name</th><td>")); client.print(ssid); client.println(F("</td></tr>"));
  /* Our IP address */
  client.print(F("<tr><th>IP address</th><td>")); client.print(WiFi.localIP()); client.println(F("</td></tr>"));
  /* The router we are monitoring */
  client.print(F("<tr><th>Monitored router</th><td>")); 
  if (myRouterAdminPage != "")
  { /* Link to your router's admin page */
    client.print(F("<p><a href=\""));
    client.print(myRouterAdminPage);
    client.print(F("\" target=\"_blank\">"));
    client.print(myRouter); 
    client.print(F("</a></p>"));
  }
  else
  {
    client.print(myRouter); 
  }
  client.println(F("</td></tr>"));
  /* The current time */
  client.print(F("<tr><th>Current time (NTP)</th><td>")); client.print(formatNTPTime(localTime, tzAbbrev)); client.println(F("</td></tr>"));
  /* When we last rebooted the router (time ago & time stamp) */
  client.print(F("<tr><th>Last router reboot</th><td>"));   client.print(timeAgo(lastRebootTime)); 
  if (lastRebootNTP > MIN_VALID_TIME)
  { /* Only print timestamp if there actually is one */
    TimeChangeRule *tcr = nullptr;
    time_t localRebootTime = myTZ.toLocal(lastRebootNTP, &tcr);
    client.print(F(" at ")); 
    client.print(formatNTPTime(localRebootTime, tcr->abbrev)); 
  }
  client.println(F("</td></tr>"));
  /* How many times have we power cycled the router */
  client.print(F("<tr><th>Reboot count</th><td>")); client.print(rebootCount); client.println(F("</td></tr>"));
  /* When the ESP32 was last reset/rebooted */
  client.print(F("<tr><th>Last ESP32 reboot</th><td>")); client.print(timeAgo(esp32UpTime)); client.println(F("</td></tr>"));
  client.println(F("</table></p>"));

  /* Our current monitoring configuration */
  client.print(F("<h2>Timers</h2>"));
  client.println(F("<table>"));
  /* Time between pings */
  client.print(F("<tr><th>Ping timer</th><td>")); client.print(PING_TIMER/1000); client.println(F("s</td></tr>"));
  /* How many ping retries before we take action */
  client.print(F("<tr><th>Ping retries</th><td>")); client.print(PING_RETRIES); client.println(F("</td></tr>"));
  /* Time between ping retries */
  client.print(F("<tr><th>Ping retry timer</th><td>")); client.print(PING_RETRY_TIMER/1000); client.println(F("s</td></tr>"));
  /* How long we keep the router off before powering back on */
  client.print(F("<tr><th>Power cycle timer</th><td>")); client.print(POWER_CYCLE_TIME); client.println(F("s</td></tr>"));
  /* How long we wait after powering the router back on before we resume our monitoring */
  client.print(F("<tr><th>Recovery delay</th><td>")); client.print(RECOVERY_DELAY); client.println(F("s</td></tr>"));
  client.println(F("</table></p>"));

  /* Our OK/FAILED ping stats for each target address */
  client.println(F("<h2>Rechability statistics</h2>"));
  client.println(F("<table><tr><th>Target address</th><th>Target name</th><th>OK pings</th><th>Failed pings</th><th>Last failed ping</th></tr>"));

  /* Output each target's statistics */
  for (int i = 0; i < NUM_DNS; i++)
  {
    /* Target IP address */
    client.print(F("<tr><td>"));
    client.print(dnsList[i]);
    /* Target name */
    client.print(F("</td><td>"));
    client.print(dnsName[i]);
    /* How many OK pings */
    client.print(F("</td><td>"));
    client.print(okCount[i]);
    /* How many failed pings */
    client.print(F("</td><td>"));
    client.print(failCount[i]);
    client.print(F("</td><td>"));
    /* Only print the failed timestamp if there is one and it's valid */
    if (lastFailedPing[i] > MIN_VALID_TIME)
    {
      TimeChangeRule *tcr = nullptr;
      time_t localFailedTime = myTZ.toLocal(lastFailedPing[i], &tcr); /* Convert from GMT/UTC to local time */
      client.print(formatNTPTime(localFailedTime, tcr->abbrev));
    }
    client.println(F("</td></tr>"));
  }
  client.println(F("</table></p>"));

  /* Print last few logs to HTTP status page as well */
  // client.print(F("<h2>Last ");
  // client.print(MAX_LOG_LINES);
  // client.println(" debug logs</h2>"));
  // client.println(F("<pre style=\"background:#f4f4f4;padding:10px;font-family:monospace;font-size:0.9em;overflow:auto;max-height:300px;\">"));
  
  // for (int i = 0; i < MAX_LOG_LINES; i++)
  // {
  //   int idx = (logHead + i) % MAX_LOG_LINES;
  //   if (logBuffer[idx].length() > 0)
  //     client.println(logBuffer[idx]);
  // }
  // client.println(F("</pre>"));

  client.println(F("</body></html>"));
  
  /* Ensure the whole page is sent before closing */
  client.flush();
}

/*************************************/
/* Log print to serial & status page */
/* Not currently used, maybe later   */
/*************************************/
// void logPrintln(const String& msg)
// {
//   Serial.println(msg);                     /* Output log to serial port*/
//   logBuffer[logHead] = msg;                /* Add it to the buffer for the status page */
//   logHead = (logHead + 1) % MAX_LOG_LINES; /* Circular wrap-around */
// }

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
    delay(200); /* 200ms */
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
  const char* name = dnsName[currentIPIndex];

  Serial.print(F(">> Pinging "));

  /* Convert IP to String and pad to a fixed width */
  String ipStr = currentTarget.toString();
  Serial.print(ipStr);
  for (int i = ipStr.length(); i < 17; i++) 
  {
    Serial.print(' ');
  }

  /* Print the name and pad to a fixed width */
  Serial.print(name);
  for (int i = strlen(name); i < 21; i++) 
  {  
    Serial.print(' ');
  }

  Serial.print(F("... "));

  if (Ping.ping(currentTarget, 1)) 
  {
    Serial.println(F("OK!"));
    okCount[currentIPIndex]++;
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;
    pixels.setPixelColor(0, pixels.Color(0, 0, 0)); /* ESP32 LED off */
    pixels.show();
    return true;
  } 
  else 
  {
    Serial.println(F("failed!"));
    failCount[currentIPIndex]++;
    lastFailedPing[currentIPIndex] = time(NULL);      /* Store UTC */
    currentIPIndex = (currentIPIndex + 1) % NUM_DNS;
    pixels.setPixelColor(0, pixels.Color(0, 255, 0)); /* Red */
    pixels.show();
    return false;
  }
}

/****************/
/* All is well! */
/****************/
void setNormalState() 
{
  digitalWrite(OK_LED_PIN,      HIGH);            /* OK LED is on       */
  digitalWrite(FAILURE_LED_PIN, LOW);             /* FAILURE LED is off */
  pixels.setPixelColor(0, pixels.Color(0, 0, 0)); /* ESP23 LED is off   */
  pixels.show();
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

  /* Attempt to connect */
  WiFi.begin(ssid, password);

  /* Flash the ESP32 LED green/blue until we connect */
  while (WiFi.status() != WL_CONNECTED) 
  {
    /* Set green */
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    delay(250); /* 250ms */
    /* Set blue */
    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
    pixels.show();
    delay(250); /* 250ms */
    /* Output WiFi status to serial console */ 
    printWiFiStatus(); 
  }

  Serial.print(">> Connected to <"); Serial.print(ssid); Serial.print("> with address <"); Serial.print(WiFi.localIP()); Serial.println(">");

  /* Turn off ESP32 built-in LED */
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  /* Start up HTTP server for status page requests */
  httpServer.begin();
  Serial.println(">> HTTP status page started on port 80");
  Serial.print("   Open in browser: http://");
  Serial.println(WiFi.localIP());

  rebootInProgress = false;

  /* Initial NTP sync after WiFi is up */
  syncNTPTime();
}

/***************************/
/********** SETUP **********/ 
/***************************/
void setup() 
{
  Serial.begin(115200);
  delay(1000); /* 1s */
  Serial.println(F("\n\n>> Brett's Router Rebooter Starting!"));
  Serial.print("   ("); Serial.print(versionDate); Serial.println(")");
  Serial.print(F(">> Compiled ")); Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);
  Serial.print(F(">> Monitoring ")); Serial.println(myRouter);

  /* Capture our start up time */
  esp32UpTime = millis();

#ifdef DEBUG_MODE
  Serial.println(F(">> Timer/retry settings (DEBUG):"));
#else
  Serial.println(F(">> Timer/retry settings:"));
#endif
  Serial.print(F("   Ping timer .......... ")); Serial.print(PING_TIMER/1000); Serial.println("s");
  Serial.print(F("   Ping retries ........ ")); Serial.println(PING_RETRIES);
  Serial.print(F("   Ping retry timer .... ")); Serial.print(PING_RETRY_TIMER/1000); Serial.println("s");
  Serial.print(F("   Power cycle timer ... ")); Serial.print(POWER_CYCLE_TIME); Serial.println("s");
  Serial.print(F("   Recovery delay ...... ")); Serial.print(RECOVERY_DELAY); Serial.println("s");
  Serial.print(F("   MIN_VALID_TIME ...... ")); Serial.println(MIN_VALID_TIME);

  /* Configure our output pins */
  pinMode(RELAY_PIN,           OUTPUT);           /* relay control pin             */
  digitalWrite(RELAY_PIN,      RELAY_DEENERGISE); /* ... de-energised, power is on */
  pinMode(OK_LED_PIN,          OUTPUT);           /* OK LED                        */
  digitalWrite(OK_LED_PIN,     HIGH);             /* ... is on                     */
  pinMode(FAILURE_LED_PIN,     OUTPUT);           /* FAILURE LED                   */
  digitalWrite(FAILURE_LED_PIN,LOW);              /* ... is off                    */

  /* Initialise the ESP32's built-in LED */
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
  /* Check for browser requests at the start of every loop */
  handleHttpClients(); 

  /* Reconnect after every power-cycle */
  connectToWiFi();

  /* Allow HTTP GET requests again (kernel panic otherwise) */
  rebootInProgress = false;
  while (true) 
  {
    /* Keep the web page responsive inside the monitoring loop */
    handleHttpClients(); 

    /* Daily NTP re-sync at 05:00 local time */
    const char* dummy = nullptr;
    time_t local = getLocalTimeWithAbbrev(&dummy);
    struct tm timeinfo;
    localtime_r(&local, &timeinfo);   // use local time
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

    /* Test our Internet reachability */
    if (performPing()) 
    {
      setNormalState();

      /* Non-blocking so HTTP requests are handled quickly */
      waitWithHttpCheck(PING_TIMER);
      continue;
    }

    /* The last ping failed, indicate via LEDs */
    setFailureState();

    /* Start ping re-tries */
    bool recovered = false;
    for (int i = 0; i < PING_RETRIES; i++) 
    {
      /* Non-blocking so HTTP requests are handled quickly */
      waitWithHttpCheck(PING_RETRY_TIMER);
      if (performPing()) 
      {
        recovered = true;
        break;
      }
    }

    /* Next ping was successful, phew! */
    if (recovered) 
    {
      setNormalState();

      /* Non-blocking so HTTP requests are handled quickly */
      waitWithHttpCheck(PING_TIMER); 
      continue;
    }

    /* Still dead after retries, power cycle */
    rebootInProgress = true; /* Needed to avoid kernel panic after router power cycling & WiFi disconnect/reconnect */
    httpServer.stop();       /* ... and this */
    Serial.println("!! No response after retries, disconnecting router power.");
    /* Energise the relay ... opens ... disconnect router power */
    digitalWrite(RELAY_PIN, RELAY_ENERGISE);
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();

    /* Record both millis and timestamp */
    lastRebootTime = millis();
    lastRebootNTP = time(NULL); /* Store as GMT/UTC, not local time */
    rebootCount++;

    /* Keep router off for a short period to ensure we clear everything from RAM */
    Serial.print(">> Waiting "); Serial.print(POWER_CYCLE_TIME); Serial.println("s before reconnecting power ...");
    int COUNT_DOWN_STEP = 5; /* Update every 5s */
    for (int seconds = POWER_CYCLE_TIME; seconds > 0; seconds-=COUNT_DOWN_STEP)
    {
      /* Keep web page alive during countdown - REMOVED due to kernel panics during WiFi reconnect */
      // handleHttpClients(); 
      Serial.print("   "); Serial.print(seconds); Serial.println("s");
      for (int i = 0; i < COUNT_DOWN_STEP; i++)
      {
        /* This accounts for 1s */
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));
        pixels.show();
        delay(500); /* 500ms */
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.show();
        delay(500); /* 500ms */
      }
    }
    Serial.println(">> Powering router back on.");
    digitalWrite(RELAY_PIN, RELAY_DEENERGISE);

    /* Wait for router to re-connect to ISP before resuming monitoring */
    Serial.print(">> Waiting "); Serial.print(RECOVERY_DELAY); Serial.println("s before resuming monitoring ...");
    for (int seconds = RECOVERY_DELAY; seconds > 0; seconds-=(COUNT_DOWN_STEP*3)) /* Update every 15s */
    {
      /* Keep web page alive during countdown - REMOVED due to kernel panics during WiFi reconnect */
      // handleHttpClients(); 
      Serial.print("   ");  Serial.print(seconds);  Serial.println("s");
      for (int i = 0; i < (COUNT_DOWN_STEP*3); i++) 
      {
        /* This accounts for 1s */
        digitalWrite(OK_LED_PIN, HIGH);                   /* OK on       */
        digitalWrite(FAILURE_LED_PIN, LOW);               /* FAILURE off */
        pixels.setPixelColor(0, pixels.Color(255, 0, 0)); /* Green       */
        pixels.show();
        delay(500); /* 500ms */
        digitalWrite(OK_LED_PIN, LOW);                    /* OK off     */
        digitalWrite(FAILURE_LED_PIN, HIGH);              /* FAILURE on */
        pixels.setPixelColor(0, pixels.Color(0, 255, 0)); /* Red        */
        pixels.show();
        delay(500); /* 500ms */
      }
    }

    Serial.println(">> Router recovery finished – resuming monitoring.");
    Serial.println();
    Serial.println();

    setNormalState();
    
    WiFi.disconnect(true);
    delay(2000); /* 2s */
    break;
  }
}
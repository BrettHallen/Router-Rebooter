# Firmware (Arduino Sketch)
## Overall Functionality
Coming ...

### Daylight savings change
The sketch will automatically change the timezone to or from daylight savings according to the rule defined:
```
/* TZ/DST setting for NSW, Australia **********************************************************************/
TimeChangeRule myDST = {"AEDT", First, Sun, Oct, 2, 660};    /* GMT+11 hours from 02:00 first Sunday Oct  */
TimeChangeRule mySTD = {"AEST", First, Sun, Apr, 3, 600};    /* GMT+10 hours from 03:00 first Sunday Apr  */
```

If your area doesn't have DST then simply set both myDST and mySTD as the same with a dummy changeover date:
```
/* Example: TZ setting for non-DST location, i.e. QLD, Australia ******************************************/
// TimeChangeRule myDST = {"AEST", First, Sun, Jan, 0, 600}; /* Always GMT+10                             */
// TimeChangeRule mySTD = {"AEST", First, Sun, Jan, 0, 600}; /* Always GMT+10                             */
```

Here is an example when DST ended in the first Sunday in April at 03:00 AEDT here in NSW, Australia:
```
>> Pinging 8.8.8.8          Google primary       ... OK!
>> Pinging 8.8.4.4          Google secondary     ... OK!
>> Pinging 9.9.9.9          Quad9 primary        ... OK!
>> Timezone changed: AEDT to AEST
>> Pinging 149.112.112.112  Quad9 secondary      ... OK!
>> Pinging 4.2.2.1          Level 3 primary      ... OK!
>> Pinging 4.2.2.2          Level 3 secondary    ... OK!
```


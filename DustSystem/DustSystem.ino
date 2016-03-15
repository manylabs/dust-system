// This sketch has been testing using Arduino 1.0.5; it may not work with older or newer Arduino IDE versions.


// main configuration
#define USE_WIFI
//#define USE_GSM
//#define USE_SD
//#define ENABLE_WDT


#include "SoftwareSerial.h"
#include "ChainableLED.h"
#include "sha256.h"
#include "DustSensor.h"
#include "ManylabsDataAuth.h"
#include "DHT.h"
#ifdef USE_WIFI
#include "WiFly.h"
#define WIFI_POST_URL "http://www.manylabs.org/data/api/v1/appendData/"
#include "WifiSender.h"
#include "HTTPClient.h"
#endif
#ifdef USE_GSM
#include "GprsSender.h"
#endif
#ifdef USE_SD
#include "SD.h"
#endif
#ifdef ENABLE_WDT
#include "avr/wdt.h"
#endif


// ======== SETTINGS ========


// pin definitions
#define DHT_PIN 4
#define LED_PIN 6
#define SD_PIN 8
#define DUST_SENSOR_COUNT 6
#define BATTERY_VOLTS_PIN A0
const int dustPins[] = { 2, 3, 18, 19, 20, 21 };


// WIFI settings
#define NETWORK_NAME "x"
#define NETWORK_PASSWORD "x"
const char contentTypeHeader[] PROGMEM = "Content-Type: application/x-www-form-urlencoded\r\n";


// Manylabs data API settings
#define PUBLIC_KEY "x"
#define PRIVATE_KEY "x"
#define DATA_SET_ID 0


// GSM settings
//#define GPRS_RESET_PIN 4
#define APN "truphone.com" // Set to the APN for your sim card


// ======== GLOBAL DATA ========


// wifi connection objects/data
#ifdef USE_WIFI
WifiSender g_wifiSender( Serial2, &Serial );
#define PARAM_BUF_SIZE 300
char g_wifiParamBuffer[ PARAM_BUF_SIZE ];
#define HEADER_BUFFER_LENGTH 200
char g_headerBuffer[ HEADER_BUFFER_LENGTH ];
#endif


// GSM connection objects/data
#ifdef USE_GSM
//SoftwareSerial g_gprsSerial( 4, 5 ); // use Serial2?
GprsSender g_gprsSender( GPRS_RESET_PIN, g_gprsSerial, Serial );
uint8_t g_gprsFailCount = 0;
#endif


#ifdef USE_SD
File g_sensorFile; // CSV of sensor data
boolean g_sensorFileReady = false;
#endif


// dust sensor objects and data
DustSensor g_dustSensors[ DUST_SENSOR_COUNT ];
float g_dustRatios[ DUST_SENSOR_COUNT ];


// other globals
ManylabsDataAuth g_dataAuth;
unsigned long g_lastSensorTime = 0;
unsigned long g_uptimeSeconds = 0; // seconds
unsigned long g_uptimeMSec = 0; // msec part
float g_temperature = 0;
float g_humidity = 0;
float g_batteryVolts = 0;
float g_signalStrength = 0;
ChainableLED g_led( LED_PIN, LED_PIN + 1, 1 );
DHT g_dht( DHT_PIN, DHT22 );


// ======== MAIN FUNCTIONS ========


// run this function once on startup
void setup() {

  // general startup
  Serial.begin( 9600 );
  Serial.println( "starting" );
  setLedHsl( 0, 0, 0 ); // off
  setLedHsl( 60, 1, 0.5 ); // yellow
  pinMode( DHT_PIN, INPUT );
  pinMode( DHT_PIN + 1, INPUT );
#ifdef ENABLE_WDT
  wdt_enable( WDTO_8S ); // enable watchdog timer with 8-second timeout
#endif

  // prep dust sensors
  for (int i = 0; i < DUST_SENSOR_COUNT; i++) {
    g_dustSensors[ i ].init( dustPins[ i ] );
  }
  attachInterrupt( digitalPinToInterrupt( g_dustSensors[ 0 ].pin() ), timeDustPulse0, CHANGE );
  attachInterrupt( digitalPinToInterrupt( g_dustSensors[ 1 ].pin() ), timeDustPulse1, CHANGE );
  attachInterrupt( digitalPinToInterrupt( g_dustSensors[ 2 ].pin() ), timeDustPulse2, CHANGE );
  attachInterrupt( digitalPinToInterrupt( g_dustSensors[ 3 ].pin() ), timeDustPulse3, CHANGE );
  attachInterrupt( digitalPinToInterrupt( g_dustSensors[ 4 ].pin() ), timeDustPulse4, CHANGE );
  attachInterrupt( digitalPinToInterrupt( g_dustSensors[ 5 ].pin() ), timeDustPulse5, CHANGE );

  // prep wifi
#ifdef USE_WIFI
  Serial2.begin( 9600 );
  g_dataAuth.init( F(PUBLIC_KEY), F(PRIVATE_KEY) );
  g_wifiParamBuffer[ 0 ] = 0;
  if (g_wifiSender.init( NETWORK_NAME, NETWORK_PASSWORD, g_wifiParamBuffer, PARAM_BUF_SIZE )) {
    Serial.println( "wifi init success" );
  } else {
    Serial.println( "wifi init failed" );
  }
#endif

  // prep GSM
#ifdef USE_GSM
  g_gprsSerial.begin( 19200 );
  g_dataAuth.init( F(PUBLIC_KEY), F(PRIVATE_KEY) );
  g_gprsSender.addManylabsDataAuth( &g_dataAuth );
  if (g_gprsSender.init( F(APN) )) {
    Serial.println( "GSM init success" );    
    setLedHsl( 120, 1, 0.5 ); // Green
  } else {
    Serial.println( "GSM init failed" );    
    setLedHsl( 0, 1, 0.5 ); // Red
  }
#endif

  // prep SD
#ifdef USE_SD
  Serial.println( F("initializing SD card") );
  pinMode( SD_PIN, OUTPUT );
  if (!SD.begin( SD_PIN )) {
    Serial.println( "SD init failed" );
  } else {
    Serial.println( "SD init success" );
    char *fileName = "LOGX.CSV";
    fileName[ 3 ] = (analogRead( 0 ) & 31) + 'A';
    SD.remove( fileName );
    g_sensorFile = SD.open( fileName, FILE_WRITE );
    if (g_sensorFile) {
      Serial.print( "SD file open success: " );
      Serial.println( fileName );
      saveDataHeader();
      g_sensorFileReady = true;
      setLedHsl( 120, 1, 0.5 ); // Green
    } else {
      Serial.println( "SD file open failed" );
      setLedHsl( 0, 1, 0.5 ); // Red
    }
  }
#endif
}


// run repeatedly as long as arduino has power
void loop() {

  // reset the watchdog timer
#ifdef ENABLE_WDT
  wdt_reset();
#endif

  // read the sensor data
  unsigned long time = millis();
  if (time - g_lastSensorTime > 30000LL) { 
  
    // read sensors
    unsigned long elapsedTime = time - g_lastSensorTime;
    for (int i = 0; i < DUST_SENSOR_COUNT; i++) {
      g_dustRatios[ i ] = g_dustSensors[ i ].pulseRatio( elapsedTime );
    }
    g_temperature = g_dht.readTemperature();
    g_humidity = g_dht.readHumidity();
#ifdef USE_GSM
    g_signalStrength = g_gprsSender.signalStrength();
#endif
    g_batteryVolts = 0; //analogRead( BATTERY_VOLTS_PIN ) * 5.0 * 3.0 / 1023.0; // using voltage divider scale factor of 3 
    updateUptime();
    
    // display sensor values
    Serial.print( "time: " );
    Serial.print( g_uptimeSeconds );
    Serial.print( ", temp: " );
    Serial.print( g_temperature );
    Serial.print( ", batt: " );
    Serial.print( g_batteryVolts );
    Serial.print( ", sig: " );
    Serial.print( g_signalStrength );
    Serial.print( ", dust: " );
    for (int i = 0; i < DUST_SENSOR_COUNT; i++) {
      if (i) {
        Serial.print( ", " );
      }
      Serial.print( g_dustRatios[ i ], 3 );
    }
    Serial.println();

    // send/save sensor values after the first iteration
    if (time > 90000LL) {
#ifdef USE_WIFI
      sendWifiData();
#endif
#ifdef USE_GSM
      sendGsmData();
#endif
#ifdef USE_SD
      saveData();
#endif
    }
    g_lastSensorTime = time;
  }
}


// ======== SEND DATA TO SERVER ========


// send data to server via WiFi
#ifdef USE_WIFI
void sendWifiData() {
  Serial.println(F("Adding Data"));
  g_wifiSender.add( F("dataSetId"), DATA_SET_ID );
  g_wifiSender.add( F("addTimestamp"), 1 );
  g_wifiSender.add( F("uptime"), (float) g_uptimeSeconds / 86400000.0, 3 );
  g_wifiSender.add( F("temperature"), g_temperature, 2 );
  g_wifiSender.add( F("humidity"), g_humidity, 2 );
  g_wifiSender.add( F("ppd42_1"), g_dustRatios[ 0 ], 5 );
  g_wifiSender.add( F("ppd42_2"), g_dustRatios[ 1 ], 5 );
  g_wifiSender.add( F("ppd42_3"), g_dustRatios[ 2 ], 5 );
  g_wifiSender.add( F("ppd60_1"), g_dustRatios[ 3 ], 4 );
  g_wifiSender.add( F("ppd60_2"), g_dustRatios[ 4 ], 4 );
  g_wifiSender.add( F("ppd60_3"), g_dustRatios[ 5 ], 4 );

  // Setup header
  Serial.println(F("Creating Header"));
  g_headerBuffer[0] = 0;

  // Copy in contentTypeHeader
  strlcpy_P(g_headerBuffer, contentTypeHeader, HEADER_BUFFER_LENGTH);

  // Write auth header
  g_dataAuth.reset();
  g_dataAuth.print(g_wifiParamBuffer);
  g_dataAuth.writeAuthHeader(g_headerBuffer, HEADER_BUFFER_LENGTH);
  Serial.println(g_headerBuffer);

  Serial.println(F("Sending"));
#ifdef ENABLE_WDT
  wdt_reset();
#endif
  if(g_wifiSender.send(g_headerBuffer)){
    setLedHsl( 120, 1, 0.5 ); // Green
    Serial.println(F("Success"));
  }else{
    setLedHsl( 0, 1, 0.5 ); // Red
    Serial.println(F("Failure"));
  }
#ifdef ENABLE_WDT
  wdt_reset();
#endif
}
#endif


// Takes an AQSensorValues object and adds the values it contains to the gprs
// sender. If includeDiagnostics is true, it also adds diagnostic values.
// Depending on the state of the gprs sender, this is either for the purpose of
// determining the content-length, or for adding the data to be sent.
#ifdef USE_GSM
void addGsmData() {
  g_gprsSender.add( F("dataSetId"), DATA_SET_ID );
  g_gprsSender.add( F("addTimestamp"), 1 );
  g_gprsSender.add( F("uptime"), (float) g_uptimeSeconds / 86400000.0, 2 );
  g_gprsSender.add( F("temperature"), g_temperature, 2 );
  g_gprsSender.add( F("humidity"), g_humidity, 2 );
  g_gprsSender.add( F("battery_volts"), g_batteryVolts, 3 );
  g_gprsSender.add( F("signal_strength"), g_signalStrength );
  g_gprsSender.add( F("ppd42_1"), g_dustRatios[ 0 ], 4 );
  g_gprsSender.add( F("ppd42_2"), g_dustRatios[ 1 ], 4 );
  g_gprsSender.add( F("ppd42_3"), g_dustRatios[ 2 ], 4 );
  g_gprsSender.add( F("ppd60_1"), g_dustRatios[ 3 ], 4 );
  g_gprsSender.add( F("ppd60_2"), g_dustRatios[ 4 ], 4 );
  g_gprsSender.add( F("ppd60_3"), g_dustRatios[ 5 ], 4 );
}
#endif


#ifdef USE_GSM
void sendGsmData() {

  // Add data to generate auth and content-length headers. This doesn't actually
  // add the data to the request body until we call prepare to send.
  addData();

  bool error = true; // We'll set this to false if send is successful

  if( g_gprsSender.prepareToSend() ){
    // Now when we call add, we're adding the data to the body of the request.
    addData();

    Serial.println(); // Blank line
    Serial.println( F("Sending") );
    setLedHsl( 240,1,0.5 ); // Blue

    if (g_gprsSender.send()) {

      // We don't consider a status code other than 201 an error (nothing we can
      // do about it here), so set this before checking the status code.
      error = false;

      int statusCode = g_gprsSender.lastStatusCode(); // Get HTTP response code
      if (statusCode == 201) { // OK
        setLedHsl( 120, 1, 0.5 ); // Green - Everything's ok
        Serial.println( "success" );
      } else {
        Serial.println( F("Status: ") );
        Serial.println( statusCode );
        setLedHsl( 29, 1, 0.5 ); // Orange - Didn't get a 201 status
      }
    }
  }

  if (error) { // prepareToSend or send failed
    int errorCause = g_gprsSender.lastErrorCode();
    if (errorCause == 2) {
      Serial.println( F("Network Fail") );
      setLedHsl( 300,1,0.5 ); // Magenta - TCP Failed
    } else {
      Serial.println( F("GPRS Fail") );
      setLedHsl( 0,1,0.5 ); // Red - GPRS Failed
    }

    // If we have repeated problems with the gprs sender, reboot and reconnect
    g_gprsFailCount++;
    if (g_gprsFailCount > 1) {
      g_gprsFailCount = 0;
      rebootAndReconnect();
    }
  }
}
#endif


#ifdef USE_GSM
// Reboot the gprs module and reconnect to the GSM network
void rebootAndReconnect() {
  Serial.println( F("rebooting") );
  g_gprsSender.reboot();
  Serial.println( F("reconnecting") );
  if (g_gprsSender.waitForNetworkReg()) {
    Serial.println( "success" );
  } else {
    Serial.println( F("fail") );
  }
}
#endif


// ======== SAVE DATA ON SD CARD ========


#ifdef USE_SD
void saveDataHeader() {
  g_sensorFile.print( F("timestamp,temperature,humidity,battery_volts,signal_strength,") );
  g_sensorFile.println( F("ppd42_1,ppd42_2,ppd42_3,ppd60_1,ppd60_2,ppd60_3") );
  g_sensorFile.flush();
  Serial.println( "wrote headers" );
}
#endif



#ifdef USE_SD
void saveData() {
  if (g_sensorFileReady) {
    g_sensorFile.print( g_uptimeSeconds );
    g_sensorFile.print( "," );
    g_sensorFile.print( g_temperature );
    g_sensorFile.print( "," );
    g_sensorFile.print( g_humidity );
    g_sensorFile.print( "," );
    g_sensorFile.print( g_batteryVolts );
    g_sensorFile.print( "," );
    g_sensorFile.print( g_signalStrength );
    for (int i = 0; i < DUST_SENSOR_COUNT; i++) {
      g_sensorFile.print( "," );
      g_sensorFile.print( g_dustRatios[ i ], 4 );
    }
    g_sensorFile.println();
    g_sensorFile.flush();
    Serial.println( "wrote data" );
  }
}
#endif


// ======== HELPER FUNCTIONS ========


// interrupt handler for dust sensor - times low pulse occupancy
void timeDustPulse0( void ) { g_dustSensors[ 0 ].change(); }
void timeDustPulse1( void ) { g_dustSensors[ 1 ].change(); }
void timeDustPulse2( void ) { g_dustSensors[ 2 ].change(); }
void timeDustPulse3( void ) { g_dustSensors[ 3 ].change(); }
void timeDustPulse4( void ) { g_dustSensors[ 4 ].change(); }
void timeDustPulse5( void ) { g_dustSensors[ 5 ].change(); }


// Sets the color of the RGB LED using HSL color format.
// h: Hue - between 0 and 360
// s: Saturation - between 0 and 1
// l: Lightness - between 0 and 1
// If persist is true, the color will be stored and flashed periodically.
//
// Note: l = 0.5 is the "brightest" the led can get for a given color before
// moving towards white. At l = 1 all colors will show as white.
void setLedHsl( int h, float s, float l ) {

  // 1 / 360 = 0.00278
  float convertedHue = h * 0.00278;

  // Seeed lists this as HSB, but it's really HSL
  g_led.setColorHSB( 0, convertedHue, s, l );
}


// not need with newer Arduino IDE
byte digitalPinToInterrupt( byte pin ) {
  switch (pin) {
  case 2: return 0; // uno/mega
  case 3: return 1; // uno/mega
  case 21: return 2; // mega
  case 20: return 3; // mega
  case 19: return 4; // mega
  case 18: return 5; // mega
  } 
  return -1;
}


// compute how long the device has been active
void updateUptime() {
  static unsigned long s_lastUptimeCheck = 0;
  unsigned long now = millis();
  g_uptimeMSec += (now - s_lastUptimeCheck);
  g_uptimeSeconds += g_uptimeMSec / 1000;
  g_uptimeMSec = g_uptimeMSec % 1000;
  s_lastUptimeCheck = now;
}


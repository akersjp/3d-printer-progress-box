/* TODO
    4.  When print is done, right now the ring stays GREEN all the way 100%.  This is probably fine until you start a new print OR turn off the printer OR disconnect
    1.  Progress went starting print, then canceling
    2.  When printer is shut off, reset progress to OFF
    3.  Filament change - could I flash all the LED's RED or BLUE or something...would be cool...
    7.  When print was done, I turned off the printer.  All lgiths in the corners turned off, but the RING, only the bottom LED turned off, the rest stayed green.
    8.  Print finished...so top to LEDs on and the ring was all green.  Turned off the printer and the first ring light turned off, but the rest of the ring stayed green.  This should have shut off.
*/

/*
  - Include custom configuration file that defines WIFI_SSID and WIFI_PASSWORD
  - This is excluded from source control (.gitignore)
*/
#include "configuration.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <FastLED.h>

/*
 * Override these with your information or create a configuration.h that isn't source controlled (.gitignore)
*/
#if !(defined(WIFI_SSID))
#define WIFI_SSID "YOUR SSID"
#endif
#if !(defined(WIFI_PASSWORD))
#define WIFI_PASSWORD "YOUR SSID PASSWORD"
#endif
#if !(defined(OCTOPRINT_ADDRESS))
#define OCTOPRINT_ADDRESS "YOUR OCTOPRINT IP ADDRESS"
#endif
#if !(defined(OCTOPRINT_PORT))
#define OCTOPRINT_PORT "YOUR OCTOPRINT PORT"
#endif
#if !(defined(OCTOPRINT_API_KEY))
#define OCTOPRINT_API_KEY "X-Api-Key: YOUR OCOTPRINT API KEY"
#endif

// ISR timer for updating the LED's
int timer1_counter;

// PWM Brightness
#define PWM_BRIGHTNESS 5

// loop delay for making REST calls to octoprint
#define LOOP_DELAY 7

// DIGITAL PINS
#define POWER_WIFI_STATUS_LED 5
#define PRINTER_STATUS 15
#define BED_STATUS_LED 4
#define NOZZLE_STATUS_LED 0

// LED Ring Light
const int PROGRESS_LING_LED = 2;
#define NUM_LEDS 12
#define CLOCK_PIN 13
#define RING_BRIGHTNESS 50
CRGB leds[NUM_LEDS];

WiFiClient client;

// REST COMMANDS
#define HTTP_PRINTER_ENDPOINT "/api/printer"
#define HTTP_JOB_ENDPOINT "/api/job"
#define HTTP_GET "GET "
#define HTTP_HTTP_HEADER " HTTP/1.1"

enum job_state {
  JOB_OPERATIONAL, // sitting idle
  JOB_PRINTING, // actually printing
};

enum printer_status {
  PRINTER_OFF,
  PRINTER_ON
};

enum bed_status {
  BED_OFF,
  BED_WARMING,
  BED_READY
};

enum nozzle_status {
  NOZZLE_OFF,
  NOZZLE_WARMING,
  NOZZLE_READY
};

enum printer_status printerStatus;
enum bed_status bedStatus;
enum nozzle_status nozzleStatus;
enum job_state jobState;
int print_progress = 0;
bool clearLastPrint = false;

void setup() {
  Serial.begin(115200);
  // Delay otherwise we miss these log messages while Serial output initiatlizes
  delay(2000);
  Serial.println("Setup...");

  // Initialize pins
  initPins();

  // setup ring light
  // I have NO idea why I have to call show() twice...some posts about this and it might be specific
  //  to the ring lights I'm using
  FastLED.addLeds<WS2811,PROGRESS_LING_LED, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(RING_BRIGHTNESS);
  //eventually remove these and roll into updateLEDS() 
  FastLED.show();
  FastLED.show();

  // set initial states
  printerStatus = PRINTER_OFF;
  bedStatus = BED_OFF;
  nozzleStatus = NOZZLE_OFF;
  jobState = JOB_OPERATIONAL;

  // get ISR firing to update LED's
  setupTimers();

  updateLEDs();

  setupWifi();
}

void setupWifi() {
  Serial.println("setupWifi()");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("setupWifi() : Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("SetupWifi() : IP Address: ");
  Serial.println(WiFi.localIP());
}

void setupTimers() {
  Serial.println("setupTimers()");
  
    // Setup Timers
  noInterrupts(); // disable all interrupts
  timer0_isr_init();
  timer0_attachInterrupt(ISR);
  timer0_write(ESP.getCycleCount() + 80000000L * 1);
  interrupts();
}

void ISR(void) // interrupt service routine
{
  timer0_write(ESP.getCycleCount() + 80000000L * 1);

  //update all LED states
  updateLEDs();
}

void updateLEDs() {
  updateWifiLED();
  updatePrinterLED();
  updateBedLED();
  updateNozzleLED();
  
  // update ring light progress - once I'm getting state correclty, I can take the init out of setup and just rely on this
  updateProgressRingLED();
}

void updateProgressRingLED() {
  Serial.print("updateProgressRingLED() : progress = ");
  Serial.println(print_progress);
  Serial.print("updateProgressRingLED() : job state = ");
  Serial.println(jobState);

  // Check print state
  if(jobState == JOB_OPERATIONAL) {
    if(print_progress != NUM_LEDS) {
      Serial.println("updateProgressRingLED() print_progress isn't done, so clearing leds");
      fill_solid(leds,print_progress, CRGB::Black);
      FastLED.show();
      FastLED.show();
      return;
   } else {
      Serial.println("updateProgressRingLED() : ELSE case hit...");
      
      // This is the case that the print finished and I turned off the printer...so I know it's done, clear the LEDS
      if(printerStatus == PRINTER_OFF) {
        Serial.println("updateProgressRingLED() : PRINTER_OFF");
        // clear ring LEDs
        fill_solid(leds,print_progress, CRGB::Black);
        FastLED.show();
        FastLED.show();
        return;
      }
      
      Serial.println("updateProgressRingLED() print progress is done, keep the leds lit to show it's done...");
    }
  } else {
    // job state is printing
    if(bedStatus == BED_WARMING || nozzleStatus == NOZZLE_WARMING) {
      // we shouldn't show print status until we are done warming up.
      Serial.println("printer still warming up, don't show print status leds");
      fill_solid(leds,NUM_LEDS, CRGB::Black);
      FastLED.show();
      FastLED.show();
      return;
    }
  }

  if(clearLastPrint == true) {
    Serial.println("updateProgressRingLED() cleaning up last print leds");
    
      fill_solid(leds,NUM_LEDS, CRGB::Black);
      FastLED.show();
      FastLED.show();
      clearLastPrint = false;
  }
  
  fill_solid(leds,print_progress, CRGB::Green);
  
  // if we have room, blink the next LED to show print progress
  if(print_progress<NUM_LEDS) {
    // Have to do this due to overload operator issue as described here:  https://forum.arduino.cc/t/usage-of-crgb-black/371727
    CRGB test_color = CRGB::Black;
    // change from black to yellow
    if(leds[print_progress] == test_color) {
      leds[print_progress] = CRGB::Yellow;  
    } else {
      leds[print_progress] = CRGB::Black; 
    }
  }
   
  FastLED.show();
  FastLED.show();
}

void updatePrinterLED() {
  Serial.print("updatePrinterLED() : printer status = ");
  Serial.println(printerStatus);
  
  switch(printerStatus) 
  {
    case PRINTER_OFF:
      // set LED to OFF
      digitalWrite(PRINTER_STATUS, LOW);
      break;
    case PRINTER_ON:
      // set LED to GREEN
      analogWrite(PRINTER_STATUS, PWM_BRIGHTNESS);
      break;
    default:
      Serial.println("updatePrinterLED() default");
  }
}

void updateWifiLED() {
  Serial.print("updateWifiLED(): WiFi.status() = ");
  Serial.println(WiFi.status());
  
  switch(WiFi.status()) {
    case WL_CONNECTED:
      // set LED to GREEN
      //digitalWrite(POWER_WIFI_STATUS_LED, HIGH);
      analogWrite(POWER_WIFI_STATUS_LED, PWM_BRIGHTNESS);
      break;
    default:
      // blink LED
      digitalWrite(POWER_WIFI_STATUS_LED, !digitalRead(POWER_WIFI_STATUS_LED));
  }
}

void updateBedLED() {
  Serial.print("updateBedLED() : bed status = ");
  Serial.println(bedStatus);
  
  switch (bedStatus) {
    case BED_OFF:
      digitalWrite(BED_STATUS_LED, LOW);
      break;
    case BED_WARMING:
      // blink green
      digitalWrite(BED_STATUS_LED, !digitalRead(BED_STATUS_LED));
      break;
    case BED_READY:
      // set LED to GREEN
      analogWrite(BED_STATUS_LED, PWM_BRIGHTNESS);
      break;
  }
}

void updateNozzleLED() {
  Serial.print("setNozzleStatus: status = ");
  Serial.println(nozzleStatus);

  switch (nozzleStatus) {
    case NOZZLE_OFF:
      digitalWrite(NOZZLE_STATUS_LED, LOW);
      break;
    case NOZZLE_WARMING:
      // blink green
      digitalWrite(NOZZLE_STATUS_LED, !digitalRead(NOZZLE_STATUS_LED));
      break;
    case NOZZLE_READY:
      // set LED to GREEN
      analogWrite(NOZZLE_STATUS_LED, PWM_BRIGHTNESS);
      break;
  }
}

bool isWifiConnected() {
  Serial.print("isWifiConnected() : returning ");
  Serial.println(WiFi.status());
  
  if(WiFi.status() != WL_CONNECTED) {
    return false;
  }

  return true;
}

void initPins() {
  // built in LED
  pinMode(LED_BUILTIN, OUTPUT);

  // LED Ring Light
  pinMode(PROGRESS_LING_LED, OUTPUT);

  // wifi led pins
  pinMode(POWER_WIFI_STATUS_LED, OUTPUT);

  // printer led pins
  pinMode(PRINTER_STATUS, OUTPUT);

  // bed led pins
  pinMode(BED_STATUS_LED, OUTPUT);

  // nozzle led pins
  pinMode(NOZZLE_STATUS_LED, OUTPUT);
}

void getLatestPrinterStatus() {
  if (!client.connect(OCTOPRINT_ADDRESS, OCTOPRINT_PORT)) {
    Serial.println("getLatestPrinterStatus(): FAILED to connect, setting all LED's to OFF");
    
    printerStatus = PRINTER_OFF;
    bedStatus = BED_OFF;
    nozzleStatus = NOZZLE_OFF;
    return;
  }

  yield();

  client.print(F(HTTP_GET));
  client.print(HTTP_PRINTER_ENDPOINT);
  client.println(F(HTTP_HTTP_HEADER));

  // Headers
  client.print(F("Host: "));
  client.println(OCTOPRINT_ADDRESS);

  client.println(OCTOPRINT_API_KEY);

  if (client.println() == 0)
  {
    Serial.println(F("Failed to send request"));
    printerStatus = PRINTER_OFF;
    bedStatus = BED_OFF;
    nozzleStatus = NOZZLE_OFF;
    return;
  }

  Serial.println(F("getLatestPrinterStatus() Successfully sent request!"));

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print(F("getLatestPrinterStatus() Unexpected response: "));
    Serial.println(status);
    printerStatus = PRINTER_OFF;
    bedStatus = BED_OFF;
    nozzleStatus = NOZZLE_OFF;
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders))
  {
    Serial.println(F("getLatestPrinterStatus() Invalid response"));
    printerStatus = PRINTER_OFF;
    bedStatus = BED_OFF;
    nozzleStatus = NOZZLE_OFF;
    return;
  }

  // This is probably not needed for most, but I had issues
  // with the Tindie api where sometimes there were random
  // characters coming back before the body of the response.
  // This will cause no hard to leave it in
  // peek() will look at the character, but not take it off the queue
  while (client.available() && client.peek() != '{')
  {
    char c = 0;
    client.readBytes(&c, 1);
    Serial.print(c);
    Serial.println("BAD");
  }

  //  // While the client is still availble read each
  //  // byte and print to the serial monitor
  //  while (client.available()) {
  //    char c = 0;
  //    client.readBytes(&c, 1);
  //    Serial.print(c);
  //  }

  DynamicJsonDocument doc(5000);
  DeserializationError error = deserializeJson(doc, client);

  if (!error) {
    bool printerState = doc["state"]["flags"]["operational"];
    float bed_actual = doc["temperature"]["bed"]["actual"];
    float bed_target = doc["temperature"]["bed"]["target"];

    float nozzle_actual = doc["temperature"]["tool0"]["actual"];
    float nozzle_target = doc["temperature"]["tool0"]["target"];

    // if we've told the bed to warm
    if (bed_target > 0) {
      if (bed_actual >= bed_target - 2) {
        bedStatus = BED_READY;
      } else {
        bedStatus = BED_WARMING;
      }
    } else {
      bedStatus = BED_OFF;
    }

    // if we've told the nozzle to warm
    if (nozzle_target > 0) {
      if (nozzle_actual >= nozzle_target - 2) {
        nozzleStatus = NOZZLE_READY;
      } else {
        nozzleStatus = NOZZLE_WARMING;
      }
    } else {
      nozzleStatus = NOZZLE_OFF;
    }

    Serial.print("getLatestPrinterStatus() bed_target: ");
    Serial.println(bed_target);
    Serial.print("getLatestPrinterStatus() bed_actual: ");
    Serial.println(bed_actual);

    Serial.print("getLatestPrinterStatus() nozzle_target: ");
    Serial.println(nozzle_target);
    Serial.print("getLatestPrinterStatus() nozzle_actual: ");
    Serial.println(nozzle_actual);
    
    Serial.print("getLatestPrinterStatus() printerState: ");
    Serial.println(printerState);
    if(printerState) {
      printerStatus = PRINTER_ON;
    } else {
      printerStatus = PRINTER_OFF;
    }
  } else {
    Serial.print(F("getLatestPrinterStatus() deserializeJson() failed: "));
    Serial.println(error.f_str());
    printerStatus = PRINTER_OFF;
    bedStatus = BED_OFF;
    nozzleStatus = NOZZLE_OFF;
    return;
  }
}

void updatePrintProgress() {
  Serial.println("updatePrintProgress()");
  
  if (!client.connect(OCTOPRINT_ADDRESS, OCTOPRINT_PORT)) {
    // can't connect
    return;
  }

  client.print(F(HTTP_GET));
  client.print(HTTP_JOB_ENDPOINT);
  client.println(F(HTTP_HTTP_HEADER));

  // Headers
  client.print(F("Host: "));
  client.println(OCTOPRINT_ADDRESS);

  client.println(OCTOPRINT_API_KEY);

  if (client.println() == 0)
  {
    Serial.println(F("updatePrintProgress() Failed to send request"));
    return;
  }

  Serial.println(F("updatePrintProgress() Successfully sent request!"));

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print(F("updatePrintProgress() Unexpected response: "));
    Serial.println(status);
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders))
  {
    Serial.println(F("updatePrintProgress() Invalid response"));
    return;
  }

  // This is probably not needed for most, but I had issues
  // with the Tindie api where sometimes there were random
  // characters coming back before the body of the response.
  // This will cause no hard to leave it in
  // peek() will look at the character, but not take it off the queue
  while (client.available() && client.peek() != '{')
  {
    char c = 0;
    client.readBytes(&c, 1);
    Serial.print(c);
    Serial.println("BAD");
  }

  DynamicJsonDocument doc(5000);
  DeserializationError error = deserializeJson(doc, client);

  if (!error) {
    float completion = doc["progress"]["completion"];
    const char* state = doc["state"];

    if(strcmp(state, "Operational") == 0) {
      jobState = JOB_OPERATIONAL;
    } else if(strcmp(state, "Printing") == 0) {
      if(jobState == JOB_OPERATIONAL) {
        clearLastPrint = true;
      } else {
        clearLastPrint = false;
      }
      jobState = JOB_PRINTING;
    } else {
      // catch all case in case we get "canceled" or something else
      jobState = JOB_OPERATIONAL;
    }
    
    Serial.print("updatePrintProgress() job state = ");
    Serial.println(state);
    Serial.print("updatePrintProgress() setting global job state to ");
    Serial.println(jobState);
    
    Serial.print("updatePrintProgress() completion_percentage: ");
    Serial.println(completion);

    print_progress = int((NUM_LEDS * int(completion)) / 100);
    if(print_progress < 1) {
      print_progress = 1;
    }

    Serial.print("updatePrintProgress() - updating progress LEDS to ");
    Serial.println(print_progress);
  } else {
    Serial.print(F("updatePrintProgress() deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
}

void loop() {
  getLatestPrinterStatus();
  
  // check WIFI connection status
  if(!isWifiConnected()) {
    Serial.println("loop() - disconnected from WiFi");
    setupWifi();
  }
  
  updatePrintProgress();

  delay(LOOP_DELAY);
}

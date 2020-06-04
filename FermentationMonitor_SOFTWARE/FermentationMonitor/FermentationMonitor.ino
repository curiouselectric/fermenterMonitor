
/*
  Fermentation Friend

  This sketch demonstrates the capabilities of the pubsub library in combination
  with the ESP8266 board/library.

  It connects to WiFi (or creates a hotspot to enter in the Wifi credentials if needed)
  It montiors 1 wire temperature sensors (as many as are chained to the single pin)

  It displays either: all temperatures, or a large version of each temperature attached
  Scroll through this using the encoder.

  It publishes the first temperature value via MQTT to the broker in the config.

  It will reconnect to the server if the connection is lost using a blocking
  reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
  achieve the same result without blocking the main loop.

  To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

  You need to include the following libraries, via the library manager in Arduino
    WiFiManager (v 0.15.0) by tzapu
    Adafruit_NeoPixel by Adafruit
    Adafruit_MQTT_Library by Adafruit
    U8g2 by Oliver
    Button2 by Lennart Hennings
    OneWire by Jim Studt + lots of others
    DallasTemperature by Miles Burton + others
*/

// Config.h includes all the hardware and defines for the board
#include "Config.h"

// ************** WIFI Libraries ************************************

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// ********** For the RGB LEDS (Neopixels) *************************
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel pixels(RGBLED_COUNT, RGBLED_DATA_PIN , RGBLED_TYPE);

// ********** For the I2C OLED Screen and Graphics *****************
#include <U8g2lib.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

//U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);   // All Boards without Reset of the Display
CBOLED_CLASS u8g2(U8G2_R0, CBOLED_SCK_PIN, CBOLED_SDA_PIN, U8X8_PIN_NONE);

// *********** For the BUTTONS AND ENCODERS *************************
#include "Button2.h"; //  https://github.com/LennartHennigs/Button2
#include "ESPRotary.h";

// ****** For the TEMPERATURE SENSING *******************************
// Include these for the temperature sensors
#include <OneWire.h>
#include <DallasTemperature.h>
// Data wire is plugged into port GPIO5 on the Arduino
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// ********* For the ADAFRUIT IO MQTT CONNECTION **********************/
// WiFiFlientSecure for SSL/TLS support
WiFiClientSecure client;
// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
// io.adafruit.com SHA1 fingerprint
static const char *fingerprint PROGMEM = "77 00 54 2D DA E7 D8 03 27 31 23 99 EB 27 DB CB A5 4C 57 18";

/************* Feeds for Adafruit IO *********************************/
// Setup a feed called 'test' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish fermenterTemp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/fermenterTemp");
Adafruit_MQTT_Publish airTemp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/airTemp");

//// ********* For other MQTT Connection - Read Cheers Lights *********
//#include <PubSubClient.h>
//PubSubClient client(client);  // Old library (but nice!)

/******************** Sketch Code ************************************/

byte displayMode = 100;    // Holds the page to display - Start in Start-up screen
byte maxDisplayMode = 5; // Roll around the modes

bool updateMQTTflag = false;

bool wificonnect = false; // Flags for the display
bool mqttconnect = false; // Flags for the display

float temp1 = 0;  // Holds the fermentation temperature
float temp2 = 0;  // Holds the air temperature

float tempHigh = 20.0;
float tempLow = 10.0;

float temp1Max = -999.9;    // Start low
float temp1Min = 999.9;     // Start High
float temp1Ave = 0.0;
int   temp1AveCounter = 0;  // Holds the number of samples read to create the average value

float temp2Max = -999.9;    // Start low
float temp2Min = 999.9;     // Start High
float temp2Ave = 0.0;
int   temp2AveCounter = 0;  // Holds the number of samples read to create the average value

long int dataCounterTime = DATA_AVERAGE * 1000;   // Holds mS until next data upload
long int graphCounterTime = GRAPH_AVERAGE * 1000;  // Holds ms until next graph point
long int displayCounterTime = DISPLAY_UPDATE;   // How often to check data and update display (mS)

// Graph drawing inputs

#define sizeOfBuffer 120
float temp1Buffer[sizeOfBuffer];  // Sets up a buffer of floats for displaying data
float temp2Buffer[sizeOfBuffer];  // Sets up a buffer of floats for displaying data

int startx = 2;          // For drawing the graphs (bottom left corner)
int starty = 46;          // For drawing the graphs (bottom left corner)
float graphHeight = 30.0;     // Height of graph (its 120 pixels wide, 64 pixels high)
float graphHeightMaxValue = 30.0;  // Maximum of the int values to be displayed
float graphHeightMinValue = 10.0;    // Minimum of the int values to be displayed
float graphCalculatedY;   // for doing calculations

ESPRotary r = ESPRotary(ROT_A_PIN2, ROT_B_PIN2, 4);
Button2 b = Button2(ROT_PUSH_PIN2);

//int32_t RotaryValue = 0;
//int32_t oldValue = 0;

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(10);

  randomSeed(analogRead(A0));

  pixels.begin();
  pixels.clear(); // Set all pixel colors to 'off'
  pixels.show();   // Send the updated pixel colors to the hardware.

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.sendBuffer();

  // Show display
  updateScreen(displayMode, wificonnect, mqttconnect);

  // Sort out Wifi
  setup_wifi();

  updateScreen(displayMode, wificonnect, mqttconnect); // Show if connected or not

  // check the fingerprint of io.adafruit.com's SSL cert
  client.setFingerprint(fingerprint);

  // Start up the dallas 1 wire temp library
  sensors.begin();

  // Init the RotaryInput object
  r.setChangedHandler(rotate);
  b.setLongClickHandler(click);

  // Initialise the temperature Buffers
  for (int i = 0; i < sizeOfBuffer; i++)
  {
    temp1Buffer[i] = 0;
    temp2Buffer[i] = 0;
  }
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(180);
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();  // Reset of cannot connect - got to AP mode
    delay(5000);
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  wificonnect = true;
  displayMode = EEPROM.read(10);  // After showing - update to mode
}

uint32_t x = 0;
float value = 0.0;  // Holds the temperature value to display and to send to AdafruitIO

void loop() {
  // This is the main loop
  // Check the input encoder and button:
  r.loop();
  b.loop();

  // ****** Update the display *********************
  if (millis() >= (displayCounterTime + (DISPLAY_UPDATE)))
  {
    displayCounterTime = millis(); // Store new value for next time
    // Read the temperature(s):
    // ***** NOTE: MUST HAVE 1k PULLUP TO 5V Vin ****
    // call sensors.requestTemperatures() to issue a global temperature
    // request to all devices on the bus
    if (DEBUG_TEMP == 1)
    {
      Serial.print("Requesting temperatures...");
    }
    sensors.requestTemperatures(); // Send the command to get temperatures
    if (DEBUG_TEMP == 1)
    {
      Serial.println("DONE");
      // After we got the temperatures, we can print them here.
      // We use the function ByIndex, and as an example get the temperature from the first sensor only.
      Serial.print("Temp 1: ");
      Serial.print(sensors.getTempCByIndex(0));
      Serial.print(" Temp 2: ");
      Serial.println(sensors.getTempCByIndex(1));
    }
    temp1 = sensors.getTempCByIndex(1);
    temp2 = sensors.getTempCByIndex(0);

    // Here we do some statistics on the data:
    if (temp1 > temp1Max && temp1 < 120.0)
    {
      temp1Max = temp1;
    }
    if (temp1 < temp1Min && temp1 > -100.0)
    {
      temp1Min = temp1;
    }
    temp1Ave += temp1;
    temp1AveCounter++;

    if (temp2 > temp2Max && temp2 < 120.0)
    {
      temp2Max = temp2;
    }
    if (temp2 < temp2Min && temp2 > -100.0)
    {
      temp2Min = temp2;
    }
    temp2Ave += temp2;
    temp2AveCounter++;

    // ****** DISPLAY THE VALUE **********************
    // Here want to update the display with the value:
    updateScreen(displayMode, wificonnect, mqttconnect);
  }

  // ******* graph buffer update *************
  // Only do this when over the graph update time
  if (millis() >= (graphCounterTime + (GRAPH_AVERAGE * 1000)))
  {

    graphCounterTime = millis(); // Store new value for next time
    // Sort out the display buffer
    for (int z = (sizeOfBuffer - 2); z >= 0; z--)
    {
      // Shift all the values along
      temp1Buffer[z + 1] = temp1Buffer[z];
      temp2Buffer[z + 1] = temp2Buffer[z];
    }
    // Add the new average values
    temp1Buffer[0] = temp1Ave / temp1AveCounter;
    temp2Buffer[0] = temp2Ave / temp2AveCounter;

    if (DEBUG_GRAPH == 1)
    {
      Serial.println("Graph Updated!");
      Serial.print("Temp1 Ave: ");
      Serial.print(temp1Buffer[0]);
      Serial.print(" Temp2 Ave: ");
      Serial.println(temp2Buffer[0]);
    }
    // reset the averages
    temp1Ave = 0;
    temp1AveCounter = 0;
    temp2Ave = 0;
    temp2AveCounter = 0;

  }
  // ********** End graph update **************

  // Send the MQTT data
  if (millis() >= (dataCounterTime + ((DATA_AVERAGE * 1000) / 2)))
  {
    dataCounterTime = millis(); // Store new value for next time
    // Only do this when time is over the next update
    // *********** MQTT SEND VALUE(S) ***************
    // Want this to be non-blocking send data every alternate 2 seconds (200*10ms)

    // Ensure the connection to the MQTT server is alive (this will make the first
    // connection and automatically reconnect when disconnected).  See the MQTT_connect
    // function definition further below.
    MQTT_connect();

    updateMQTTflag = !updateMQTTflag;

    if (updateMQTTflag == false)
    {
      if (DEBUG_MQTT == 1)
      {
        Serial.print(F("\nSending val "));
        Serial.print(temp2);
        Serial.print(F(" to airTemp feed..."));
      }
      if (! airTemp.publish(temp2)) {
        if (DEBUG_MQTT == 1)
        {
          Serial.println(F("Failed"));
        }
      } else {
        if (DEBUG_MQTT == 1)
        {
          Serial.println(F("OK!"));
        }
      }
    }
    else if (updateMQTTflag == true)
    {
      if (DEBUG_MQTT == 1)
      {
        Serial.print(F("\nSending val "));
        Serial.print(temp1);
        Serial.print(F(" to fermenterTemp feed..."));
      }
      if (! fermenterTemp.publish(temp1)) {
        if (DEBUG_MQTT == 1)
        {
          Serial.println(F("Failed"));
        }
      } else {
        if (DEBUG_MQTT == 1)
        {
          Serial.println(F("OK!"));
        }
      }
    }
  }
  // ******* END MQTT UPLOAD ********
  delay(10);  //Short delay to slow it all down!
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    mqttconnect = true;
    return;
  }
  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    mqttconnect = false;
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
    if (retries == 0) {
      // basically die and wait for WDT to reset me
      while (1);
    }
  }
  Serial.println("MQTT Connected!");
  mqttconnect = true;
}

void updateScreen(int _mode, bool _wificonnect, bool _mqttconnect)
{
  // This routine draws the basic display with all features
  // Displays if WiFi connected
  // Displays if MQTT connected
  // Dsiplays mode (for debugging)
  u8g2.clearBuffer();
  u8g2.setFont(CBOLED_MESSAGE_FONT_8PT);  // choose a suitable font

  char _buffer[6];  // A holder for data to string conversions

  // Here we decide what to show depending upon the displayMode
  switch (displayMode) {
    case 1:
      // Show both temperatures as small values
      u8g2.setCursor(0, 10);
      u8g2.print(F("Fermenters Friend!"));
      u8g2.setCursor(0, 25);
      u8g2.print(F("T ferm:"));
      u8g2.setCursor(64, 25);
      u8g2.print(checkTempRange(temp1, -100.0, 120.0));
      u8g2.setCursor(0, 40);
      u8g2.print(F("T air:"));
      u8g2.setCursor(64, 40);
      u8g2.print(checkTempRange(temp2, -100.0, 120.0));
      pixels.clear(); // Set all pixel colors to 'off'
      pixels.show();   // Send the updated pixel colors to the hardware.
      break;
    case 2:
      u8g2.setCursor(0, 10);
      u8g2.print(F("MIN:"));
      // String checkTempRange(float _temp, float _min, float _max)
      u8g2.setCursor(32, 10);
      u8g2.print(checkTempRange(temp1Min, -100.0, 120.0));
      u8g2.setCursor(64, 10);
      u8g2.print(F("MAX:"));
      u8g2.setCursor(96, 10);
      u8g2.print(checkTempRange(temp1Max, -100.0, 120.0));
      // Draw the main temp in BIG in the middle
      u8g2.setCursor(0, 36);
      u8g2.print(F("T ferm:"));
      // Want to adjust the font here:
      u8g2.setFont(CBOLED_MESSAGE_FONT_24PT);  // choose a suitable font
      u8g2.setCursor(38, 50);
      u8g2.print(checkTempRange(temp1, -100.0, 120.0));
      u8g2.setFont(CBOLED_MESSAGE_FONT_8PT);  // Font back to normal!
      checkLEDs(temp1, tempHigh, tempLow);
      break;

    case 3:
      // Show temp 1 as a bar chart over time
      u8g2.setCursor(0, 10);
      u8g2.print(F("T ferm:"));
      u8g2.setCursor(64, 10);
      u8g2.print(checkTempRange(temp1, -100.0, 120.0));
      // Want to draw 120 lines from
      // startx, staryy graphHeight, graphHeightMaxValue
      for (int n = 0; n < sizeOfBuffer; n++)
      {
        if (temp1Buffer[n] <= graphHeightMaxValue && temp1Buffer[n] >= graphHeightMinValue)
        {
          graphCalculatedY = (starty - (((temp1Buffer[n] - graphHeightMinValue) / (graphHeightMaxValue - graphHeightMinValue)) * graphHeight ));
        }
        else if (temp1Buffer[n] > graphHeightMaxValue)
        {
          graphCalculatedY = (starty - (graphHeight));
        }
        else if (temp1Buffer[n] < graphHeightMinValue)
        {
          graphCalculatedY = starty;
        }
        u8g2.drawLine(startx + n, starty, startx + n, (int)graphCalculatedY);
      }
      checkLEDs(temp1, tempHigh, tempLow);
      break;
    case 4:
      u8g2.setCursor(0, 10);
      u8g2.print(F("MIN:"));
      u8g2.setCursor(32, 10);
      u8g2.print(checkTempRange(temp2Min, -100.0, 120.0));
      u8g2.setCursor(64, 10);
      u8g2.print(F("MAX:"));
      u8g2.setCursor(96, 10);
      u8g2.print(checkTempRange(temp2Max, -100.0, 120.0));
      // Draw the main temp in BIG in the middle
      u8g2.setCursor(0, 36);
      u8g2.print(F("T air:"));
      // Want to adjust the font here:
      u8g2.setFont(CBOLED_MESSAGE_FONT_24PT);  // choose a suitable font
      u8g2.setCursor(38, 50);
      u8g2.print(checkTempRange(temp2, -100.0, 120.0));
      u8g2.setFont(CBOLED_MESSAGE_FONT_8PT);  // Font back to normal!
      checkLEDs(temp2, tempHigh, tempLow);
      break;

    case 5:
      // Show temp 1 as a bar chart over time
      u8g2.setCursor(0, 10);
      u8g2.print(F("T air:"));
      u8g2.setCursor(64, 10);
      u8g2.print(checkTempRange(temp2, -100.0, 120.0));
      // Want to draw 120 lines from
      // startx, staryy graphHeight, graphHeightMaxValue
      for (int n = 0; n < sizeOfBuffer; n++)
      {
        if (temp2Buffer[n] <= graphHeightMaxValue && temp2Buffer[n] >= graphHeightMinValue)
        {
          graphCalculatedY = (starty - (((temp2Buffer[n] - graphHeightMinValue) / (graphHeightMaxValue - graphHeightMinValue)) * graphHeight ));
        }
        else if (temp2Buffer[n] > graphHeightMaxValue)
        {
          graphCalculatedY = starty - (graphHeight);
        }
        else if (temp2Buffer[n] < graphHeightMinValue)
        {
          graphCalculatedY = starty;
        }
        u8g2.drawLine(startx + n, starty, startx + n, (int)graphCalculatedY);
      }
      checkLEDs(temp2, tempHigh, tempLow);
      break;

    case 99:
      // This is the case when the EEPROM has been saved
      // Displays this screen for a bit!
      u8g2.setCursor(0, 10);
      u8g2.print(F("MODE SAVED!"));
      displayMode = EEPROM.read(10);  // After showing - update to mode
      break;
    case 100:
      // This is the case when the EEPROM has been saved
      // Displays this screen for a bit!
      u8g2.setCursor(0, 10);
      u8g2.print(F("START UP"));
      break;
  }

  // This section draws the bit at the botoom (always there)
  if (_wificonnect == true)
  {
    u8g2.setCursor(0, 64);
    u8g2.print(F("Wifi OK"));
  }
  else
  {
    u8g2.setCursor(0, 64);
    u8g2.print(F("       "));
  }
  if (_mqttconnect == true)
  {
    u8g2.setCursor(64, 64);
    u8g2.print(F("MQTT OK"));
  }
  else
  {
    u8g2.setCursor(64, 64);
    u8g2.print(F("       "));
  }

  //  // Decomment this to show the displayMode for debugging
  //  dtostrf(_mode, 7, 0, _buffer);
  //  u8g2.setCursor(100, 64);
  //  u8g2.print(_buffer);

  u8g2.sendBuffer();  // Write all the display data to the screen
}

void checkLEDs(float _temp, float _tempHigh, float _tempLow)
{
  // This lights the LEDs in different colours depending upon high/low setpoints
  if (_temp > _tempHigh)
  {
    // Show red as too warm
    for (int i = 0; i < 5; i++)
    {
      pixels.setPixelColor(i, 255, 0, 0);
    }
  }
  else   if (_temp < _tempLow)
  {
    // Show Blue as too cold
    for (int i = 0; i < 5; i++)
    {
      pixels.setPixelColor(i, 0, 0, 255);
    }
  }
  else
  {
    //  Show green as Just right
    for (int i = 0; i < 5; i++)
    {
      pixels.setPixelColor(i, 0, 255, 0);
    }
  }
  pixels.setBrightness(RGBLED_BRIGHTNESS);
  pixels.show();
}



// ****** ENCODER & BUTTON FUNCTIONS *****************
// on change of encoder
void rotate(ESPRotary & r) {
  //RotaryValue = r.getPosition();
  //  Serial.println(r.directionToString(r.getDirection()));
  //  Serial.println(r.getPosition());
  if (DEBUG_ENCODER == 1)
  {
    Serial.println(r.directionToString(r.getDirection()));
    Serial.print("Position: ");
    Serial.println(r.getPosition());
  }

  if (r.directionToString(r.getDirection()) == "RIGHT")
  {
    displayMode++;
    if (displayMode > maxDisplayMode)
    {
      displayMode = 1;
    }
  }
  else if (r.directionToString(r.getDirection()) == "LEFT")
  {
    displayMode--;
    if (displayMode <= 0)
    {
      displayMode = maxDisplayMode;
    }
  }
  updateScreen(displayMode, wificonnect, mqttconnect);
}

// ****** Check if temperature value is within bounds *******

String checkTempRange(float _temp, float _min, float _max)
{
  char _localBuffer[4];
  String _string;
  // Check temp and return a string of value if OK
  // Return string of "NA" if not
  if (_temp > _max || _temp < _min)
  {
    _string = "NA  ";
    // Sensor TOO high - probably not connected
    _string.toCharArray(_localBuffer, 4);
  }
  else
  {
    dtostrf(_temp, 4, 1, _localBuffer);
  }
  return (_localBuffer);
}

// long click of button
void click(Button2 & btn) {
  //Store starting displayMode to EEPROM with long press
  EEPROM.write(10, displayMode);  // this writes a good value to it
  EEPROM.commit();
  Serial.println("MODE Saved");
  displayMode = 99; // Show the 'saved' screen
  updateScreen(displayMode, wificonnect, mqttconnect);
}

#include <WiFi.h>                 //WiFi
#include <AsyncTCP.h>             //WebServer
#include <ESPAsyncWebServer.h>    //WebServer
#include <Wire.h>                 //enable I2C.
#include <LiquidCrystal_I2C.h>    //LCD
#include <Button.h>               //Button
#include <FastLED.h>              //NeoPixel LED Lights

//-----------[ DBUG ]---------
const int DBUG = 0;          // Set this to 0 for no serial output for debugging, 1 for moderate debugging, 2 for FULL debugging to see serail output in the Arduino GUI.
//----------------------------

//-----------[ CUSTOMIZE ]----------------
bool CODE_FOR_DOx_DEVICE          = true;
bool CODE_FOR_TEMPSALINITY_DEVICE = false;
const char* ssid                  = "HBOI";
const char* password              = "1c7146fb30";
float GV_DOx_TOOHIGHVALUE         = 10.00;
float GV_DOx_TOOLOWVALUE          = 6.00;
int LED_DEFAULT_BRIGHTNESS        = 5;
#define LED_DATA_PIN              15
#define LED_NUMBER_OF_LEDS        7
#define DOxSensorAddress          97                 // default I2C ID number for EZO D.O. Circuit.
#define SalinitySensorAddress     100
#define TemperatureSensorAddress  102
//----------------------------------------

//----------[ FUNCTION SWITCH VARS ]------
bool GV_WEB_REQUEST_IN_PROGRESS = false;
bool GV_READ_REQUEST_IN_PROGRESS = false;
bool GV_THIS_IS_A_SERIAL_COMMAND = false;
bool GV_QUERY_DO_NAME_ON_NEXT_COMMAND = true;   // Loop tests first to see if we need to query the DO for it's name.  Set to true to do this first thing
int GV_FIND=0;
bool GV_BOOTING_UP = true;
//----------------------------------------

//LED LIGHTS
CRGB leds[LED_NUMBER_OF_LEDS];  // Define the array of leds
//----------

//BUTTON
Button button1(2);                                      // Connect your button between pin 2 and GND
int     GV_LCD_MAIN_TEXT_INDEX=0;                         // Text to show in top left of LCD (DO sensor name, IP Address, Program version, FireBeatle version.  Depending on the button.
String  GV_LCD_MAIN_TEXT[4] = {"xxx.xxx.xxx.xxx \0",    //Text to cycle through when button is pressed.
                                "JoeSmoe        \0",
                                "V12.28         \0",
                                "Program V1.00  \0"};
//-----
 
// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);               // set the LCD address to 0x27 for a 16 chars and 2 line
const bool ClearLCD = true, NoClearLCD = false, PrintSerial = true, NoSerial = false;  // Used for LCD_DISPLAY

//I2C

String GV_SENSOR_DATA = "";            // Holds latest reading from D.O. Circuit.
String GV_SENSOR_RESPONSE = "NONE";  // Holds latest response from D.O. Circuit.
String GV_WEB_RESPONSE_TEXT ="";

//---
unsigned long SensorAutoReadingMillis = millis();  // Stores milliseconds since last D.O. reading.
unsigned long HeartBeatMillis = millis();
unsigned long WebServerRestartMillis = millis();
char HeartBeat = ' ';
//----------------------------------------

//---------------[ PRE-SETUP]-------------
AsyncWebServer server(80);              // Setup Web Server Port.
const char* PARAM_MESSAGE = "command";     // HTTP_GET parameter to look for.
//----------------------------------------

//=======================================[ SETUP ]================================================
void setup() {
  Serial.begin(115200);
  //I2C
  Wire.begin();                //enable I2C port.
  //---

  //LED LIGHTS
  FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, LED_NUMBER_OF_LEDS);
  FastLED.setBrightness(LED_DEFAULT_BRIGHTNESS);
  LED_Clear();
  //----------
  
  //BUTTON
  button1.begin();
  //------
  
  // LCD
  lcd.begin(); //initialize the lcd

  //-----------------------------[ WEB SERVER ]-----------------------------------------------
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    LCD_DISPLAY("Wifi..", 0, 0, ClearLCD, PrintSerial);
    LCD_DISPLAY(ssid, 0, 1, NoClearLCD, PrintSerial);
  }
  lcd.clear();
  
  IPAddress IP=WiFi.localIP();
  GV_LCD_MAIN_TEXT[0]=String(IP[0]) + '.' + String(IP[1]) + '.' + String(IP[2]) + '.' + String(IP[3]);

  byte mac[6];
  WiFi.macAddress(mac);
  GV_LCD_MAIN_TEXT[2]=String(mac[5],HEX) + String(mac[4],HEX) + String(mac[3],HEX) + String(mac[2],HEX) + String(mac[1],HEX) + String(mac[0],HEX);
  
  //SendCommandToSensorAndSetReturnGVVariables("name,?");          // get the name of the DO sensor to display on the LCD
  
  //-----------------[ read web page]-------------------------------
  server.on("/read", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", GV_WEB_RESPONSE_TEXT);
    });
  //-----------------[ send web page]-------------------------------
  // Send a GET request to <IP>/get?message=<message>
  server.on("/send", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String message;
    int w=0;
    GV_WEB_REQUEST_IN_PROGRESS = true;               // set this to tell the auto DOx read loop we want to talk to the DOx sensor from the micro PC's web page.  I assume this is an Interupt.
    if (request->hasParam(PARAM_MESSAGE)) {
      message = request->getParam(PARAM_MESSAGE)->value();
    } else {
      message = "No message sent";
    }
    while (GV_READ_REQUEST_IN_PROGRESS){             // wait until the auto DOx read ends so we can have a turn sending this request sent from the micro PC's web page to the DOx sensor
      w++;
      if (w < 2000) break;
      }
      
    SendCommandToSensorAndSetReturnGVVariables(message);

    request->send(200, "text/plain", GV_WEB_RESPONSE_TEXT);

    GV_WEB_REQUEST_IN_PROGRESS = false;
    });

  //---------------[ FIND web page ON ]----------------------------
  server.on("/findon", HTTP_GET, [](AsyncWebServerRequest * request) {
    GV_FIND=1;
    request->send(200, "text/plain", "Find On");
    });
 
 //---------------[ FIND web page OFF ]-----------------------------
  server.on("/findoff", HTTP_GET, [](AsyncWebServerRequest * request) {
    GV_FIND=0;
    request->send(200, "text/plain", "Find Off");
    }); 

  server.begin();
 
  //----------------------------------------------------------------------------------------
}
//=======================================[ SETUP: END ]=============================================


//-------------------------------[ Variables used in LOOP ]------------
char computerdata[20];           //we make a 20 byte character array to hold incoming data from a pc/mac/other.
byte received_from_computer = 0; //we need to know how many characters have been received.
char incoming_data[20];                //we make a 20 byte character array to hold incoming data from the D.O. circuit.
byte in_char = 0;                //used as a 1 byte buffer to store inbound bytes from the D.O. Circuit.
int time_ = 600;                 //used to change the delay needed depending on the command sent to the EZO Class D.O. Circuit.
String DO;                        //char pointer used in string parsing.
int i = 0;
char R = 'r';
char *ReadDOx = &R;

//=======================================[ LOOP ]===================================================
void loop() {
  
  if (GV_FIND){                                                         // GV_FIND is set to true if web page ipaddress/findon is called.  Nothing is supposed to function while this is happening.
    FastLED.setBrightness(100);
    while( GV_FIND == 1 ){                                              // GV_FIND is set to false if web page ipaddress/findoff is called.
      if (CODE_FOR_DOx_DEVICE){
        LED_FIND_LIGHT_FOR_DOx();
      }
    }
    LED_Clear();
    FastLED.setBrightness(LED_DEFAULT_BRIGHTNESS);
  }

  
  if (GV_QUERY_DO_NAME_ON_NEXT_COMMAND){              // query DOx name if this is set to true, then display it on the LCD screen.  This is set to run on boot-up and when DOx name was changed. 
    SendCommandToSensorAndSetReturnGVVariables("name,?\0");
    Serial.println(GV_SENSOR_DATA);
    GV_SENSOR_DATA.remove(0,6);
    GV_LCD_MAIN_TEXT[1]=GV_SENSOR_DATA;
    GV_LCD_MAIN_TEXT_INDEX=1;
    LCD_DISPLAY(GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX],0,0,ClearLCD,PrintSerial);
    GV_QUERY_DO_NAME_ON_NEXT_COMMAND = false;
  }

  if (Serial.available() > 0) {                                           //if data is holding in the serial buffer
    received_from_computer = Serial.readBytesUntil(13, computerdata, 20); //we read the data sent from the serial monitor(pc/mac/other) until we see a <CR>. We also count how many characters have been received.
    computerdata[received_from_computer] = 0;                             //stop the buffer from transmitting leftovers or garbage.
    computerdata[0] = tolower(computerdata[0]);                           //we make sure the first char in the string is lower case.
    if (computerdata[0] == 'c' || computerdata[0] == 'r')time_ = 600;     //if a command has been sent to calibrate or take a reading we wait 600ms so that the circuit has time to take the reading.
    else time_ = 300;                                                     //if not 300ms will do

    GV_THIS_IS_A_SERIAL_COMMAND=true;                                     //set gloabal indicator that the next command to the DO cercuit is from the Serial Monitor.
    String str_serial_command=computerdata;                               //convert char array to String type for the function that follows
    SendCommandToSensorAndSetReturnGVVariables(str_serial_command);                    //send the command received by the serial monitoring device.
  }
  
  if (((millis() - SensorAutoReadingMillis) > 2000) && !GV_WEB_REQUEST_IN_PROGRESS){
    GV_READ_REQUEST_IN_PROGRESS=true;
    SendCommandToSensorAndSetReturnGVVariables(ReadDOx);
    LCD_DISPLAY(GV_SENSOR_DATA, 0, 1, NoClearLCD, PrintSerial);
    LCD_DISPLAY("  ", GV_SENSOR_DATA.length(), 1, NoClearLCD, NoSerial);
    SensorAutoReadingMillis = millis();
    GV_READ_REQUEST_IN_PROGRESS=false;
  }
  
  if ((millis() - WebServerRestartMillis) > 500000) {
    if(DBUG) Serial.println("WebServer re-begin");
    WebServerRestartMillis = millis();
    server.begin();
  }
  
  LCDshowHeartBeat();

  BUTTON_WasItPressed_ChangeLCD();

  GV_BOOTING_UP=false;
  
}
//=======================================[ LOOP: END ]================================================


//===============================[ FUNCTIONS ]=========================
void LED_Center_Blue(bool ON){
  LED_Clear();
  if (ON) {
    leds[0] = CRGB::Blue;
    FastLED.show();    
  }else{
    LED_Clear();  
  }
  
}

void LED_Clear(){
  for (int led=0; led!=7; led++){
    leds[led] = CRGB::Black;
  }
  FastLED.show();
}

void LED_FIND_LIGHT_FOR_DOx(){
  fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(255,  255,  255));   //white
  delay(40);
  FastLED.show();
  fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(0,  0,  0));   //white
  delay(40);
  FastLED.show();  
}

void LED_sensor_return_code_Fade(int code){
  LED_Clear();
  FastLED.setBrightness(30);
  if (CODE_FOR_DOx_DEVICE){
      switch (code){
        case 1:   fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(0,    0,    255)); break;   //blue
        case 2:   fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(255,  0,      0)); break;   //red
        case 254: fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(255,  255,    0)); break;   //yellow
        case 255: fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(255,  255,  255)); break;   //white
      }
      for (int i=0; i<100; i++){
        delay(15);
        for (int j=0; j<7; j++){
          leds[j].fadeToBlackBy ( i );   
        }
        FastLED.show();
      }    
  }

}

String AddCarrageReturnIfNeeded(String str){   
    long int len = str.length();
    int i=0;
    bool HasCR = false;
    
    for (i=0; i < len; i++) if (str[i]=='\r') HasCR=true;   
    if (!HasCR) { str[i++]='\r'; str[i++]='\0'; }   
    if(DBUG==2){
        for (i=0; i < (str[i] != '\0'); i++){
            Serial.print(str[i]); Serial.print(":"); Serial.print(i); Serial.print(":"); Serial.print(int(str[i]));
        }
    }
    return str;    
}

void LCD_DISPLAY(String Text, int row, int col, bool xClearLCD, bool xprintSerial) { // 12 Millis
  if (xClearLCD) lcd.clear();
  lcd.setCursor(row, col); // set the cursor to column 15, line 1
  lcd.print(Text);
  if (xprintSerial) if(DBUG) Serial.println("LCD:" + Text);
}

void BUTTON_WasItPressed_ChangeLCD(){
  String LCDTEXT;
  if (button1.pressed() && !GV_BOOTING_UP){
    if(DBUG) Serial.println("Button 1 pressed");
    switch (GV_LCD_MAIN_TEXT_INDEX){
      case 0: GV_LCD_MAIN_TEXT_INDEX=1;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
      case 1: GV_LCD_MAIN_TEXT_INDEX=2;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
      case 2: GV_LCD_MAIN_TEXT_INDEX=3;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
      case 3: GV_LCD_MAIN_TEXT_INDEX=0;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
    }
    if(DBUG) Serial.println(LCDTEXT);
    LCD_DISPLAY(LCDTEXT, 0, 0, ClearLCD, PrintSerial);                            
   }  
}

void LCDshowHeartBeat() {
  if ((millis() - HeartBeatMillis) > 1000) {
    if ( (GV_SENSOR_DATA.toFloat() >= GV_DOx_TOOHIGHVALUE) || (GV_SENSOR_DATA.toFloat() <= GV_DOx_TOOLOWVALUE) ){
        for(int v=0; v<15; v++){
          fill_solid(leds, LED_NUMBER_OF_LEDS, CRGB(255,  255,    0));          //yellow
          FastLED.show();
          delay(30);
          LED_Clear();
          delay(30);
        }
    }else{
        if (HeartBeat != ' ') {
          HeartBeat = ' ';
          LED_Center_Blue(false);
        }
        else {
          HeartBeat = '*';
          LED_Center_Blue(true);
        }
        if(DBUG==2) Serial.println(HeartBeat);
        LCD_DISPLAY(&HeartBeat, 15, 0, NoClearLCD, NoSerial);
    } 
    HeartBeatMillis = millis();
  }
}

void SendCommandToSensorAndSetReturnGVVariables(String command) {
  char Ccommand[20];
  byte code = 0;                   //used to hold the I2C response code.
  command[0] = tolower(command[0]);
  command=AddCarrageReturnIfNeeded(command);
  command.toCharArray(Ccommand,20);
  Wire.beginTransmission(DOxSensorAddress);                              //call the circuit by its ID number.
  Wire.write(Ccommand);                                            //transmit the command that was sent through the serial port.
  Wire.endTransmission();                                          //end the I2C data transmission.
 
  if (DBUG){ Serial.print("DOc command:("); Serial.print(Ccommand); Serial.print(")"); }

  if (Ccommand[0] == 'c' || Ccommand[0] == 'r' || Ccommand[0] == 'n')time_ = 600;     //if a command has been sent to calibrate or take a reading we wait 600ms so that the circuit has time to take the reading.
  else time_ = 300;                                             //if not 300ms will do
    
  //if (strcmp(computerdata, "sleep") != 0) {  //if the command that has been sent is NOT the sleep command, wait the correct amount of time and request data.
  //if it is the sleep command, we do nothing. Issuing a sleep command and then requesting data will wake the D.O. circuit.

  delay(time_);                     //wait the correct amount of time for the circuit to complete its instruction.

  Wire.requestFrom(DOxSensorAddress, 20, 1); //call the circuit and request 20 bytes (this may be more than we need)
  code = Wire.read();               //the first byte is the response code, we read this separately.

  switch (code) {                   //switch case based on what the response code is.
    case 1:                         //decimal 1.
      if(DBUG==2) Serial.println("Success");    //means the command was successful.
      GV_SENSOR_RESPONSE="Success";
      break;                        //exits the switch case.
    case 2:                         //decimal 2.
      if(DBUG==2) Serial.println("Failed");     //means the command has failed.
      GV_SENSOR_RESPONSE="Failed \0";
      GV_SENSOR_DATA="";
      break;                        //exits the switch case.
    case 254:                      //decimal 254.
      if(DBUG==2) Serial.println("Pending");   //means the command has not yet been finished calculating.
      GV_SENSOR_RESPONSE="Pending\0";
      GV_SENSOR_DATA="";
      break;                       //exits the switch case.
    case 255:                      //decimal 255.
      if(DBUG==2) Serial.println("No Data");   //means there is no further data to send.
      GV_SENSOR_RESPONSE="No Data\0";
      GV_SENSOR_DATA="";
      break;                       //exits the switch case.
  }
   
  while (Wire.available()) {       //are there bytes to receive.
    in_char = Wire.read();         //receive a byte.
    incoming_data[i] = in_char;          //load this byte into our array.
    if(DBUG==2) Serial.println(incoming_data[i]);
    i += 1;                        //incur the counter for the array element.
    if (in_char == 0) {            //if we see that we have been sent a null command.
      i = 0;                       //reset the counter i to 0.
      Wire.endTransmission();      //end the I2C data transmission.
      break;                       //exit the while loop.
    }
  }
  if (Ccommand[0] != 'r') LED_sensor_return_code_Fade(code);
  
  GV_SENSOR_DATA = incoming_data;         

  GV_WEB_RESPONSE_TEXT=GV_SENSOR_DATA + "," + GV_SENSOR_RESPONSE;

  if (Ccommand[0] == 'n' && Ccommand[5] != '?'){              // someone sent the name,TheName to the DO cercuit (not just querying the name,?).
    GV_QUERY_DO_NAME_ON_NEXT_COMMAND = true;    
  }
  
  if ( DBUG==2 || (DBUG==1 && GV_THIS_IS_A_SERIAL_COMMAND) ){
      Serial.println("GV_SENSOR_DATA:" + GV_SENSOR_DATA);
      Serial.println("GV_SENSOR_RESPONSE:" + GV_SENSOR_RESPONSE);
      Serial.println("GV_WEB_RESPONSE_TEXT" + GV_WEB_RESPONSE_TEXT);
  }
  if (GV_THIS_IS_A_SERIAL_COMMAND) GV_THIS_IS_A_SERIAL_COMMAND=false;                                     //set gloabal indicator that command from the Serial Monitor is done.

  
}

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//     _____       __         ____
//    / ___/____  / /______  / __ \____ ___  __
//    \__ \/ __ \/ //_/ __ \/ / / / __ `/ / / /
//   ___/ / /_/ / ,< / /_/ / /_/ / /_/ / /_/ /
//  /____/\____/_/|_|\____/_____/\__,_/\__, /
//                     vonkonow.com   /____/
//
// SokoDay is an addictive handheld gaming device providing daily Sokoban challenges.
// The device downloads three new puzzles every day from a server.
// One short, one medium and one long (referring to number of moves to solve the level).
// You can build your own device with a 3D printer, soldering iron and a handful of components.
// Everything is open source and hacking is encouraged!
//
// *** SokoDay is using the ESP8266 microchip on a LOLIN D1 Mini module ***
//
// Latest version, details and build instructions is found on https://vonkonow.com/wordpress/sokoday/
// Check out the forum where you can get support and contribute: https://vonkonow.com/wordpress/community/
// SokoDay uses the excellent neopixelbus library from makuna: https://github.com/Makuna/NeoPixelBus
//
// This code is open source under MIT License.
// (attribution is optional, but always appreciated - Johan von Konow ;)
//
// History: 2022-01-04
// * updated to MIT license
// * cleaned code for GitHub
// Update 230524:
// * the webserver hosting the levels got a firewall rule blocking requests from arduino http client.
// * replaced httpclient with manual request, updated path and level extraction.


const char ap1id[] = "wifi name";             // <== replace with network1 SSID (name of wifi network)
const char ap1pw[] = "wifi password";         // <== replace with network1 pw (wifi password)
const char ap2id[] = "";                      // network2 SSID (name of second wifi network)
const char ap2pw[] = "";                      // network2 pw (wifi password for second network)
const char otaid[] = "SokoDay";               // This id the name the device will have on the WiFi (used by OTA)
const char otapw[] = "rXlghRrT409IBltvZBRC";  // <== OTA password (change to random pw to secure device)
const int brightness = 64;


#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>  // used by OTA
#include <WiFiUdp.h>      // used by OTA
#include <ArduinoOTA.h>   // used by OTA
#include <EEPROM.h>       // used to store difficult setting in emulated eeprom
ESP8266WiFiMulti WiFiMulti;

#include <NeoPixelBrightnessBus.h>
const uint16_t PixelCount = 64;
NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount);

const RgbColor playerColor(HtmlColor(0x550000));
const RgbColor wallColor(HtmlColor(0x550055));
const RgbColor goalColor(HtmlColor(0x005500));
const RgbColor boxColor(HtmlColor(0x0000ff));
const RgbColor floorColor(HtmlColor(0x000000));
const RgbColor playerGoalColor(HtmlColor(0x777700));
const RgbColor boxGoalColor(HtmlColor(0x00aaaa));

const char wall = '#';
const char player = '@';
const char goal = '.';
const char box = '$';
const char empty = ' ';
const char playerGoal = '+';
const char boxGoal = '*';


const char fontA[13][3] = {
  { 0x0E, 0x11, 0x0E },  // 0
  { 0x12, 0x1F, 0x10 },  // 1
  { 0x1A, 0x15, 0x12 },  // 2
  { 0x11, 0x15, 0x0E },  // 3
  { 0x07, 0x04, 0x1F },  // 4
  { 0x17, 0x15, 0x1D },  // 5
  { 0x1E, 0x15, 0x1D },  // 6
  { 0x19, 0x05, 0x03 },  // 7
  { 0x1F, 0x15, 0x1F },  // 8
  { 0x17, 0x15, 0x1F },  // 9
  { 0x1F, 0x15, 0x11 },  // E
  { 0x1F, 0x02, 0x1F },  // M
  { 0x1F, 0x04, 0x1F }   // H
};

const char spriteA[3][8] = {
  { 0x00, 0x24, 0x2A, 0x2A, 0x2A, 0x12, 0x00, 0x00 },  // S
  //0x00, 0x00, 0x3E, 0x2A, 0x2A, 0x22, 0x00, 0x00}, // E
  { 0x00, 0x3E, 0x04, 0x08, 0x04, 0x3E, 0x00, 0x00 },  // M
  { 0x00, 0x00, 0x3E, 0x20, 0x20, 0x20, 0x00, 0x00 }   //L
  //0x00, 0x00, 0x3E, 0x08, 0x08, 0x3E, 0x00, 0x00} // H
};

// hw3.0 rect2
const int keyRight = 12;  // 12,D6 right
const int keyUp = 14;     // 14,D5 up
const int keyDown = 5;    // 05,D1 down
const int keyLeft = 13;   // 13,D7 left

const uint8_t xMin = 0, yMin = 0;
const uint8_t xMax = 7, yMax = 7;
const int pulseSpeed = 38;
char level[yMax + 1][xMax + 1];         // game level (2D array)
int px, py;                             // player coordinates
int pMove, pPush;                       // player move and push counters
char pDir;                              // move direction (' '= no move)
int loopC = 0, loopC2 = 0, loopC3 = 0;  // animation counter
int difficulty;                         // difficulty level (loaded from EEPROM) 0= easy, 1= medium, 2= hard

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("WS2812 Sokuban 8266 serial debug initiated.");

  // Init LED matrix
  strip.Begin();
  strip.SetBrightness(brightness);
  strip.SetPixelColor(0, HtmlColor(0x005500));  //led0 power on
  strip.Show();

  // Check if difficulty setting is changed (pressing up (easy), right (medium) or down (hard) during power up)
  pinMode(keyRight, INPUT_PULLUP);  // set pin to input
  pinMode(keyUp, INPUT_PULLUP);     // set pin to input
  pinMode(keyDown, INPUT_PULLUP);   // set pin to input
  pinMode(keyLeft, INPUT_PULLUP);   // set pin to input
  EEPROM.begin(512);                // use 512 bytes of ESP8266 flash (for "EEPROM" emulation) we only need two bits ;)
  difficulty = EEPROM.read(0);
  if (difficulty > 2) {
    difficulty = 0;
  }
  if (!digitalRead(keyUp)) {  // test if easy is selected during power up
    difficulty = 0;
  }
  if (!digitalRead(keyRight)) {  // test if medium is selected during power up
    difficulty = 1;
  }
  if (!digitalRead(keyDown)) {  // test if hard is selected during power up
    difficulty = 2;
  }
  showDifficulty();
  strip.Show();

  if (!digitalRead(keyLeft)) {
    delay(2000);
    if (!digitalRead(keyLeft)) {
      EEPROM.write(0, difficulty);
      EEPROM.commit();
    }
  }


  // Connect to WiFi network
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ap1id, ap1pw);
  WiFiMulti.addAP(ap2id, ap2pw);
  //WiFiMulti.addAP(ap3id, ap3pw);
  while (WiFiMulti.run() != WL_CONNECTED) {
    rnd2d(10);
    showDifficulty();
    strip.Show();
  }
  for (int i = 0; i < PixelCount; i++) {  // clear background
    strip.SetPixelColor(i, HtmlColor(0x000000));
  }

  initOTA();  // setup OTA (wireless FW updates)

  // download level
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    strip.SetPixelColor(1, HtmlColor(0x005500));  //led1 wifi connected
    strip.Show();
    Serial.print("difficulty");
    Serial.println(difficulty);

    WiFiClient client;
    Serial.print("manual request...\n");
    if (client.connect("www.vonkonow.com", 80)) {
      strip.SetPixelColor(2, HtmlColor(0x005500));  //led2 http connect
      strip.Show();
      Serial.print("[HTTP] GET...\n");

      String request = "GET /sokoplay/";
      if (difficulty == 0) {
        request += "short.php";
      } else if (difficulty == 1) {
        request += "medium.php";
      } else if (difficulty == 2) {
        request += "long.php";
      }
      request += "?i=1&m=";
      request += WiFi.macAddress();
      request += " HTTP/1.1\r\nHost: vonkonow.com\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n";
      Serial.println(request);
      Serial.println("sending request...");
      client.print(request);

      int i = 0;
      while (!client.available() && i < 1000) {
        delay(1);
        i++;
      }
      strip.SetPixelColor(3, HtmlColor(0x005500));  //led3 http get
      strip.Show();

      if (client.available() > 0) {
        String response = client.readString();
        //Serial.println(response);
        if (response.substring(0, 15).equals("HTTP/1.1 200 OK")) {
          Serial.println("response ok!");

          strip.SetPixelColor(4, HtmlColor(0x005500));  //led4 file downloaded
          strip.Show();

          importLevel(response);

          Serial.println("Imported level:");
          dumpLevel();

        } else {
          strip.SetPixelColor(4, HtmlColor(0xff0000));  //led4 file error
          strip.Show();
        }
      } else {
        strip.SetPixelColor(3, HtmlColor(0xff0000));  //led3 http get error
        strip.Show();
        Serial.printf("[HTTP] GET... failed, error: %s\n");
      }
      client.stop();
    } else {
      strip.SetPixelColor(2, HtmlColor(0xff0000));  //led2 http connect error
      strip.Show();
      Serial.printf("[HTTP} Unable to connect\n");
    }
  } else {
    strip.SetPixelColor(1, HtmlColor(0xff0000));  //led1 wifi not connected
    strip.Show();
  }

  //get player coordinates
  for (int y = 0; y <= yMax; y++) {
    for (int x = 0; x <= xMax; x++) {
      if (level[y][x] == player || level[y][x] == playerGoal) {
        px = x;
        py = y;
      }
    }
  }
}


void showDifficulty() {
  if (difficulty == 2) {
    sprite(difficulty, 0, 0, RgbColor(255, 0, 0));
  } else if (difficulty == 1) {
    sprite(difficulty, 0, 0, RgbColor(255, 255, 0));
  } else {
    sprite(difficulty, 0, 0, RgbColor(0, 255, 0));
  }
}

void loop() {
  movePlayer(checkKeys());
  showLevel();
  testWin();
  ArduinoOTA.handle();
}

void movePlayer(char mov) {
  if (mov == ' ') {  // player didnt move...
    return;
  }
  int dx = 0, dy = 0;
  if (mov == 'U') {
    dy = -1;
  }
  if (mov == 'D') {
    dy = 1;
  }
  if (mov == 'R') {
    dx = 1;
  }
  if (mov == 'L') {
    dx = -1;
  }
  int tx = px + dx;
  int ty = py + dy;
  // test if outside level
  if (tx > xMax || tx < xMin || ty > yMax || ty < yMin) {
    return;
  }
  // test if wall
  if (level[ty][tx] == wall) {
    return;
  }
  // test if box and move box if possible
  if (level[ty][tx] == box || level[ty][tx] == boxGoal) {
    if (tx + dx > xMax || tx + dx < xMin || ty + dy > yMax || ty + dy < yMin) {
      return;
    }
    if (level[ty + dy][tx + dx] == wall || level[ty + dy][tx + dx] == box || level[ty + dy][tx + dx] == boxGoal) {
      return;
    }
    if (level[ty + dy][tx + dx] == empty) {
      level[ty + dy][tx + dx] = box;
      pPush++;
      //      pMove--;
    } else if (level[ty + dy][tx + dx] == goal) {
      level[ty + dy][tx + dx] = boxGoal;
      pPush++;
      //      pMove--;
    }
    if (level[ty][tx] == box) {
      level[ty][tx] = empty;
    } else if (level[ty][tx] == boxGoal) {
      level[ty][tx] = goal;
    }
  }
  // move player
  if (level[ty][tx] == empty) {
    level[ty][tx] = player;
    pMove++;
  } else if (level[ty][tx] == goal) {
    level[ty][tx] = playerGoal;
    pMove++;
  }
  if (level[py][px] == player) {
    level[py][px] = empty;
  } else if (level[py][px] == playerGoal) {
    level[py][px] = goal;
  }
  px = tx;
  py = ty;
}


void testWin() {
  int nrGoals = 0;
  for (int y = 0; y <= yMax; y++) {
    for (int x = 0; x <= xMax; x++) {
      if (level[y][x] == goal || level[y][x] == playerGoal) {
        return;
      }
      if (level[y][x] == boxGoal) {
        nrGoals++;
      }
    }
  }
  if (nrGoals > 0) {
    // we have a winner - upload result

    WiFiClient client;
    if (client.connect("www.vonkonow.com", 80)) {
      String request = "GET /sokoplay/";
      if (difficulty == 0) {
        request += "short.php";
      } else if (difficulty == 1) {
        request += "medium.php";
      } else if (difficulty == 2) {
        request += "long.php";
      }
      request += "?g=1&m=";
      request += WiFi.macAddress();
      request += "&pm=";
      request += pMove;
      request += "&pp=";
      request += pPush;
      request += " HTTP/1.1\r\nHost: vonkonow.com\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n";
      Serial.println(request);
      Serial.println("sending request...");
      client.print(request);
    }
    // display winner animation
    if (pMove > 99) {
      pMove = 99;
    }
    while (1) {
      font(pMove / 10, 2, 4, RgbColor(32, 0, 32));
      font(pMove % 10, 2, 0, RgbColor(32, 0, 32));
      plasma(2);
      strip.Show();
    }
  }
}

void showLevel() {
  loopC++;
  if (loopC > pulseSpeed)
    loopC = 0;
  int dimIntensity = (cos(((float)loopC / pulseSpeed) * 3.14 * 2) + 1) * 120 + 15;

  for (int y = 0; y <= yMax; y++) {
    for (int x = 0; x <= xMax; x++) {
      setif(x, y, '#', wallColor);
      setif(x, y, '@', playerColor.Dim(dimIntensity));
      setif(x, y, '.', goalColor);
      setif(x, y, '$', boxColor);
      setif(x, y, ' ', floorColor);
      setif(x, y, '+', playerGoalColor.Dim(dimIntensity));
      setif(x, y, '*', boxGoalColor);
    }
  }
  strip.Show();
  delay(20);
}


void setif(int x, int y, char test, RgbColor wallColor) {
  if (level[y][x] == test) {
    strip.SetPixelColor((xMax - x) * (yMax + 1) + y, wallColor);
  }
}

void font(uint8_t value, uint8_t xo, uint8_t yo, RgbColor color) {  // note that y and x are swapped since LED matrix is rotated
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 5; x++) {
      if (bitRead(fontA[value][2 - y], x)) {
        strip.SetPixelColor((x + xo) + (y + yo) * (yMax + 1), color);
      }
    }
  }
}

void sprite(uint8_t value, uint8_t xo, uint8_t yo, RgbColor color) {  // note that y and x are swapped since LED matrix is rotated
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (bitRead(spriteA[value][7 - y], x)) {
        strip.SetPixelColor((x + xo) + (y + yo) * (yMax + 1), color);
      }
    }
  }
}

char checkKeys() {
  if (!digitalRead(keyRight)) {
    while (!digitalRead(keyRight)) {
      delay(20);
    }
    Serial.println("Right");
    return 'R';
  }
  if (!digitalRead(keyLeft)) {
    while (!digitalRead(keyLeft)) {
      delay(20);
    }
    Serial.println("Left");
    return 'L';
  }
  if (!digitalRead(keyDown)) {
    while (!digitalRead(keyDown)) {
      delay(20);
    }
    Serial.println("Down");
    return 'D';
  }
  if (!digitalRead(keyUp)) {
    while (!digitalRead(keyUp)) {
      delay(20);
    }
    Serial.println("Up");
    return 'U';
  }
  return ' ';
}

void importLevel(String levelImport) {
  int x, y;
  int strPt = levelImport.indexOf("content=") + 9;
  Serial.println(strPt);
  for (y = 0; y <= yMax; y++) {
    for (x = 0; x <= xMax; x++) {
      while (!validChar(levelImport[strPt])) {
        strPt++;
        if (strPt > levelImport.length()) {
          fillXY(x, y);
          return;
        }
      }
      if (levelImport[strPt] == ',') {
        /*
          Serial.print("L");
          Serial.print(levelImport.length());
          Serial.print(", S");
          Serial.print(strPt);
          Serial.print(", x");
          Serial.print(x);
          Serial.print(", y");
          Serial.print(y);
          Serial.print(": ");
          Serial.print(levelImport[strPt]);
          Serial.println(" ");
        */
        while (x <= xMax) {
          level[y][x] = empty;
          x++;
        }
      } else if (levelImport[strPt] == ';') {
        fillXY(x, y);
        return;
      } else {
        level[y][x] = levelImport[strPt];
        strPt++;
      }
      if (strPt > levelImport.length()) {
        fillXY(x, y);
        return;
      }
    }
    while (levelImport[strPt] != ',') {
      strPt++;
      if (strPt > levelImport.length() || levelImport[strPt] == ';') {
        fillXY(x, y);
        return;
      }
    }
    strPt++;
  }
}


void dumpLevel() {  // used to show (debug) imported level during init
  Serial.println("Dumping level: ");
  for (int y = 0; y <= yMax; y++) {
    for (int x = 0; x <= xMax; x++) {
      Serial.print(level[y][x]);
    }
    Serial.println(' ');
  }
}


void fillXY(int x, int y) {  // used by importLevel
  while (y <= yMax) {
    while (x <= xMax) {
      level[y][x] = empty;
      x++;
    }
    y++;
  }
}


bool validChar(char in) {  // used by importLevel
  if (in == wall) {
    return true;
  } else if (in == player) {
    return true;
  } else if (in == goal) {
    return true;
  } else if (in == box) {
    return true;
  } else if (in == empty) {
    return true;
  } else if (in == playerGoal) {
    return true;
  } else if (in == boxGoal) {
    return true;
  } else if (in == ',') {
    return true;
  } else if (in == ';') {
    return true;
  }
  return false;
}

void rnd2d(uint8_t pause) {
  for (int i = 0; i <= PixelCount; i++) {
    RgbColor pixelColor = strip.GetPixelColor(i);
    pixelColor.Darken(1);
    strip.SetPixelColor(i, pixelColor);
  }
  int pos = random(8 * 8 * 2);
  if (pos < 8 * 8) {
    strip.SetPixelColor(pos, RgbColor(128, 128, 255));
  }
  delay(pause);
}

void plasma(uint8_t pause) {
  float pulse = cos(((float)loopC2 / 3600) * 3.14 * 2);
  float pulse2 = sin(((float)loopC3 / 1000) * 3.14 * 2);
  for (int y = 0; y <= yMax; y++) {
    for (int x = 0; x <= xMax; x++) {
      int color = 0;
      color += y * 85 * pulse;
      color += x * 50 * pulse2;
      float cx = -4 + pulse2 * 4 + x;
      float cy = -4 + pulse * 4 + y;
      color += sqrt(cx * cx + cy * cy) * 50;
      color = (int)color / (3 + pulse2);
      color = color + loopC;
      strip.SetPixelColor(y * (yMax + 1) + x, WheelH(color));
    }
  }
  loopC = nextCount(loopC, 768);
  loopC2 = nextCount(loopC2, 3600);
  loopC3 = nextCount(loopC3, 1000);
  delay(pause);
}

int nextCount(int count, int limit) {
  count++;
  if (count >= limit) {
    count = 0;
  }
  return count;
}

void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  for (j = 0; j < 256 * 5; j++) {  // 5 cycles of all colors on wheel
    for (i = 0; i < PixelCount; i++) {
      strip.SetPixelColor(i, Wheel(((i * 256 / PixelCount) + j) & 255));
    }
    strip.Show();
    delay(wait);
  }
}


// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
RgbColor Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return RgbColor((255 - WheelPos * 3) / 2, 0, (WheelPos * 3) / 2);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return RgbColor(0, (WheelPos * 3) / 2, (255 - WheelPos * 3) / 2);
  }
  WheelPos -= 170;
  return RgbColor((WheelPos * 3) / 2, (255 - WheelPos * 3) / 2, 0);
}

// Input a value 0 to 767 to get a color value of all colors
RgbColor WheelH(int WheelPos) {
  while (WheelPos < 0) {
    WheelPos += 767;
  }
  while (WheelPos > 767) {
    WheelPos -= 767;
  }
  WheelPos = 767 - WheelPos;
  if (WheelPos < 256) {
    return RgbColor((255 - WheelPos), 0, WheelPos);
  }
  if (WheelPos < 512) {
    WheelPos -= 256;
    return RgbColor(0, WheelPos, (255 - WheelPos));
  }
  WheelPos -= 512;
  return RgbColor(WheelPos, (255 - WheelPos), 0);
}


void initOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(otaid);
  ArduinoOTA.setPassword(otapw);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: % u % % \r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[ % u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

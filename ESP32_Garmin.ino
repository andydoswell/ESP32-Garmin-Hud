

/*ESP32-Garmin HUD+ controller.

   Based on the work of gabonator, Frank Huebenthal & skyforce Shen

   ESP32 to handle parsing of GPS data, GPS receiver connected to ESP32's Serial 2, and transmit to Garmin HUD+ display over the ESP32's inbuilt bluetooth
   radio, to implement a simple HUD display. It's stand-alone, so doesn't do any of the flash google maps stuff, like Skyforce's Android app.
*/

#include "BluetoothSerial.h"
#include <TinyGPSPlus.h>

BluetoothSerial SerialBT;

String MACadd = "AA:BB:CC:11:22:33";
uint8_t address[6]  = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
//uint8_t address[6]  = {0x00, 0x1D, 0xA5, 0x02, 0xC3, 0x22};
String name = "GARMIN HUD+";
char *pin = "1234"; //<- standard pin would be provided by default
bool connected;
bool lastStraight = true;
typedef enum {
  None = 0,
  Metres = 1,
  Kilometres = 3,
  Miles = 5
} eUnits;
typedef enum {
  Down = 0x01,
  SharpRight = 0x02,
  Right = 0x04,
  EasyRight = 0x08,
  Straight = 0x10,
  EasyLeft = 0x20,
  Left = 0x40,
  SharpLeft = 0x80,
  LeftDown = 0x81,
  RightDown = 0x82,
  AsDirection = 0x00
} eOutAngle;

typedef enum {
  Off = 0x00,
  Lane = 0x01,
  LongerLane = 0x02,
  LeftRoundabout = 0x04,
  RightRoundabout = 0x08,
  ArrowOnly = 0x80
} eOutType;

// The serial connection to the GPS device
HardwareSerial SerialGPS(2);
TinyGPSPlus gps;

void setup() {
  Serial.begin(115200);
  setupGPS();
  SerialBT.begin("ESP32GarminCTRL", true);
  Serial.println("The device started in master mode, make sure Garmin HUD device is on!");
  SerialBT.register_callback(callback); // used to detect dropped BT connection
  connected = SerialBT.connect(name); // connect
  if (connected) {
    Serial.println("Connected Succesfully!");
    delay (2500); // wait for OK to finish animation
    SetAutoBrightness();// set HUD brihtness
    ClearTime(); // clear display
    ClearDistance();
    SetDirection(Straight, ArrowOnly, AsDirection);
  } else {
    while (!SerialBT.connected(10000)) {
      Serial.println("Failed to connect. ");
      ESP.restart(); // I feel dirty
    }
  }
}

void loop()
{
  while (SerialGPS.available() > 0) // get and decode the gps data
    gps.encode(SerialGPS.read());

  if  (gps.time.isUpdated()) // set the clock, adjusting for BST
  {
    int Hour = (gps.time.hour());
    if (isBST()) {
      Hour ++;
      if (Hour == 24) {
        Hour = 0;
      }
    }
    SetTime(Hour, gps.time.minute(), 0, 0, 1, 0);
  }

  else if (gps.speed.isUpdated())
  {
    if (gps.speed.mph() > 0) {
      SetDistance (gps.speed.mph(), Miles, 0, 0);
    }
    else {
      SetDistance (0, Miles, 0, 0);
    }
  }
  else if (gps.course.isUpdated())
  {
    displayCompass(gps.course.deg());
  }
  else if (gps.satellites.isUpdated())
  {
    if (gps.satellites.value() >= 4) {
      SetSpeedWarning(gps.satellites.value(), 0, 0, 0);
    }
    else {
      SetSpeedWarning(gps.satellites.value(), 1, 0, 0);
    }
  }
}

void SetTime(int nH, int nM, bool bFlag, bool bTraffic, bool bColon, bool bH)
{
  char arr[] = {(char) 0x05,
                bTraffic ? (char) 0xff : (char) 0x00,
                Digit(nH / 10), Digit(nH), bColon ? (char) 0xff : (char) 0x00,
                Digit(nM / 10), Digit(nM), bH ? (char) 0xff : (char) 0x00,
                bFlag ? (char) 0xff : (char) 0x00
               };

  SendHud(arr, sizeof(arr));
}

void SendHud(char* pBuf, int nLen)
{
  char sendBuf[255], len = 0;
  unsigned int nCrc = 0xeb + nLen + nLen;

  sendBuf[len++] = 0x10;
  sendBuf[len++] = 0x7b;
  sendBuf[len++] = nLen + 6;
  if (nLen == 0xa)
    sendBuf[len++] = 0x10;
  sendBuf[len++] = nLen;
  sendBuf[len++] = 0x00;
  sendBuf[len++] = 0x00;
  sendBuf[len++] = 0x00;
  sendBuf[len++] = 0x55;
  sendBuf[len++] = 0x15;
  for (int i = 0; i < nLen; i++)
  {
    nCrc += pBuf[i];
    sendBuf[len++] = pBuf[i];
    if (pBuf[i] == 0x10) //Escape LF
      sendBuf[len++] = 0x10;
  }

  sendBuf[len++] = (-(int)nCrc) & 0xff;
  sendBuf[len++] = 0x10;
  sendBuf[len++] = 0x03;

  SendPacket(sendBuf, len);
}

void SetSpeedWarning(int nLimit, bool bSpeeding, bool bIcon, bool bSlash)
{
  char arr[] = {
    (char)0x06,
    (char)0,
    (char)0,
    (char)0,
    (char)(bSlash ? 0xff : 0x00),
    (char) ((nLimit / 100) % 10),
    Digit(nLimit / 10),
    Digit(nLimit),
    (char)(bSpeeding ? 0xff : 0x00),
    (char)(bIcon ? 0xff : 0x00)
  };
  SendHud(arr, sizeof(arr));
}

char Digit(int n)
{
  n = n % 10;
  if (n == 0)
    return (char)10;
  return (char)n;
}

int SendPacket(const char* pBuf, int nLen)
{
  //Serial.println (nLen);
  size_t size = 0;
  // size = Serial.write(pBuf, nLen);

  for (int i = 0; i <= nLen; i++) {
    //  Serial.print (i);
    //  Serial.print (" ");
    SerialBT.print(pBuf[i]);
  }
  return size;
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) //Filthy dirty way to reset the connection in the event of the BT connection dropping.
{
  if (event == ESP_SPP_CLOSE_EVT ) {
    Serial.println("Client disconnected");
    ESP.restart();
  }
}

void SetAutoBrightness()
{
  char command_auto_brightness[] = { 0x10, 0x7B, 0x0E, 0x08, 0x00, 0x00, 0x00, 0x56, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x10, 0x03 };
  SendPacket(command_auto_brightness, sizeof(command_auto_brightness));
}

void SetSpeed(int nSpeed, bool bSpeeding, bool bIcon, bool bSlash)
{
  char arr[] = {
    (char)0x06,

    (char)((nSpeed / 100) % 10),
    Digit(nSpeed / 10),
    Digit(nSpeed),
    (char)(bSlash ? 0xff : 0x00),
    (char) 0,
    (char) 0,
    (char) 0,

    (char)(bSpeeding ? 0xff : 0x00),
    (char)(bIcon ? 0xff : 0x00)
  };
  SendHud(arr, sizeof(arr));
}


void SetDirection(eOutAngle nDir, eOutType nType, eOutAngle nRoundaboutOut )
{
  char arr[] = {
    (char) 0x01,
    (char) ((nDir == LeftDown) ? 0x10 : ((nDir == RightDown) ? 0x20 : nType)),
    (char)((nType == RightRoundabout || nType == LeftRoundabout) ? ((nRoundaboutOut == AsDirection) ? nDir : nRoundaboutOut) : 0x00),
    (char)((nDir == LeftDown || nDir == RightDown) ? 0x00 : nDir)
  };
  SendHud(arr, sizeof(arr));
}

void SetLanes(char nArrow, char nOutline)
{
  char arr[] = { (char)0x02, nOutline, nArrow };
  SendHud(arr, sizeof(arr));
}

void ClearTime()
{
  char arr[] = {( char) 0x05,
                0x00 ,
                0, 0 ,
                0x00 ,
                0, 0 ,
                0x00 ,
                // 0x00
               };
  SendHud(arr, sizeof(arr));
}

void SetDistance(int nDist, eUnits unit , bool bDecimal, bool bLeadingZero)
{
  char arr[] = {
    ( char) 0x03,
    Digit(nDist / 1000),
    Digit(nDist / 100),
    Digit(nDist / 10),
    ( char)(bDecimal ? 0xff : 0x00),
    Digit(nDist),
    ( char)unit
  };

  if (!bLeadingZero)
  {
    if (arr[1] == 0xa)
    {
      arr[1] = 0;
      if (arr[2] == 0xa)
      {
        arr[2] = 0;
        if (arr[3] == 0xa)
        {
          arr[3] = 0;
        }
      }
    }
  }
  SendHud(arr, sizeof(arr));
}

void ClearDistance() {
  char arr[] = {
    ( char) 0x03,
    0,
    0,
    0,
    0x00,
    0,
    ( char)None
  };
  SendHud(arr, sizeof(arr));
}

void setupGPS ()
{
  SerialGPS.begin(9600, SERIAL_8N1, 16, 17);
  // send update u-blox rate to 200mS
  SerialGPS.write(0xB5);
  SerialGPS.write(0x62);
  SerialGPS.write(0x06);
  SerialGPS.write(0x08);
  SerialGPS.write(0x06);
  SerialGPS.write(0x00);
  SerialGPS.write(0xC8);
  SerialGPS.write(0x00);
  SerialGPS.write(0x01);
  SerialGPS.write(0x00);
  SerialGPS.write(0x01);
  SerialGPS.write(0x00);
  SerialGPS.write(0xDE);
  SerialGPS.write(0x6A);
  SerialGPS.write(0xB5);
  SerialGPS.write(0x62);
  SerialGPS.write(0x06);
  SerialGPS.write(0x08);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x0E);
  SerialGPS.write(0x30);
  delay (100);
  SerialGPS.flush();
  // set 57,600 baud on u-blox
  SerialGPS.write(0xB5);
  SerialGPS.write(0x62);
  SerialGPS.write(0x06);
  SerialGPS.write(0x00);
  SerialGPS.write(0x14);
  SerialGPS.write(0x00);
  SerialGPS.write(0x01);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0xD0);
  SerialGPS.write(0x08);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0xE1);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x07);
  SerialGPS.write(0x00);
  SerialGPS.write(0x02);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0x00);
  SerialGPS.write(0xDD);
  SerialGPS.write(0xC3);
  SerialGPS.write(0xB5);
  SerialGPS.write(0x62);
  SerialGPS.write(0x06);
  SerialGPS.write(0x00);
  SerialGPS.write(0x01);
  SerialGPS.write(0x00);
  SerialGPS.write(0x01);
  SerialGPS.write(0x08);
  SerialGPS.write(0x22);
  delay (100);
  SerialGPS.end();// stop SerialGPS coms at 9,600 baud
  delay (100);
  SerialGPS.begin (57600); // start SerialGPS coms at 57,600 baud.
}

void displayCompass (float compass)
{
  if (gps.speed.mph() > 3) { // only update compass if we're moving (stops it flickering about if we're stationary)
    if ((compass > 337.5) || (compass <= 22.5)) {
      SetDirection(Straight, ArrowOnly, AsDirection);
    }

    if ((compass > 22.5) && (compass <= 67.5)) {
      SetDirection(EasyRight, ArrowOnly, AsDirection);
      SetDirection(EasyRight, ArrowOnly, AsDirection);
    }
    if ((compass > 67.5) && (compass <= 112.5)) {
      SetDirection(Right, ArrowOnly, AsDirection);
      SetDirection(Right, ArrowOnly, AsDirection);
    }
    if ((compass > 112.5) && (compass <= 157.5)) {
      SetDirection(SharpRight, ArrowOnly, AsDirection);
      SetDirection(SharpRight, ArrowOnly, AsDirection);
    }
    if ((compass > 157.5) && (compass <= 202.5)) {
      SetDirection(Down, ArrowOnly, AsDirection);
      SetDirection(Down, ArrowOnly, AsDirection);
    }
    if ((compass > 202.5) && (compass <= 247.5)) {
      SetDirection(SharpLeft, ArrowOnly, AsDirection);
      SetDirection(SharpLeft, ArrowOnly, AsDirection);
    }
    if ((compass > 247.5) && (compass <= 292.5)) {
      SetDirection(Left, ArrowOnly, AsDirection);
      SetDirection(Left, ArrowOnly, AsDirection);
    }
    if ((compass > 292.5) && (compass <= 337.4)) {
      SetDirection(EasyLeft, ArrowOnly, AsDirection);
      SetDirection(EasyLeft, ArrowOnly, AsDirection);
    }
  }
}

boolean isBST() // this bit of code blatantly plagarised from http://my-small-projects.blogspot.com/2015/05/arduino-checking-for-british-summer-time.html
{
  int imonth = gps.date.month();
  int iday = gps.date.day();
  int hr = gps.time.hour();
  //January, february, and november are out.
  if (imonth < 3 || imonth > 10) {
    return false;
  }
  //April to September are in
  if (imonth > 3 && imonth < 10) {
    return true;
  }
  // find last sun in mar and oct - quickest way I've found to do it
  // last sunday of march
  int lastMarSunday =  (31 - (5 * gps.date.year() / 4 + 4) % 7);
  //last sunday of october
  int lastOctSunday = (31 - (5 * gps.date.year() / 4 + 1) % 7);
  //In march, we are BST if is the last sunday in the month
  if (imonth == 3) {
    if ( iday > lastMarSunday)
      return true;
    if ( iday < lastMarSunday)
      return false;
    if (hr < 1)
      return false;
    return true;
  }
  //In October we must be before the last sunday to be bst.
  //That means the previous sunday must be before the 1st.
  if (imonth == 10) {
    if ( iday < lastOctSunday)
      return true;
    if ( iday > lastOctSunday)
      return false;
    if (hr >= 1)
      return false;
    return true;
  }
}

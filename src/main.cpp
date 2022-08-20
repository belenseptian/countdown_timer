/* Initialization */
//Libraries
#include <Arduino.h>
#include <DMD32.h>
#include "fonts/SystemFont5x7.h"
#include "fonts/Arial_Black_16_ISO_8859_1.h"
#include <EEPROM.h>
#include <Ds1302.h>
#include "BluetoothSerial.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

//Constants
#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
#define TIMER_MS 2000
#define FREQ  5000
#define CHANNEL 0
#define RESOLUTION 8
#define BLUETOOTH_NAME "p10Controller"

//Global Vars
String run_text, onair_text, running_text;
unsigned int txt_brightness, txt_speed, timer_, end_time, end_time_second, start_time = 0;
bool is_timer = false, is_finished = false;
char onair_txt[24], clock_buffer[13];

//Classic Bluetooth
BluetoothSerial SerialBT;

// DS1302 RTC
Ds1302 rtc(25, 32, 33); //RST, CLK, DATA

//DMD
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

//Timer Setup
hw_timer_t * disp_timer = NULL;

/* Methods */
String EEPROM_get(char add)
{
  char data[100]; //Max 100 Bytes
  int len = 0;
  unsigned char k;
  k = EEPROM.read(add);
  while (k != '\0' && len < 500) //Read until null character
  {
    k = EEPROM.read(add + len);
    data[len] = k;
    len++;
  }
  data[len] = '\0';
  return String(data);
}

String secondsToHMS(const uint32_t seconds)
{
    uint32_t t = seconds;
    uint16_t h,m,s;
    String h_,m_,s_;

    s = t % 60;
    if(s < 10)
    {
      s_ = "0" + String(s);
    }
    else
    {
      s_ = String(s);
    }

    t = (t - s)/60;
    m = t % 60;
    if(m < 10)
    {
      m_ = "0" + String(m);
    }
    else
    {
      m_ = String(m);
    }

    t = (t - m)/60;
    h = t;
    if(h < 10)
    {
      h_ = "0" + String(h);
    }
    else
    {
      h_ = String(h);
    }

    return h_ + m_ + s_;
}

uint8_t parseDigits(char* str, uint8_t count)
{
    uint8_t val = 0;
    while(count-- > 0) val = (val * 10) + (*str++ - '0');
    return val;
}

void EEPROM_put(char add, String data)
{
  int _size = data.length();
  int i;
  for (i = 0; i < _size; i++)
  {
    EEPROM.write(add + i, data[i]);
  }
  EEPROM.write(add + _size, '\0'); //Add termination null character for String Data
  EEPROM.commit();
}

void startCountdown()
{
  end_time = start_time;
  start_time = end_time-(millis()-timer_);
  end_time_second = start_time/1000;

}

void setBrightness()
{
  //control the brightness of LED
  ledcSetup(CHANNEL, FREQ, RESOLUTION); 
  //attach the channel to the GPIO to be controlled
  ledcAttachPin(PIN_DMD_nOE, CHANNEL);
  ledcWrite(CHANNEL, txt_brightness);
}

void initVars()
{
  run_text = EEPROM_get(10) + " ";
  txt_speed = EEPROM_get(100).toInt();
  txt_brightness = EEPROM_get(110).toInt();
  onair_text = EEPROM_get(120);
  onair_text.toCharArray(onair_txt, 24);
}

void getBluetoothData()
{
  while (SerialBT.available() > 0) {
    char c = SerialBT.read();
    running_text += c;
  }
  running_text.trim();
  if(running_text.length() > 0 && running_text.indexOf("1234_") >= 0) { 
    if(running_text.indexOf("STXT_") >= 0)
    {
      SerialBT.println("OK.Running text has been modified");
      EEPROM_put(10, running_text.substring(10,running_text.length()));
      is_finished = true;
    }
    else if(running_text.indexOf("STMR_") >= 0)
    {
      SerialBT.println("OK.Timer has been started");
      start_time = running_text.substring(10,running_text.length()).toInt()*1000;
      is_finished = true;
      is_timer = true;
    }
    else if(running_text.indexOf("SCLK_") >= 0)
    {
      SerialBT.println("OK.Timer has been adjusted");
      running_text.substring(10,running_text.length()).toCharArray(clock_buffer, 13);
      Ds1302::DateTime dt;
      dt.year = parseDigits(clock_buffer, 2);
      dt.month = parseDigits(clock_buffer + 2, 2);
      dt.day = parseDigits(clock_buffer + 4, 2);
      dt.dow = parseDigits(clock_buffer + 6, 1);
      dt.hour = parseDigits(clock_buffer + 7, 2);
      dt.minute = parseDigits(clock_buffer + 9, 2);
      dt.second = parseDigits(clock_buffer + 11, 2);

      // set the date and time
      rtc.setDateTime(&dt);
      is_finished = true;
    }
    else if(running_text.indexOf("SSPD_") >= 0)
    {
      SerialBT.println("OK.Speed has been changed");
      EEPROM_put(100, running_text.substring(10,running_text.length()));
      is_finished = true;
    }
    else if(running_text.indexOf("SBRT_") >= 0)
    {
      SerialBT.println("OK.Brightness has been adjusted");
      EEPROM_put(110, running_text.substring(10,running_text.length()));
      is_finished = true;
    }
    else if(running_text.indexOf("STBR_") >= 0)
    {
      SerialBT.println("OK.Broadcast text has been changed");
      EEPROM_put(120, running_text.substring(10,running_text.length()));
      is_finished = true;
    }
    else
    {
      SerialBT.println("NOK.No such command");
    }
  }
  else if(running_text.length() > 0 && running_text.indexOf("1234_") < 0)
  {
    SerialBT.println("NOK.Auth is failed");
  }
  running_text = "";
}

void showClockTimer(String uiTime, byte bColonOn)
{
  dmd.drawChar(10, 0, '0'+((uiTime.toInt()%1000000)/100000), GRAPHICS_NORMAL);   // thousands
  dmd.drawChar(17, 0, '0'+((uiTime.toInt()%100000)/10000), GRAPHICS_NORMAL);   // hundreds
  dmd.drawChar(26, 0, '0'+((uiTime.toInt()%10000)/1000), GRAPHICS_NORMAL);   // tens
  dmd.drawChar(33, 0, '0'+ ((uiTime.toInt()%1000)/100), GRAPHICS_NORMAL);   // units
  dmd.drawChar(42, 0, '0'+((uiTime.toInt()%100)/10), GRAPHICS_NORMAL);   // thousands
  dmd.drawChar(49, 0, '0'+(uiTime.toInt()%10), GRAPHICS_NORMAL);   // hundreds
  if(bColonOn)
  {
    dmd.drawChar(22, 0, ':', GRAPHICS_OR);   // clock colon overlay on
    dmd.drawChar(38,  0, ':', GRAPHICS_OR);   // clock colon overlay on
  }
  else
  {
    dmd.drawChar(22, 0, ':', GRAPHICS_NOR);   // clock colon overlay off
    dmd.drawChar(38,  0, ':', GRAPHICS_NOR);   // clock colon overlay on
  }
}

void softwareReset()
{
  while (SerialBT.available() > 0) {
    char c = SerialBT.read();
    running_text += c;
  }
  running_text.trim();
  if(running_text.length() > 0 && running_text.indexOf("1234_") >= 0) { 
    if(running_text.indexOf("FRST") >= 0)
    {
      ESP.restart();
    }
  }
  else if(running_text.length() > 0 && running_text.indexOf("1234_") < 0)
  {
    SerialBT.println("NOK.Auth is failed");
  }
  running_text = "";
}

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
  setBrightness();
}

void initTimer()
{
  //return the clock speed of the CPU
  uint8_t cpuClock = ESP.getCpuFreqMHz();  
  //display timer
  disp_timer = timerBegin(1, cpuClock, true);
  timerAttachInterrupt(disp_timer, &triggerScan, true);
  timerAlarmWrite(disp_timer, TIMER_MS, true);
  // start an alarm 
  timerAlarmEnable(disp_timer);
}

void initSerial()
{
  Serial.begin(115200);
  SerialBT.begin(BLUETOOTH_NAME); //Bluetooth device name
}

void initFont(int fnt)
{
  if(fnt == 0)
  {
    dmd.clearScreen(true);
    dmd.selectFont(SystemFont5x7);
  }
  else if(fnt == 1)
  {
    dmd.clearScreen(true);
    dmd.selectFont(Arial_Black_16_ISO_8859_1);
  }
}

void normalMode()
{
  int length = run_text.length() + 1;
  char display[length];
  run_text.toCharArray(display, length);
  initFont(0);
  dmd.drawMarquee(display, length, 32*DISPLAYS_ACROSS, 9);
  long timerms = millis();
  is_finished = false;
  while (!is_finished) {
    if ((timerms + txt_speed) < millis()) {
      is_finished = dmd.stepMarquee(-2,0);
      getBluetoothData();
      // get the current time
      Ds1302::DateTime now;
      rtc.getDateTime(&now);

      static uint8_t last_second = 0;
      if (last_second != now.second)
      {
          last_second = now.second;
          showClockTimer(secondsToHMS((now.hour*3600)+(now.minute*60)+now.second), true);
      }
      timerms = millis();
    }
  }
}

void timerMode()
{
  while(is_timer)
  {
    softwareReset();
    initFont(1);
    showClockTimer(secondsToHMS(end_time_second), true);
    // dmd.drawString((64-(onair_text.length()*6))/2,  9, onair_txt, onair_text.length(), GRAPHICS_NORMAL);
    timer_ = millis();
    delay(100);
    startCountdown();
    if(end_time_second==0)
    {
      is_timer = false;
    }
  }
}

/* Main Program */
void setup(void)
{
  initSerial();
  initTimer();
  EEPROM.begin(512);
  rtc.init();
}

void loop(void)
{
  initVars();
  timerMode();
  normalMode();
}
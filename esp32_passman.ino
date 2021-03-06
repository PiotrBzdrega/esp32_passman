#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Crypto.h>
#include <ChaCha.h>
#include <BLAKE2s.h>
#include <SHA3.h>
#include <string.h>
#include <ESP.h>
#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

#define MAX_PLAINTEXT_SIZE  64
#define MAX_CIPHERTEXT_SIZE 64

#include "Arduino.h"
//#include <esp_wifi.h>
//#include <WiFi.h>
//#include "driver/adc.h"

#include <BleKeyboard.h>
#include "DetectBtn.h"

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h> //OLED library

#include <Adafruit_PN532.h> //RFID library

//struct for credentials
typedef struct
{
  String _name; //nick name of site/system
  String _login;
  String _pass;
  int _action; //0-login, password on one page; 1-login, password on two pages (1st ack -> login, 2nd ack -> password; 2-only password)
}  credential;


BleKeyboard bleKeyboard;  //bluetooth keyboard object

#define BUTTON_ACK 15  // GIOP15 pin connected to button
#define BUTTON_CHANGE 4  // GIOP4 pin connected to button

/* pins for RFID module SPI*/
#define PN532_SCK  (18)
#define PN532_MOSI (23)
#define PN532_SS   (5)
#define PN532_MISO (19)

SPIClass SPI_NFC(VSPI);
Adafruit_PN532 nfc(PN532_SS);

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define MAX_CREDENTIALS 4 //change in case of new element
#define MAX_PLAINTEXT_SIZE  64

#define IV_SIZE 8 //initial vector size
#define KEY_SIZE 32 //result of sha3 -> key size for chacha 
#define HASH_SIZE 32 //hash size for blake2s
ChaCha chacha; //chacha object

uint8_t chacha_key[KEY_SIZE]; //key used for decryption
uint8_t chacha_iv[IV_SIZE];    //initial vector for chacha encr/decr
uint8_t chacha_pass[KEY_SIZE]={0x48, 0x75, 0x78, 0x23, 0x31, 0x39, 0x4c, 0x65, //test messy pass
                               0x79, 0x21, 0x34, 0x38, 0x65, 0x74, 0x75, 0x74,
                               0x6f, 0x72, 0x41, 0x6c, 0x64, 0x23, 0x31, 0x39,
                               0x4f, 0x75, 0x73, 0x21, 0x38, 0x34, 0x19, 0x1e};


uint8_t test1[] = {0x52 , 0x79 , 0x64 , 0x7a , 0x61 , 0x6b , 0x33 , 0x30 , 0x23};
uint8_t test2[] = {0x48 , 0x75 , 0x78 , 0x23 , 0x31 , 0x39 , 0x4c , 0x65 , 0x79 , 0x21 , 0x34 , 0x38 , 0x79 , 0x61 , 0x6e , 0x64 , 0x65 , 0x78 , 0x41 , 0x6c , 0x64 , 0x23 , 0x31 , 0x39 , 0x4f , 0x75 , 0x73 , 0x21 , 0x38 , 0x34};
uint8_t key[] = {0x38, 0x0F, 0x75, 0xD6, 0x32, 0xF8, 0xBB, 0x2C,
                 0x44, 0x81, 0xF4, 0x27, 0x90, 0xB8, 0xAA, 0xE3,
                 0x38, 0x0F, 0x75, 0xD6, 0x32, 0xF8, 0xBB, 0x2C,
                 0x44, 0x81, 0xF4, 0x27, 0x90, 0xB8, 0xAA, 0xE3
                };
uint8_t iv[8] = {0x1C, 0x91, 0xE7, 0x99, 0x71, 0xC0, 0x1C, 0x2A};
uint8_t counter[8] = {0xEC, 0xE9, 0x24, 0x35, 0xB1, 0x6E, 0xBF, 0xFD};
byte pass_buffer[MAX_PLAINTEXT_SIZE];

uint8_t uid[7];  // Buffer to store the returned UID (max 7 bytes)
uint8_t uidLength;        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
uint8_t mac_address[6];  // Buffer to store the returned MAC

uint8_t log_hash[HASH_SIZE] =
{ 0x25, 0x89, 0x9c, 0x86, 0x3d, 0x67, 0x29, 0xfd,
  0x5e, 0xf2, 0x3,  0xb4, 0x95, 0xf4, 0x1,  0xef,
  0x79, 0xc5, 0xd2, 0x1f, 0xed, 0xcc, 0x88, 0xed,
  0x13, 0xb7, 0xbe, 0x51, 0xe9, 0xb8, 0x53, 0xc9
}; //log hash result

uint8_t cipherdata[sizeof(test1)] = {0};

uint8_t buffer[sizeof(test1)] = {0};

credential credentials[MAX_CREDENTIALS] = {
  {"Git", "piotrbzdrega@yandex.com", "0a213c2239336b687b", 0},
  {"Etutor", "piotrbzdrega@yandex.com", "102d207b6961143d21796c603d2c2d2c372a19343c7b6961172d2b79606c", 0},
  {"Yandex", "piotrbzdrega@yandex.com", "102d207b6961143d21796c602139363c3d2019343c7b6961172d2b79606c", 1},
  {"Legion Windows", "" , "6a6b6e69", 2}
};

int credentialPointer = -1; //pointer to credentials (start with -1), when bluetooth whould establish connection (edge) -> jump to 0 (first element)


bool stepCredentialActive = false; //credential typing action pending? -> block to move forward


bool BlConnected = false; //is device connected

//create button objects
DetectBtn ackBtn(BUTTON_ACK);
DetectBtn changeBtn(BUTTON_CHANGE);

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  Serial.begin(115200);

   //adc_power_off();
   //adc_power_release();
   //WiFi.mode(WIFI_OFF);    // Switch WiFi off
   //setCpuFrequencyMhz(80);

  retrieveMac(mac_address);//save esp mac address

  /* DEBUG : PRINT */
  printHex("MAC ADDRESS", mac_address, sizeof(mac_address));

  displayInit(); //initialize dsipley; show welcome screen

  rfidInit(uid, &uidLength); //initialize PN532; user needs to enter secret code before use main functionality

  /* DEBUG : PRINT */
  printHex("CARD ID", uid, uidLength);



  bleKeyboard.begin(); //start blueetooth keyboard connection




}

void loop() {
  // check to see if it's time to blink the LED; that is, if the difference
  // between the current time and last time you blinked the LED is bigger than
  // the interval at which you want to blink the LED.
  unsigned long currentMillis = millis();


   
  ackBtn.read();     //read ack button
  changeBtn.read();  //read change button

  if (changeBtn.isRisingEdge())
  {
      //ENCRYPTION
      chacha.setKey(chacha_key, chacha.keySize()); //set encryption key
      chacha.setIV(chacha_iv, chacha.ivSize());    //set encryption initial vector 
      uint8_t chcha_cipher[KEY_SIZE];              //cipher declaration
      memset(chcha_cipher, 0, sizeof(chcha_cipher)); //reset cipher before encryption
      chacha.encrypt(chcha_cipher, chacha_pass, sizeof(chacha_pass)); //operative encryption

      printHex("cipher", chcha_cipher, sizeof(chcha_cipher)); //print cipher

      //DECRYPTION
      chacha.setKey(chacha_key, chacha.keySize()); //set decryption key
      chacha.setIV(chacha_iv, chacha.ivSize());    //set decryption initial vector 
      uint8_t chcha_digest[KEY_SIZE];              //digest declaration
      chacha.decrypt(chcha_digest, chcha_cipher, sizeof(chcha_cipher));

      printHex("digest", chcha_digest, sizeof(chcha_digest)); //print cipher

          //in last byte we stored password length
      for (int i = 0; i < (int)chcha_digest[sizeof(chcha_digest) - 1]; i++)
      {
          Serial.write(chcha_digest[i]);
      }
    
 /*   chacha.setKey(key, chacha.keySize());
    chacha.setIV(iv, chacha.ivSize());
    //chacha.setCounter(counter, chacha.ivSize());

    for (int i = 0; i < sizeof(test1); i++)
    {
      Serial.print(test1[i], HEX);
    }
    Serial.println();

    memset(cipherdata, 0, sizeof(cipherdata));

    chacha.encrypt(cipherdata, test1, sizeof(test1));




    for (int i = 0; i < sizeof(cipherdata); i++)
    {
      Serial.print(cipherdata[i], HEX);
    }

    Serial.print(cipherdata[sizeof(cipherdata) - 1]);
    Serial.println();


    memset(buffer, 0, sizeof(buffer));

    chacha.setKey(key, chacha.keySize());
    chacha.setIV(iv, chacha.ivSize());

    chacha.decrypt(buffer, cipherdata, sizeof(cipherdata));

    for (int i = 0; i < sizeof(buffer); i++)
    {
      Serial.write(buffer[i]);
    }
    Serial.println();
 */

  }


  if (bleKeyboard.isConnected()) {

            

    
    //detect pressed ack button and we have credential pointer in range
    if (ackBtn.isRisingEdge() && credentialPointer > -1)
    {
      stepCredentialActive = sendCredentials(credentialPointer, stepCredentialActive); //send requested Credential
    }

    if (!BlConnected || (changeBtn.isRisingEdge() && !stepCredentialActive)) //first cycle device connected or change button pushed
    {
      credentialPointer++; //move credential pointer forward


      if (credentialPointer >= MAX_CREDENTIALS) //check if we go out of the range
        credentialPointer = 0;                  //if yes start from 0


      showCredentials(credentialPointer); //show next credentials entry


      BlConnected = true; //set bluetooth device as connected
    }
  }
  else {


    scrolltext(); // Draw scrolling text "Not Connected"
  }
}

/*  DISPLAY MOVING "NOT CONNECTED"  */
void scrolltext(void) {
  display.clearDisplay();

  display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println(F("Not Connected"));
  display.display();      // Show initial text
  delay(100);

  // Scroll in various directions, pausing in-between:
  display.startscrollright(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrollleft(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrolldiagright(0x00, 0x07);
  delay(2000);
  display.startscrolldiagleft(0x00, 0x07);
  delay(2000);
  display.stopscroll();
  delay(1000);
}


/*  CONVERT FROM CHAR DEC TO HEX  */
byte decTohex(char ms, char ls)
{
  //two char's to byte
  byte b[2] = {ls, ms};

  for (int i = 0; i < 2; i++)
  {
    //check if character lie in '0'-'9' range
    if (b[i] < ':')
      b[i] = b[i] - '0'; //ex '6'-'0'=54-48=6
    //check if character is capital leter
    else
      b[i] = ( b[i] > 'F' ) ? (b[i] - 'W') : (b[i] - '7'); //DEC 'A'-65, '7'-55 'a'-97, 'W'-87 //HEX A/a-10 (...) F/f-15
  }

  return (b[1] * 16 + b[0]); //Dec to Hex
}

/*  PRIMITIVE XOR BYTE OPERATION WITH HARDCODDED KEY */
byte encryptDecrypt(byte toEncrypt) {
  byte key = 'X'; //Any char will work
  byte output = toEncrypt;

  output = toEncrypt ^ key ;

  return output;
}

/*  DISPLAY CURRENT CREDENTIALS */
void showCredentials(int entryPointer) {
  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  //Name
  display.print("Name: ");
  display.println(credentials[entryPointer]._name);

  //Login
  display.print("Login: ");
  display.println(credentials[entryPointer]._login);


  //Password
  display.print("Password: ");

  //extract hex from String
  for (int i = 1; i < credentials[entryPointer]._pass.length(); i = i + 2)
  {
    display.print(decTohex(credentials[entryPointer]._pass.charAt(i - 1), credentials[entryPointer]._pass.charAt(i)), HEX);
  }
  display.display();
  delay(50);
}

/* SAVE ESP32 ID (MAC) */
void retrieveMac( uint8_t* mac_address)
{
  uint64_t chipid = ESP.getEfuseMac(); //get mac
  memcpy(mac_address, &chipid, sizeof(chipid)); //copy content of variable to array
}

/*  DETECT OLED AND DISPLAY WELCOME SCREEN */
void displayInit()
{
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(2);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  display.print("Gib den Code ein"); //Enter code text

  // Show the display buffer on the screen. You MUST call display() after
  display.display(); // drawing commands to make them visible on screen!
}

/*  DETECT PN532 AND WAIT FOR RFID CARD */
void rfidInit(uint8_t* uid, uint8_t* uidLength)
{
  /*start with RFID module*/
  SPI_NFC.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    for (;;); // Don't proceed, loop forever
  }

  // Set the max number of retry attempts to read from a card
  // This prevents us from waiting forever for a card, which is
  // the default behaviour of the PN532.
  nfc.setPassiveActivationRetries(0xFF);

  // configure board to read RFID tags
  nfc.SAMConfig();

  boolean success = false;


  while (!success)
  {
    // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
    // 'uid' will be populated with the UID, and uidLength will indicate
    // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], uidLength);

    if (success)
    {
      Serial.println("Found a card!");
      printHex("uid", uid , *uidLength); //card uid
      success = verifyHash();

       //card was wrong one -> wait few moments to start read again
      if (!success)
        while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], uidLength,1000)) {}

    }

    
  }

  //nfc.shutDown(); //shut down module in case we recognized correct uid
  //delay(2000);

}

boolean verifyHash()
{
  if ( uidLength == 0 || sizeof(mac_address) == 0) //zero size log_key component
    return false;

  uint8_t log_key[2 * sizeof(mac_address) + uidLength]; //log_key size of 2*MAC length(2*6byte) + rfid card (4-7bytes)
  int idx_byte = 0; //current element idx in log_key

  memcpy(&log_key[idx_byte], mac_address, sizeof(mac_address)); //copy mac_address to log_key array
  idx_byte += sizeof(mac_address); //move local pointer forward
  memcpy(&log_key[idx_byte], uid, uidLength); //copy uid to log_key array
  idx_byte += uidLength; //move local pointer forward
  memcpy(&log_key[idx_byte], mac_address, sizeof(mac_address)); //copy mac_address to log_key array


  printHex("log_key", log_key , sizeof(log_key)); //print log_key

/* CARD VERIFICATION */
  BLAKE2s blake2s;  //hash object
  uint8_t hash[HASH_SIZE]; // hash result

  blake2s.reset(); // Resets the hash ready for a new hashing process

  blake2s.update(log_key, sizeof(log_key)); //Updates the hash with data

  blake2s.finalize(hash, sizeof(hash)); //Finalizes the hashing process and returns the hash.

  printHex("hash", hash, sizeof(hash)); //print hash

  printHex("log_hash", log_hash, sizeof(log_hash)); //print hash

  if (memcmp(hash, log_hash, sizeof(hash)) != 0) //check if new hash is identical with saved
    return false;

  Serial.println();
  Serial.println("Hauska Tutustua!");

/* KEY FOR DESCRYPTION ASSIGNMENT */
  SHA3_256 sha3_256;
  
  sha3_256.reset(); // Resets the hash ready for a new hashing process

  sha3_256.update(log_key, sizeof(log_key)); //Updates the hash with data

  sha3_256.finalize(chacha_key, sizeof(chacha_key)); //Finalizes the hashing process and returns the hash.

  printHex("chacha_key", chacha_key, sizeof(chacha_key)); //print hash
  
  idx_byte = 0; // reset before next operation

  //fill initial vector with card uid content
  memcpy(&chacha_iv[idx_byte], uid, uidLength); //copy uid to iv 
  idx_byte += uidLength; //move local pointer forward
  memcpy(&chacha_iv[idx_byte], uid, uidLength); //copy uid to iv 

  return true;
}

void printHex(String header, uint8_t* array_to_print , int array_size)
{
  Serial.println(); //start with new line
  Serial.print(header); Serial.print(": "); //print header + padding

  //print each byte separately
  for (uint8_t i = 0; i < array_size; i++)
  {
    Serial.print(" 0x"); Serial.print(array_to_print[i], HEX);
  }

}

/*  FORWARD CURRENT CREDENTIALS VIA BLUETOOTH */
bool sendCredentials(int entryPointer, bool stepCredential) {

  switch (credentials[entryPointer]._action)
  {
    case 0:

      for (int i = 0; i < credentials[entryPointer]._login.length(); i++)
      {
        bleKeyboard.print(credentials[entryPointer]._login.charAt(i));
        delay(20);
      }

      bleKeyboard.press(KEY_TAB);
      delay(20);
      bleKeyboard.release(KEY_TAB);
      delay(20);

      //extract hex from String
      for (int i = 1; i < credentials[entryPointer]._pass.length(); i = i + 2)
      {
        bleKeyboard.write(encryptDecrypt(decTohex(credentials[entryPointer]._pass.charAt(i - 1), credentials[entryPointer]._pass.charAt(i))));
        delay(20);
      }

      bleKeyboard.press(KEY_RETURN);
      delay(20);
      bleKeyboard.release(KEY_RETURN);
      delay(20);
      break;
    case 1:
      if (!stepCredential)
      {
        for (int i = 0; i < credentials[entryPointer]._login.length(); i++)
        {
          bleKeyboard.print(credentials[entryPointer]._login.charAt(i));
          delay(20);
        }
        bleKeyboard.press(KEY_RETURN);
        delay(20);
        bleKeyboard.release(KEY_RETURN);
        delay(20);

        //set step credential pending to print only password during next call
        stepCredential = true;
      }
      else
      {
        //extract hex from String
        for (int i = 1; i < credentials[entryPointer]._pass.length(); i = i + 2)
        {
          bleKeyboard.write(encryptDecrypt(decTohex(credentials[entryPointer]._pass.charAt(i - 1), credentials[entryPointer]._pass.charAt(i))));
          delay(20);
        }

        bleKeyboard.press(KEY_RETURN);
        delay(20);
        bleKeyboard.release(KEY_RETURN);
        delay(20);
        stepCredential = false;
      }
      break;
    case 2:

      //extract hex from String
      for (int i = 1; i < credentials[entryPointer]._pass.length(); i = i + 2)
      {
        bleKeyboard.write(encryptDecrypt(decTohex(credentials[entryPointer]._pass.charAt(i - 1), credentials[entryPointer]._pass.charAt(i))));
        delay(20);
      }

      bleKeyboard.press(KEY_RETURN);
      delay(20);
      bleKeyboard.release(KEY_RETURN);
      delay(20);
      break;


  }
  return stepCredential;

}

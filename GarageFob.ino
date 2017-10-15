/*
 * HID RFID Reader Wiegand Interface for Arduino Uno
 * Originally by  Daniel Smith, 2012.01.30 -- http://www.pagemac.com/projects/rfid/arduino_wiegand
 * 
 * Updated 2016-11-23 by Jon "ShakataGaNai" Davis.
 * See https://obviate.io/?p=7470 for more details & instructions
 * 
 * Updated 2017-10-14 by James Ranson (james@ranson.org)
 * Refactored  the bit parser, added Access Control List and Garage Opening capabilities via pin6
*/


#define MAX_BITS 100                 // max number of bits 
#define WEIGAND_WAIT_TIME  5000      // time to wait for another weigand pulse.  
 
unsigned char databits[MAX_BITS];    // stores all of the data bits
unsigned char bitCount;              // number of bits currently captured
unsigned char flagDone;              // goes low when data is currently being captured
unsigned int weigand_counter;        // countdown until we assume there are no more bits

const int DATA0_PIN = 2;             // Pin for Data0 from the HID Reader (Green Wire)
const int DATA1_PIN = 3;             // Pin for Data1 from the HID Reader (White Wire)
const int RELAY_PIN = 6;             // Pin for Output to the Garage Relay

// Any card in this Access list will open the garage when scanned.
// Connect the Arduino to your Serial Monitor to scan a new card and see its values, so you can add to the ACL
const int MAX_ACCESS_ENTRIES = 5;
unsigned long AccessList[MAX_ACCESS_ENTRIES][2] = {
 // FacilityID, CardID       // User
  { 1234 ,      567890  } ,  // John Doe
  { 1234 ,      212394  } ,  // Jane Doe
  { 1234 ,      211273  } ,  // Mom Doe
  { 1234 ,      367801  } ,  // Dad Doe
  { 1234 ,      551238  }    // Granpa Doe
};

// interrupt that happens when INTO goes low (0 bit)
void ISR_INT0() {
  // Serial.print("0");   // uncomment this linwe to display raw binary
  bitCount++;
  flagDone = 0;
  weigand_counter = WEIGAND_WAIT_TIME;  
 
}
 
// interrupt that happens when INT1 goes low (1 bit)
void ISR_INT1() {
  // Serial.print("1");   // uncomment this line to display raw binary
  databits[bitCount] = 1;
  bitCount++;
  flagDone = 0;
  weigand_counter = WEIGAND_WAIT_TIME;
}
 
void setup() {
  // Data In wires from HID Reader
  pinMode(DATA0_PIN, INPUT);     // DATA0 (INT0)
  pinMode(DATA1_PIN, INPUT);     // DATA1 (INT1)

  // Setup Data Out wire to the Garage Opener Relay - HIGH is off, LOW is on
  digitalWrite(RELAY_PIN, HIGH);    // Turn the relay OFF
  pinMode(RELAY_PIN, OUTPUT);       // Now set it to output. 
  // This ordering ^^^ is important or the garage will open if the arduiono reboots.

  // Setup the Serial Monitor so we can read new card data to update the access list.
  Serial.begin(9600);
  Serial.println("RFID Reader");
 
  // binds the ISR functions to the falling edge of INTO and INT1
  attachInterrupt(0, ISR_INT0, FALLING);  
  attachInterrupt(1, ISR_INT1, FALLING);
 
  weigand_counter = WEIGAND_WAIT_TIME;
}
 
void loop()
{
  // This waits to make sure that there have been no more data pulses before processing data
  if (!flagDone) {
    if (--weigand_counter == 0)
      flagDone = 1;  
  }
 
  // if we have bits and the weigand counter went out
  if (bitCount > 0 && flagDone) {

    unsigned long facilityCode=0;        // decoded facility code
    unsigned long cardCode=0;            // decoded card code
    
    unsigned char i;
 
    Serial.print("Read ");
    Serial.print(bitCount);
    Serial.println(" bits. ");

    byte fc_start = 0;
    byte cc_start = 0;
    byte cc_end   = 0;

    // Based on the # of bits read, we attempt to determine the card format
    // from which we derive the boundaries of the Facility Code and Card Code for Parsing

    // In general, fc_start is the # of leading parity bits in front of the facility id (1 or 2)
    // cc_start is fc_cstart + length (in bits) of facility id section
    // cc_end is bitCount-1 or bitCount-2 dependng on the # of trailing parity bits
    
    if ( bitCount == 35 ) {
      fc_start = 2;
      cc_start = 14;
      cc_end   = 34;
    } else if ( bitCount == 26 ) {
      fc_start = 1;
      cc_start = 9;
      cc_end   = 25;
    } else if ( bitCount == 37 ) {
      fc_start = 1;
      cc_start = 17;
      cc_end   = 36;
    }

    // We should have these 3 values if we recognized the card format and can proceed to parse
    // Otherwise access is denied due to unrecognized card format.
    if ( fc_start > 0 && cc_start > 0 && cc_end > 0 )
    {
      // Calculate Facility Code
      for (i=fc_start; i<cc_start; i++) {
         facilityCode <<=1;
         facilityCode |= databits[i];
      }
 
      // Calculate Card Code
      for (i=cc_start; i<cc_end; i++) {
         cardCode <<=1;
         cardCode |= databits[i];
      }

      // Print Card Data to Serial Monitor. Do this because if it's a unknown card that might need access, 
      // we will need to see the codes in the Monitor in order to grant new access by expanding our ACL array.
      printBits(facilityCode, cardCode);

      // Authenticate the fob by checking for it's FC and CC in the Access List array and react accordingly.
      if ( verifyAccess(facilityCode, cardCode) == 1 ) { openGarage(); } else { accessDenied(); }

    }
 
     // cleanup and get ready for the next card
     bitCount = 0;
     for (i=0; i<MAX_BITS; i++) 
     {
       databits[i] = 0;
     }
  }
}

// Prints out the Card Data for debugging purposes
void printBits( unsigned long facilityCode, unsigned long cardCode) {
      Serial.print("FC = ");
      Serial.print(facilityCode);
      Serial.print(", CC = ");
      Serial.println(cardCode);
}

// Checks the access list for the scanned card
int verifyAccess( unsigned long facilityCode, unsigned long cardCode) {
  int access_granted = 0;
  for (int i=0;i<MAX_ACCESS_ENTRIES;i++) {
    if ( AccessList[i][0] == facilityCode && AccessList[i][1] == cardCode ) { access_granted = 1; break; }
  }
  return access_granted;
}

// Simulates a button press on the garage opener by activating the relay for 1 second
void openGarage() {
  Serial.println("Open Sesame!!");
  pinMode(RELAY_PIN, OUTPUT);    // Ensure output mode for the relay-connected pin
  digitalWrite(RELAY_PIN, LOW);  // Turn on the Relay
  delay(1000);                   // Wait 1 second
  digitalWrite(RELAY_PIN, HIGH); // Turn off the Relay
  Serial.println("Done!");
}

// Prints an access denied debug message in the Serial Monitor
void accessDenied() {
  Serial.println("No soup for you!!");
}



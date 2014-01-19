/* 
 using functions from MMA8452Q sparkfun basic - https://www.sparkfun.com/products/10955 
 using built-in arduino SD library
 using ShiftOutX - http://playground.arduino.cc/Main/ShiftOutX
 */
 

#include <ShiftOutX.h>
#include <ShiftPinNo.h>
#include <Wire.h> 
#include <SD.h>

shiftOutX regOne(9, 10, 8, MSBFIRST, 5); // declare shiftOutX object with pins 9/10/8 for latch/data/clock. Most-Significant-Bit-First, and 5 registers.

// The SparkFun breakout board defaults to 1, set to 0 if SA0 jumper on the bottom of the board is set
#define MMA8452_ADDRESS 0x1D  // 0x1D if SA0 is high, 0x1C if low

//Define a few of the registers that we will be accessing on the MMA8452
#define OUT_X_MSB 0x01
#define XYZ_DATA_CFG  0x0E
#define WHO_AM_I   0x0D
#define CTRL_REG1  0x2A

#define GSCALE 8 // Sets full-scale range to +/-2, 4, or 8g. Used to calc real g values.



///// gross, global variables!/////////////////////////////////
File myFile;
int image_num_lines = 40;					//number of columns in an image
unsigned long image_data [40];
int swing_count = 0; 						//number of oscillations, used for animations
int swing_direction = 1;
int display_time = 1;
int normal_accel;
int last_normal_accel = 0;
float max_accel = 8;
float min_accel = -8; 
float temp_max_accel = 0;
float temp_min_accel = 0;
unsigned long last_accel_time_check = 0;
unsigned long last_frame_time_check = 0;
unsigned long iteration_counter;


void setup()
{
  //Serial.begin(57600);
  Wire.begin(); //Join the bus as a master
  initMMA8452(); //Test and intialize the MMA8452
  pinMode(10, OUTPUT);	//set up SPI communication with the SD card
  if (!SD.begin(4)) {
    //Serial.println("SD initialization failed!");
    return;
  }

  myFile = SD.open("test.txt");
  myFile.seek(0);  
  for (int i = 0; i < image_num_lines; i++){
    image_data[i] = 0;
  }
  load_image();
}

void loop()
{  
  //10 waves / second @ 40 lines = 2.5ms per line
  //2.5ms for code iteration
  //10ms for SD load

  int accelCount;
  accelCount = readAccelData();  // Read the x adc values
  float accelG;  // Stores the real accel value in g's
  accelG = (float) accelCount / ((1<<12)/(2*GSCALE));  // get actual g value, this depends on scale being set
  
  if (accelG > temp_max_accel) //see if we've hit a maximum for this second
  {
    temp_max_accel = accelG;
  }
  else if (accelG < temp_min_accel) //see if we've hit a minimum for this second
  {
    temp_min_accel = accelG;
  }
  
  if (millis() - last_accel_time_check > 1000) //check if a second has passed - get new acceleration range
  {
     max_accel = temp_max_accel;
     min_accel = temp_min_accel;  
     temp_max_accel = 0;
     temp_min_accel = 0;
     last_accel_time_check = millis();
  }   
  
  //normalize the acceleration; map it to a column in the image.
  normal_accel = image_num_lines + swing_direction - (int) (floor((accelG-min_accel)*(image_num_lines-1)/(max_accel-min_accel)));
  if (normal_accel < 0){normal_accel = 0;} //ensure we don't try to get a negative array index
  else if (normal_accel > image_num_lines - 1){normal_accel = image_num_lines - 1;} //ensure we don't try to get too big an array index
  
  if (normal_accel != last_normal_accel){ // only update the displayed column if its changed in the last loop - prevent blinking
    display_column(normal_accel);
  }
  last_normal_accel = normal_accel;
  
  if (normal_accel < 4) //if we're near one side of the wave
  {
    if (millis() - last_frame_time_check > 100 * display_time){ //if enough time has passed, get the next frame
        myFile.read(); //read carriage return
        myFile.read(); //read newline character
        for (int i = 0; i < image_num_lines; i++){ //clear old image data
          image_data[i] = 0;
        }
        load_image(); //load new image
        last_frame_time_check = millis();
    }
    swing_direction = 1;
  }
  else if (normal_accel > image_num_lines - 4){
    swing_direction = -1;
  }

}




void load_image(){
  
  boolean quitflag = 0;
  int line_number = 0;
  char storage[10];
  int storage_digit = 0;
  char readcharacter;
  
  if (myFile) {  
    display_time = myFile.read();  //first character of a line is always how long i want that frame to be displayed
    if (display_time == -1){ //if we hit the end of the file, go back to the beginning
      myFile.seek(0);
      display_time = myFile.read();
    }
    display_time = display_time - 48; //convert from ascii to int
    while(quitflag == 0){ //until you hit an 'x'
      readcharacter = myFile.read();
      //Serial.print(readcharacter);
      
      switch (readcharacter){
        case 115: //s
          image_data[line_number] = atol(storage); //convert collected array to a long number
          for (int i = 0; i < 10; i++){ //loop through 10 digits collected and clear old results
            storage[i] = NULL;
          }
          storage_digit = 0;
          line_number = line_number + 1;
          break;
        case 120: //x
          quitflag = 1;
          image_num_lines = line_number - 1;
        default: //if you hit any number, just store it in an array
          storage[storage_digit] = readcharacter;
          storage_digit = storage_digit + 1;
      }
    }
  }
}


void display_column(int normal_accel){
  regOne.allOff();
  regOne.pinOn(image_data[normal_accel]);
}


int readAccelData() //modified to only output x data
{
  byte rawData[6];  // x/y/z accel register data stored here

  readRegisters(OUT_X_MSB, 6, rawData);  // Read the six raw data registers into data array

  
    int gCount = (rawData[0] << 8) | rawData[(0)+1];  //Combine the two 8 bit registers into one 12-bit number
    gCount >>= 4; //The registers are left align, here we right align the 12-bit integer

    // If the number is negative, we have to make it so manually (no 12-bit data type)
    if (rawData[0] > 0x7F)
    {  
      gCount = ~gCount + 1;
      gCount *= -1;  // Transform into negative 2's complement #
    }

    return gCount; //Record this gCount into the 3 int array
  
}

// Initialize the MMA8452 registers 
// See the many application notes for more info on setting all of these registers:
// http://www.freescale.com/webapp/sps/site/prod_summary.jsp?code=MMA8452Q
void initMMA8452()
{
  Serial.println("MMA8452 Basic Example");
  byte c = readRegister(WHO_AM_I);  // Read WHO_AM_I register
  //Serial.println("MMA8452 Basic Example");
  if (c == 0x2A) // WHO_AM_I should always be 0x2A
  {  
    Serial.println("MMA8452Q is online...");
  }
  else
  {
    Serial.print("Could not connect to MMA8452Q: 0x");
    Serial.println(c, HEX);
    while(1) ; // Loop forever if communication doesn't happen
  }

  MMA8452Standby();  // Must be in standby to change registers

  // Set up the full scale range to 2, 4, or 8g.
  byte fsr = GSCALE;
  if(fsr > 8) fsr = 8; //Easy error check
  fsr >>= 2; // Neat trick, see page 22. 00 = 2G, 01 = 4A, 10 = 8G
  writeRegister(XYZ_DATA_CFG, fsr);

  //The default data rate is 800Hz and we don't modify it in this example code

  MMA8452Active();  // Set to active to start reading
}

// Sets the MMA8452 to standby mode. It must be in standby to change most register settings
void MMA8452Standby()
{
  byte c = readRegister(CTRL_REG1);
  writeRegister(CTRL_REG1, c & ~(0x01)); //Clear the active bit to go into standby
}

// Sets the MMA8452 to active mode. Needs to be in this mode to output data
void MMA8452Active()
{
  byte c = readRegister(CTRL_REG1);
  writeRegister(CTRL_REG1, c | 0x01); //Set the active bit to begin detection
}

// Read bytesToRead sequentially, starting at addressToRead into the dest byte array
void readRegisters(byte addressToRead, int bytesToRead, byte * dest)
{
  Wire.beginTransmission(MMA8452_ADDRESS);
  Wire.write(addressToRead);
  Wire.endTransmission(false); //endTransmission but keep the connection active

  Wire.requestFrom(MMA8452_ADDRESS, bytesToRead); //Ask for bytes, once done, bus is released by default

  while(Wire.available() < bytesToRead); //Hang out until we get the # of bytes we expect

  for(int x = 0 ; x < bytesToRead ; x++)
    dest[x] = Wire.read();    
}

// Read a single byte from addressToRead and return it as a byte
byte readRegister(byte addressToRead)
{
  Wire.beginTransmission(MMA8452_ADDRESS);
  Wire.write(addressToRead);
  Wire.endTransmission(false); //endTransmission but keep the connection active

  Wire.requestFrom(MMA8452_ADDRESS, 1); //Ask for 1 byte, once done, bus is released by default

  while(!Wire.available()) ; //Wait for the data to come back
  return Wire.read(); //Return this one byte
}

// Writes a single byte (dataToWrite) into addressToWrite
void writeRegister(byte addressToWrite, byte dataToWrite)
{
  Wire.beginTransmission(MMA8452_ADDRESS);
  Wire.write(addressToWrite);
  Wire.write(dataToWrite);
  Wire.endTransmission(); //Stop transmitting
}






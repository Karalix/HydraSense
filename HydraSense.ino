#include <LinkedList.h>

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

#define INTERRUPT_PIN 2  // use pin 2 on Arduino Uno & most boards
#define NB_POINTS 50
#define SIZEOF_ARRAY(_array) (sizeof(_array) / sizeof(_array[0]))
#define FLOAT_MAX_VALUE 15000


struct Result{
  int id;
  float score;
};

struct Template {
  float templatePath[NB_POINTS];
  int id;
};

//MPU variables

MPU6050 mpu;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

int limiter = 0 ;


LinkedList<float> *measureList = new LinkedList<float>();
//BMPoint measure[NB_POINTS];
Template templates[] = {
  {{77.16,
77.44,
78.24,
78.99,
79.46,
79.36,
78.36,
76.46,
74.10,
71.73,
69.56,
67.62,
65.38,
62.58,
59.44,
56.30,
53.29,
50.38,
47.46,
44.38,
41.25,
38.27,
35.54,
33.01,
30.56,
28.06,
25.51,
22.92,
20.39,
18.12,
16.24,
14.55,
12.76,
10.88,
9.02,
7.38,
6.05,
4.89,
3.72,
2.58,
1.48,
0.28,
-1.01,
-2.25,
-3.35,
-4.30,
-5.12,
-5.86,
-6.35,
-6.61},1},
{{-13.60,
-13.31,
-12.85,
-12.27,
-11.61,
-10.89,
-10.12,
-9.23,
-8.19,
-6.93,
-5.42,
-3.69,
-1.88,
-0.14,
1.60,
3.43,
5.39,
7.46,
9.53,
11.64,
13.91,
16.41,
19.14,
22.10,
25.23,
28.56,
32.04,
35.40,
38.34,
41.16,
44.07,
46.96,
49.66,
52.16,
54.74,
57.52,
60.10,
62.52,
64.61,
66.36,
67.87,
69.20,
70.33,
71.30,
72.12,
72.83,
73.53,
74.20,
74.75,
75.23},2}
  };

int nbTemplates = SIZEOF_ARRAY(templates);

//Interrupt detection routine
volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}

void printPoint(float p) {
  Serial.print(p);
  Serial.println(',');
}

float distance(float a, float b) {
  //printPoint(a);
  //printPoint(b);
  //Serial.println(abs(a-b));
  return abs(a-b);
}


float pathDistance(float templ[], LinkedList<float> *path2) {
  if(path2->size()< NB_POINTS) {
    return -1;
  }
  int i;
  float pDist = 0;
  for(i = 0 ; i < NB_POINTS ; i++) {
    pDist += distance(templ[i],path2->get(i));
  }

  return pDist;
}


Result recognize() { 
  Result r = {-1,-1};
  int i;
  int templateRecognized = -1 ;
  float distance = FLOAT_MAX_VALUE;

  if(measureList->size() < NB_POINTS) {
    return r;
  }
  
  for(i=0;i<nbTemplates;i++) {
    float a = pathDistance(templates[i].templatePath, measureList);
    if(a < distance) {
      distance = a;
      templateRecognized = templates[i].id;
    }
  }

  r.id = templateRecognized ;
  r.score = (4500.-distance)/4500;
  /*Serial.print(r.id);
            Serial.print(':');*/
            //Serial.println(distance);
  return r;
}



void setup() {
  
  pinMode(LED_BUILTIN, OUTPUT);
    // join I2C bus (I2Cdev library doesn't do this automatically)
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
        Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif

    // initialize serial communication
    Serial.begin(115200);

    // initialize device
    Serial.println(F("Initializing I2C devices..."));
    mpu.initialize();
    pinMode(INTERRUPT_PIN, INPUT);

    // verify connection
    Serial.println(F("Testing device connections..."));
    Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // wait for ready
    Serial.println(F("\nSend any character to begin DMP programming and demo: "));
    while (Serial.available() && Serial.read()); // empty buffer
    while (!Serial.available());                 // wait for data
    while (Serial.available() && Serial.read()); // empty buffer again

    // load and configure the DMP
    Serial.println(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();

    // supply your own gyro offsets here, scaled for min sensitivity
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1788); // 1688 factory default for my test chip

    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        Serial.println(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        Serial.println(F("DMP ready! Waiting for first interrupt..."));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }
}

void loop() {
    // if programming failed, don't try to do anything
    if (!dmpReady) return;

    // wait for MPU interrupt or extra packet(s) available
    while (!mpuInterrupt && fifoCount < packetSize) {
        // other program behavior stuff here
        // .
        // .
        // .
        // if you are really paranoid you can frequently test in between other
        // stuff to see if mpuInterrupt is true, and if so, "break;" from the
        // while() loop to immediately process the MPU data
        // .
        // .
        // .
    }

    // reset interrupt flag and get INT_STATUS byte
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();

    // get current FIFO count
    fifoCount = mpu.getFIFOCount();

    // check for overflow (this should never happen unless our code is too inefficient)
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        Serial.println(F("FIFO overflow!"));

    // otherwise, check for DMP data ready interrupt (this should happen frequently)
    } else if (mpuIntStatus & 0x02) {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        
        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;

        if(limiter == 0){

          // display Euler angles in degrees
          mpu.dmpGetQuaternion(&q, fifoBuffer);
          mpu.dmpGetGravity(&gravity, &q);
          mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
 
          mpu.dmpGetAccel(&aa, fifoBuffer);
          mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
  
          float roll = ypr[2]*180/M_PI;
          measureList->add(roll);
          if(measureList->size() > NB_POINTS) {
            measureList->shift();
          }
          //printPoint(roll);
          //printPoint(measureList->get(measureList->size()-1));
          //Serial.println(nbTemplates);
          //Serial.println(measureList->size());
          
          Result res = recognize();
          if(res.score >= 0.91){
            switch(res.id){
              case 1 :
              Serial.println("Commence a boire !");
              break;
              case 2 :
              Serial.println("Arrete de boire !");
            }
            /*
            Serial.print(res.id);
            Serial.print(':');
            Serial.println(res.score);*/
          }

        }

        ++limiter;
        if(limiter == 2) {
          limiter = 0;
        }
      
    }
}





/*
  MecanumbotController.ino - Mecanumbot low-level ROS driver.
 Created by Josh Villbrandt (http://javconcepts.com/), July 19, 2012.
 Released into the public domain.
 */

// ROS
#include <ros.h>
#include <ros/node_handle.h>
#include <geometry_msgs/Twist.h>
#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/UInt32.h>
#include <mecanumbot/RobotHazardsEnable.h>
#include <mecanumbot/RobotHealth.h>
#include <Utils.h>
#include <PID_v1.h>
//For Setting Up Motor Controller Pins:
#include <SparkFun_TB6612.h>
#define PWMA 12    //A Motor Speed
#define DIRA1 34
#define DIRA2 35  //A Motor Direction
#define PWMB 8    //B Motor Speed  
#define DIRB1 37
#define DIRB2 36  //B Motor Direction
#define PWMC 9   //C Motor Speed
#define DIRC1 43
#define DIRC2 42  //C Motor Direction
#define PWMD 5    //D Motor Speed
#define DIRD1 26
#define DIRD2 27  //D Motor Direction
#define STBY 19
Motor motorA = Motor(DIRA1, DIRA2, PWMA, -1, STBY);
Motor motorB = Motor(DIRB1, DIRB2, PWMB, 1, STBY);
Motor motorC = Motor(DIRC1, DIRC2, PWMC, -1, STBY);
Motor motorD = Motor(DIRD1, DIRD2, PWMD, 1, STBY);

#define ENCA_A 18 //A Moter Encoder
#define ENCA_B 31
#define ENCB_A 19 // B Moter Encoder
#define ENCB_B 38 
#define ENCC_A 3  // C Moter Encoder
#define ENCC_B 49
#define ENCD_A 2  // D Moter Encoder
#define ENCD_B 23
#define INT_MAX 65535
#define COUNT_RESET 200
#define COUNT_OVERFLOW 16374
#define sign(a) (min(1, max(-1, a)))
/*
//Motor Directions
 #define MOTORA_FORWARD(pwm)    do{digitalWrite(DIRA1,LOW); digitalWrite(DIRA2,HIGH);analogWrite(PWMA,pwm);}while(0)
 #define MOTORA_STOP(x)         do{digitalWrite(DIRA1,LOW); digitalWrite(DIRA2,LOW); analogWrite(PWMA,0);}while(0)
 #define MOTORA_BACKOFF(pwm)    do{digitalWrite(DIRA1,HIGH);digitalWrite(DIRA2,LOW); analogWrite(PWMA,pwm);}while(0)
 
 #define MOTORB_FORWARD(pwm)    do{digitalWrite(DIRB1,HIGH); digitalWrite(DIRB2,LOW);analogWrite(PWMB,pwm);}while(0)
 #define MOTORB_STOP(x)         do{digitalWrite(DIRB1,LOW); digitalWrite(DIRB2,LOW); analogWrite(PWMB,0);}while(0)
 #define MOTORB_BACKOFF(pwm)    do{digitalWrite(DIRB1,LOW);digitalWrite(DIRB2,HIGH); analogWrite(PWMB,pwm);}while(0)
 
 #define MOTORC_FORWARD(pwm)    do{digitalWrite(DIRC1,LOW); digitalWrite(DIRC2,HIGH);analogWrite(PWMC,pwm);}while(0)
 #define MOTORC_STOP(x)         do{digitalWrite(DIRC1,LOW); digitalWrite(DIRC2,LOW); analogWrite(PWMC,0);}while(0)
 #define MOTORC_BACKOFF(pwm)    do{digitalWrite(DIRC1,HIGH);digitalWrite(DIRC2,LOW); analogWrite(PWMC,pwm);}while(0)
 
 #define MOTORD_FORWARD(pwm)    do{digitalWrite(DIRD1,HIGH); digitalWrite(DIRD2,LOW);analogWrite(PWMD,pwm);}while(0)
 #define MOTORD_STOP(x)         do{digitalWrite(DIRD1,LOW); digitalWrite(DIRD2,LOW); analogWrite(PWMD,0);}while(0)
 #define MOTORD_BACKOFF(pwm)    do{digitalWrite(DIRD1,LOW);digitalWrite(DIRD2,HIGH); analogWrite(PWMD,pwm);}while(0)
 */
#define COUNTS_TO_METERS (0.6283185 / 360.0)



// Loop Periods
#define ODOM_PERIOD 50 // 20 Hz
#define WATCHDOG_PERIOD 500 // 2 Hz
#define HEALTH_PERIOD 1000 // 1 Hz
#define PID_PERIOD 50 // 20 Hz

// Global Variables
Utils utils;
ros::NodeHandle  nh;
std_msgs::Float32MultiArray enc_msg;
ros::Publisher enc_pub("encoders", &enc_msg);
mecanumbot::RobotHealth health_msg;
ros::Publisher health_pub("robot_health", &health_msg);
unsigned long health_timer = 0;
unsigned long odom_timer = 0;
unsigned long watchdog_timer = 0;
unsigned long PID_timer=0;
boolean hazards_enabled = false;
volatile int EncoderCounts[4];
int lastEncoderCounts[4];
// convert to motor
// TODO: move this into the Mecanum library
#define LINEAR_X_MAX_VEL 1.3 // m/s
#define LINEAR_Y_MAX_VEL 1.3 // m/s
#define ANGULAR_Z_MAX_VEL 2.8 // rad/s
#define DEADZONE 5 // in bytes around 128
#define MAXBITS 255
//PID Variables
double xSetpoint, ySetpoint, thSetpoint;
double xInput, yInput, thInput;
double xOutput, yOutput, thOutput;
double Kp=5,Ki=5, Kd=4;

PID xPID(&xInput, &xOutput, &xSetpoint, Kp, Ki, Kd, DIRECT);
PID yPID(&yInput, &yOutput, &ySetpoint, Kp, Ki, Kd, DIRECT);
PID thPID(&thInput, &thOutput, &thSetpoint, Kp, Ki, Kd, DIRECT);
byte convertToMotor(float value, float maxValue) {
  
  value = constrain(value, -1.0 * maxValue, maxValue);
  byte r = 128;
   //r = map(value, -1.0 * maxValue, maxValue, 0.0, MAXBITS);
 if(value != 128){  
    r=  (value - maxValue*-1) * (MAXBITS - 20) / (maxValue - maxValue*-1) + 20;
 }//if(value >= maxValue) r = 255; // i guess i don't understand how map works

  // enforce deadzone around 128
  /*char STest[8];
dtostrf(r, 6, 2, STest); 
nh.loginfo(STest);*/ 
  if(r >= MAXBITS/2-DEADZONE && r <= MAXBITS/2+DEADZONE) r = MAXBITS/2;
 
  return r;
}

// Mecanum cmd_vel Callback    
void vel_callback(const geometry_msgs::Twist& cmdvel) {
  if(true/*hazards_enabled*/) {
    
/*    controller_cmd_vel(

    convertToMotor(cmdvel.linear.x, LINEAR_X_MAX_VEL),
    convertToMotor(cmdvel.linear.y, LINEAR_Y_MAX_VEL),
    convertToMotor(cmdvel.angular.z, ANGULAR_Z_MAX_VEL)
      
      );*/
     xSetpoint=(double)cmdvel.linear.x*(double)(660.0/(PI*.0762));
     ySetpoint=(double)cmdvel.linear.y*(double)(660.0/(PI*.0762));
     thSetpoint=(double)cmdvel.angular.z*(double)(660.0/(PI*.0762));

  }
  else {
    stopMotors();
  }

  watchdog_timer = millis();
}
ros::Subscriber<geometry_msgs::Twist> vel_sub("cmd_vel", &vel_callback);

// Hazards Enable Callback
void hazards_callback(const mecanumbot::RobotHazardsEnableRequest& req, mecanumbot::RobotHazardsEnableResponse& res) {
  // handle request
  hazards_enabled = req.enable;
  if(!hazards_enabled) {
    stopMotors();
  }

  // generate response
  res.success = true;
}
ros::ServiceServer<mecanumbot::RobotHazardsEnableRequest, mecanumbot::RobotHazardsEnableResponse> hazard_srv("robot_hazards_enable", &hazards_callback);





void stopMotors() {
  motorA.brake();
  motorB.brake();
  motorC.brake();
  motorD.brake();
}

void setup()
{
  
  Serial1.begin(9600);
  // Get baseline CPI utilization
  utils.measureIdleUsage(HEALTH_PERIOD);
  //todo
  //Serial.begin(57600);
  //attachInterrupt(18,doEncoderA,RISING);
  //attachInterrupt(19,doEncoderB,RISING);
  //attachInterrupt(3,doEncoderC,RISING);
  //attachInterrupt(2,doEncoderD,RISING);
  pinMode(ENCA_B,INPUT);
  pinMode(ENCB_B,INPUT);
  pinMode(ENCC_B,INPUT);
  pinMode(ENCD_B,INPUT);
  controller_cmd_vel(MAXBITS/2,MAXBITS/2,MAXBITS/2);

  // ROS Setup
  nh.initNode();
  nh.subscribe(vel_sub);
  nh.advertise(enc_pub);
  nh.advertise(health_pub);
  nh.advertiseService(hazard_srv);
  while(!nh.connected()) nh.spinOnce();
  odom_timer = millis();
  PID_timer= millis();
  enc_msg.data = (float *)malloc(sizeof(float)*4);
  enc_msg.data_length = 4;

  xSetpoint=0;
  ySetpoint=0;
  thSetpoint=0;
  xPID.SetMode(AUTOMATIC);  
  yPID.SetMode(AUTOMATIC);
  thPID.SetMode(AUTOMATIC);
  
  // ready to go! default color state w/ ROS connected
  nh.loginfo("GOFR Controller startup complete");
 


}

void loop()
{
  // auto stop motors if we haven't received a cmd_vel message in a while
  if((millis() - watchdog_timer) > WATCHDOG_PERIOD) {
    stopMotors();
    watchdog_timer = millis();
  }

  // send odom
  if((millis() - PID_timer) > PID_PERIOD) {
    // grab data from encoders
    controller_getEncoderCounts(enc_msg.data); // returned as x, y, theta
    unsigned long currentTime = millis();
    // publish data
    if((millis() - odom_timer) > ODOM_PERIOD) {
    enc_msg.data[3] = (float)(currentTime - odom_timer) / 1000;
    enc_pub.publish(&enc_msg);
    if((currentTime - ODOM_PERIOD) > (odom_timer + ODOM_PERIOD)) {
      odom_timer = currentTime;
    }
    else {
      odom_timer = odom_timer + ODOM_PERIOD;
    }
    
  }
    //if (xSetpoint!=0 || ySetpoint!=0 || thSetpoint!=0){
      xInput=(double)(enc_msg.data[0]/(currentTime-PID_timer)/1000);
      yInput=(double)(enc_msg.data[1]/(currentTime-PID_timer)/1000);
      thInput=(double)(enc_msg.data[2]/(currentTime-PID_timer)/1000);
 
      xPID.Compute();
      yPID.Compute();
      thPID.Compute();
      byte xSpeed = xOutput;//*0.035123966942159+127.5);
      byte ySpeed = yOutput;//*0.035123966942159+127.5);
      byte thSpeed = thOutput;//*0.035123966942159+127.5);
      controller_cmd_vel(xSpeed,ySpeed,thSpeed);
      
    //}
  /*else{
        controller_cmd_vel(MAXBITS/2,MAXBITS/2,MAXBITS/2);

        }*/
              char STest[8];
//  sprintf(STest, "%.02f.", xSetpoint);
 dtostrf(xOutput,7,2,STest);
nh.loginfo(STest);
  if((currentTime - PID_PERIOD) > (PID_timer + PID_PERIOD)) {
        PID_timer = currentTime;
      }
      else {
        PID_timer = PID_timer + PID_PERIOD;
      }
  }
//  if((millis() - odom_timer) > ODOM_PERIOD) {
//    // grab data from encoders
//    controller_getEncoderCounts(enc_msg.data); // returned as x, y, theta
//    unsigned long currentTime = millis();
//    enc_msg.data[3] = (float)(currentTime - odom_timer) / 1000;
//
//    // publish data
//    enc_pub.publish(&enc_msg);
//    if((currentTime - ODOM_PERIOD) > (odom_timer + ODOM_PERIOD)) {
//      odom_timer = currentTime;
//    }
//    else {
//      odom_timer = odom_timer + ODOM_PERIOD;
//    }
//  }

  // health timer
  if((millis() - health_timer) > HEALTH_PERIOD) {
    // microcontroller health
    health_msg.cpu_used = utils.cpuUsage(HEALTH_PERIOD); // percentage
    health_msg.mem_used = utils.memUsage(); // percentage
    health_pub.publish(&health_msg);
    health_timer = millis();
  }
  
    // send / receive ROS messages
  nh.spinOnce();
  // ros likes us to wait at least 1ms between loops - this is accounted for in utils.cpuIdle() below

  // keep track of an idle CPU
  utils.cpuIdle(); // sleeps 1ms  
}  

void controller_cmd_vel(byte x, byte y, byte xy)

{
  /* x=(byte)128;
   y=(byte)255;
   xy=(byte)128;*/
  // normalize
  int x2 = x - MAXBITS/2;
  int y2 = y - MAXBITS/2;
  int xy2 = xy - MAXBITS/2;
  int sum = abs(x2) + abs(y2) + abs(xy2);

  if(sum > (MAXBITS/2)-1) {
    x2 = (x2 * MAXBITS / sum);
    y2 = (y2 * MAXBITS / sum);
    xy2 = (xy2 * MAXBITS / sum);
  }  
  int pwma =x2 - y2 - xy2;
  int pwmb =x2 + y2 + xy2;
  int pwmc =x2 + y2 - xy2;
  int pwmd = x2 - y2 + xy2;
  //DRIVE(pwma,pwmb,pwmc,pwmd);
  //DRIVE (x2 - y2 + 128 - xy2, x2 + y2 + 128 + xy2, x2 + y2 + 128 - xy2, x2 - y2 + 128 + xy2); Changed to match our code
}
void DRIVE (int pwmA, int pwmB, int pwmC, int pwmD) {

  motorA.drive(pwmA);
  motorB.drive(pwmB);
  motorC.drive(pwmC);
  motorD.drive(pwmD);
}

void controller_getEncoderCounts(float *xytCounts)

{

  // get new encoder counts from MD25 boards
  int newEncoderCounts[4];
  newEncoderCounts[0] = EncoderCounts[0]; // M_fl
  newEncoderCounts[1] = EncoderCounts[1]; // M_fr
  newEncoderCounts[2] = EncoderCounts[2]; // M_bl
  newEncoderCounts[3] = EncoderCounts[3]; // M_br	

  // find deltas
  int deltaEncoderCounts[4];
  for(int i = 0; i < 4; i++) {
    //TODO Replace "this" calls
    // check for overflow
    if(abs(lastEncoderCounts[i]) > COUNT_OVERFLOW && abs(newEncoderCounts[i]) > COUNT_OVERFLOW && sign(lastEncoderCounts[i]) != sign(newEncoderCounts[i])) {
     if(sign(lastEncoderCounts[i]) > 0){
     deltaEncoderCounts[i] = newEncoderCounts[i] - lastEncoderCounts[i] + INT_MAX;}
     else{
     deltaEncoderCounts[i] = newEncoderCounts[i] - lastEncoderCounts[i] - INT_MAX;}
     }
     else {deltaEncoderCounts[i] = newEncoderCounts[i] - lastEncoderCounts[i];}		
     
     // check for gross change -> MD25 board power cycled
     if(abs(deltaEncoderCounts[i]) > COUNT_RESET){ deltaEncoderCounts[i] = 0;}
     // no idea what the real value is in this case		
     
     // save encoder counts
     lastEncoderCounts[i] = newEncoderCounts[i]; // i should grow up and just use a point after the for loop
     
  }	

  // convert the motor counts into x, y, theta counts
  xytCounts[0] = (deltaEncoderCounts[0] + deltaEncoderCounts[1] + deltaEncoderCounts[2] + deltaEncoderCounts[3]) / 4;
  xytCounts[1] = (0 - deltaEncoderCounts[0] + deltaEncoderCounts[1] + deltaEncoderCounts[2] - deltaEncoderCounts[3]) / 4;
  xytCounts[2] = (0 - deltaEncoderCounts[0] + deltaEncoderCounts[1] - deltaEncoderCounts[2] + deltaEncoderCounts[3]) / 4;	

  // debug 
  //xytCounts[0] = newEncoderCounts[0];
  //xytCounts[1] = newEncoderCounts[1];
  //xytCounts[2] = newEncoderCounts[2];

}


void doEncoderA(){
  if(digitalRead(ENCA_B)) {
    EncoderCounts[0]++;
  } 
  else {  
    EncoderCounts[0]--;  
  }
}
void doEncoderB(){
  if(digitalRead(ENCB_B)) {
    EncoderCounts[1]++;
  } 
  else {  
    EncoderCounts[1]--;  
  }
}
void doEncoderC(){
  if(digitalRead(ENCC_B)) {
    EncoderCounts[2]++;
  } 
  else {  
    EncoderCounts[2]--;  
  }
}
void doEncoderD(){
  if(digitalRead(ENCD_B)) {
    EncoderCounts[3]++;
  } 
  else {  
    EncoderCounts[3]--;  
  }
}

double PIDCalcs(double inputPulseA,double inputPulseB,double inputPulseC,double inputPulseD, double dt ){
  
}

#include <Servo.h>
#include <Wire.h>

constexpr uint8_t I2C_ADDRESS = 0x12;
constexpr uint8_t IR_RX1_PIN = 2;
constexpr uint8_t IR_TX1_PIN = 3;
constexpr uint8_t IR_RX2_PIN = 4;
constexpr uint8_t MOTOR1_PIN = 5;
constexpr uint8_t MOTOR2_PIN = 6;
constexpr uint8_t SERVO_TOP_PIN = 7;
constexpr uint8_t SERVO_MIDDLE_PIN = 8;
constexpr uint8_t SERVO_EXIT_PIN = 9;
constexpr uint8_t IR_TX2_PIN = 11;
constexpr uint8_t CMD_DISPENSE = 0x01;
constexpr uint8_t CMD_STOP = 0x02;

Servo servoTop;
Servo servoMiddle;
Servo servoExit;
volatile uint8_t pendingCommand = 0;
volatile uint8_t pendingAmount = 0;

void stopOutputs()
{
  analogWrite(MOTOR1_PIN, 0);
  analogWrite(MOTOR2_PIN, 0);
}

void receiveCommand(int byteCount)
{
  if (byteCount < 2) return;
  pendingCommand = Wire.read();
  pendingAmount = Wire.read();
}

void dispense(uint8_t amount)
{
  Serial.print("Lane 3 dispense amount: ");
  Serial.println(amount);
  // TODO: calibrate servo angles, IR counting, and 38 kHz transmitters.
}

void setup()
{
  Serial.begin(9600);
  pinMode(IR_RX1_PIN, INPUT);
  pinMode(IR_RX2_PIN, INPUT);
  pinMode(IR_TX1_PIN, OUTPUT);
  pinMode(IR_TX2_PIN, OUTPUT);
  pinMode(MOTOR1_PIN, OUTPUT);
  pinMode(MOTOR2_PIN, OUTPUT);
  servoTop.attach(SERVO_TOP_PIN);
  servoMiddle.attach(SERVO_MIDDLE_PIN);
  servoExit.attach(SERVO_EXIT_PIN);
  stopOutputs();
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveCommand);
}

void loop()
{
  noInterrupts();
  uint8_t command = pendingCommand;
  uint8_t amount = pendingAmount;
  pendingCommand = 0;
  interrupts();
  if (command == CMD_DISPENSE) dispense(amount);
  else if (command == CMD_STOP) stopOutputs();
}


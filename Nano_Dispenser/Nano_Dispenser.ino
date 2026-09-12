#include <Servo.h>
#include <Wire.h>

namespace {
constexpr uint8_t I2C_ADDRESS = 0x10;
constexpr uint8_t DISPENSER_COUNT = 3;
constexpr uint8_t SERVO_PINS[DISPENSER_COUNT] = {7, 8, 9};

constexpr uint8_t CMD_DISPENSE = 0x01;
constexpr uint8_t CMD_STOP = 0x02;

// Keep motion disabled until the mechanisms are calibrated without pills.
constexpr bool ENABLE_SERVO_MOVEMENT = false;
constexpr int REST_PULSE_US[DISPENSER_COUNT] = {1500, 1500, 1500};
constexpr int RELEASE_PULSE_US[DISPENSER_COUNT] = {1750, 1750, 1750};
constexpr unsigned long MOVE_TIME_MS = 700;

Servo dispenserServos[DISPENSER_COUNT];

volatile uint8_t pendingCommand = 0;
volatile uint8_t pendingDispenser = 0;
volatile uint8_t pendingAmount = 0;

void moveServo(uint8_t index, int pulseWidthUs)
{
  dispenserServos[index].writeMicroseconds(pulseWidthUs);
  delay(MOVE_TIME_MS);
}

void stopAllServos()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
    dispenserServos[index].detach();
}

void dispense(uint8_t dispenser, uint8_t amount)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT || amount == 0)
    return;

  Serial.print("Dispense unit ");
  Serial.print(dispenser);
  Serial.print(" amount ");
  Serial.println(amount);

  if (!ENABLE_SERVO_MOVEMENT)
  {
    Serial.println("Servo movement disabled: calibrate pulse widths first");
    return;
  }

  const uint8_t index = dispenser - 1;
  if (!dispenserServos[index].attached())
    dispenserServos[index].attach(SERVO_PINS[index]);

  for (uint8_t count = 0; count < amount; ++count)
  {
    moveServo(index, RELEASE_PULSE_US[index]);
    moveServo(index, REST_PULSE_US[index]);
  }

  dispenserServos[index].detach();
}

void receiveCommand(int byteCount)
{
  if (byteCount < 3)
  {
    while (Wire.available())
      Wire.read();
    return;
  }

  pendingCommand = Wire.read();
  pendingDispenser = Wire.read();
  pendingAmount = Wire.read();

  while (Wire.available())
    Wire.read();
}
}

void setup()
{
  Serial.begin(9600);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveCommand);

  Serial.println("Nano dispenser ready at I2C address 0x10");
}

void loop()
{
  noInterrupts();
  const uint8_t command = pendingCommand;
  const uint8_t dispenser = pendingDispenser;
  const uint8_t amount = pendingAmount;
  pendingCommand = 0;
  interrupts();

  if (command == CMD_DISPENSE)
    dispense(dispenser, amount);
  else if (command == CMD_STOP)
    stopAllServos();
}

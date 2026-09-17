#pragma once

#include <cassert>
#include <vector>

struct Pulse { int pin; int value; };
extern std::vector<Pulse> pulses;
extern int attachedCount;
extern bool failAttach;

class Servo {
  int pin = -1;
public:
  void setPeriodHertz(int hz) { assert(hz == 50); }
  void attach(int target, int minimum, int maximum)
  {
    assert(minimum < maximum);
    if (!failAttach) { pin = target; ++attachedCount; }
  }
  bool attached() const { return pin >= 0; }
  void writeMicroseconds(int value)
  {
    assert(attached());
    pulses.push_back({pin, value});
  }
  void detach()
  {
    assert(attached());
    pin = -1;
    --attachedCount;
  }
};

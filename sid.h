#pragma once

enum SidRegister
{
    Frequency,
    PulseWidth,
    Control,
    AttackDecay,
    SustainRelease
};

#define CONTROL_GATE            (0x01)
#define CONTROL_SYNCHRONIZE     (0x02) // NOT IMPLEMENTED
#define CONTROL_RING_MODULATION (0x04) // NOT IMPLEMENTED
#define CONTROL_TESTBIT         (0x08) // NOT IMPLEMENTED
#define CONTROL_TRIANGLE        (0x10)
#define CONTROL_SAWTOOTH        (0x20)
#define CONTROL_PULSE           (0x40)
#define CONTROL_NOISE           (0x80)

uint8_t GetNextSample();

void SetSidRegister(uint8_t channelNum, enum SidRegister address, uint8_t value);

uint8_t GetSidRegister(uint8_t channelNum, enum SidRegister address);

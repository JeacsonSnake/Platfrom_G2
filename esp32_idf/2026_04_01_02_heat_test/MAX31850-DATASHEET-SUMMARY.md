# MAX31850/MAX31851 Datasheet Summary

**Source**: `hardware_info/max31850-max31851.pdf` (25 pages)

---

## 1. Device Overview

### MAX31850 (This Project)
- **Thermocouple Type**: K-Type only
- **Family Code**: 0x3B

### MAX31851
- **Thermocouple Types**: J, N, T, S, R, E

---

## 2. K-Type Thermocouple Specifications

| Parameter | Value |
|-----------|-------|
| Temperature Range | -270°C to +1372°C |
| Cold Junction Range | -55°C to +125°C |
| Thermocouple Resolution | 0.25°C (14-bit) |
| Cold Junction Resolution | 0.0625°C (12-bit) |
| Accuracy (-200°C to +700°C) | ±2.0°C |
| Accuracy (-270°C to -200°C) | ±4.0°C |
| Accuracy (+700°C to +1372°C) | ±4.0°C |
| Conversion Time | ~100ms (automatic continuous) |

---

## 3. Memory Map (9-Byte Scratchpad)

| Byte | Content | Description |
|------|---------|-------------|
| 0 | TC Temp LSB + Fault | D7-D4: Temperature LSB, D3-D0: Fault bits |
| 1 | TC Temp MSB | D7-D2: Temperature MSB (14-bit signed) |
| 2 | CJ Temp LSB | Cold junction temperature LSB |
| 3 | CJ Temp MSB | D7-D4: CJ temperature MSB (12-bit signed) |
| 4 | Config Register | AD[3:0]: Hardware address pins |
| 5 | Reserved | Reads FFh |
| 6 | Reserved | Reads FFh |
| 7 | Reserved | Reads FFh |
| 8 | CRC | CRC8 of bytes 0-7 |

### Config Register (Byte 4)
- **Bits 3:0 (AD[3:0])**: Hardware address pin states
  - AD0 (Bit 0): GND=0, DQ/VCC=1
  - AD1 (Bit 1): GND=0, DQ/VCC=1
  - AD2, AD3: Not used in MAX31850
- **Note**: Pins connected through <10kΩ resistor are valid logic levels

### Fault Bits (Byte 0, D3-D0)
- **D0**: Fault (any fault detected)
- **D1**: Thermocouple Open Circuit
- **D2**: Thermocouple Short to GND
- **D3**: Thermocouple Short to VCC

---

## 4. 1-Wire Protocol Commands

| Command | Code | Description |
|---------|------|-------------|
| Search ROM | 0xF0 | Find all devices on bus |
| Read ROM | 0x33 | Read ROM (single device only) |
| Match ROM | 0x55 | Select specific device |
| Skip ROM | 0xCC | Skip ROM (broadcast/single device) |
| Read Scratchpad | 0xBE | Read 9-byte scratchpad |

**Note**: MAX31850 does NOT support:
- Convert T command (0x44) - auto-converts continuously
- Write Scratchpad (0x4E)
- Copy Scratchpad (0x48)
- Recall EEPROM (0xB8)

---

## 5. 64-Bit ROM Code Structure

| Field | Size | Description |
|-------|------|-------------|
| Family Code | 8-bit | 0x3B for MAX31850/MAX31851 |
| Serial Number | 48-bit | Unique device ID |
| CRC | 8-bit | CRC8 of first 56 bits |

**CRC Polynomial**: X8 + X5 + X4 + 1 (0x31)

---

## 6. Hardware Connection Requirements

### Address Pin Configuration
| AD1 | AD0 | Address | PCB Label |
|-----|-----|---------|-----------|
| GND | GND | 0 | U1 (P1) |
| GND | VCC | 1 | U2 (P2) |
| VCC | GND | 2 | U3 (P3) |
| VCC | VCC | 3 | U4 (P4) |

**Note**: Pins must be connected through <10kΩ resistor for valid logic levels.

### Pull-up Resistor
- **Value**: 4.7kΩ (per datasheet, can use 1kΩ to 10kΩ)
- **Connection**: Between DQ and VCC (+3.3V)

---

## 7. Key Features

1. **Cold-Junction Compensation**: Built-in temperature sensor compensates for cold junction
2. **Fault Detection**: Automatic detection of open/short conditions
3. **1-Wire Interface**: Single data line + ground
4. **Parasite Power**: Can operate without local power (not used in this project)
5. **Alarm Search**: Can identify devices with temperature faults

---

**Document Version**: 1.0  
**Date**: 2026-04-01  
**Source**: Maxim Integrated MAX31850/MAX31851 Datasheet

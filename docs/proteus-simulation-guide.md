# Proteus Simulation Guide

## 1. Scope
This guide explains how to build and validate the wireless smartphone charging controller in Proteus using Arduino firmware from `src/wireless_charging_controller/wireless_charging_controller.ino`.

## 2. Required Proteus Components
1. Arduino UNO R3 (or Nano model if available)
2. Virtual Terminal (for serial telemetry)
3. LM35 temperature sensor
4. DC voltage source(s)
5. Potentiometer or variable resistor (to emulate varying receiver voltage)
6. Current sense element model (shunt + amplifier block, or equivalent analog source)
7. N-Channel MOSFET (logic level)
8. LED indicators with resistors
9. Generic resistor/capacitor set
10. Ground and +5V rails

## 3. Firmware Preparation
1. Open the `.ino` in Arduino IDE.
2. Select board: Arduino Uno.
3. Verify/Compile project.
4. Export compiled binary (`Sketch -> Export Compiled Binary`).
5. Note the generated `.hex` path.

## 4. Create New Proteus Project
1. Open Proteus and create a new project in `simulation/`.
2. Name it `wireless_charging_controller`.
3. Skip PCB design; create schematic only.
4. Add all components listed above.

## 5. Wiring Plan
1. Arduino pin A0 <- LM35 output.
2. Arduino pin A1 <- voltage sensor output (scaled 0-5V).
3. Arduino pin A2 <- current sensor output (scaled 0-5V around configured zero level).
4. Arduino pin D9 -> MOSFET gate drive (through gate resistor).
5. Arduino pin D8 -> power stage enable line (or relay control).
6. Arduino pin D13 -> status LED (with series resistor).
7. Arduino pin D12 -> fault LED (with series resistor).
8. Arduino TX (D1) -> Virtual Terminal RX for telemetry capture.
9. Common GND for all modules.

## 6. Sensor Emulation Strategy
1. LM35: Set a temperature profile by varying sensor temperature property.
2. Voltage sense: Use a variable source/potentiometer so the ADC sees 0-5V corresponding to 0-10V or selected divider scaling.
3. Current sense: Use a controlled analog source to emulate current feedback behavior.

## 7. Load and Charging Behavior Emulation
1. Model smartphone battery with RC + controlled voltage source approximation.
2. During test, gradually increase sensed voltage to emulate charging progression.
3. Reduce sensed current in CV phase to emulate taper behavior.

## 8. Simulation Test Cases
1. Nominal cycle:
   - Initial voltage above detect threshold.
   - Verify transitions: IDLE -> PRECHARGE -> CC -> CV -> COMPLETE.
2. Overtemperature fault:
   - Raise LM35 temperature above max threshold.
   - Verify FAULT state and PWM shutdown.
3. Overvoltage fault:
   - Force sensed voltage above maxAbsoluteVoltage.
   - Verify immediate cutoff.
4. Overcurrent fault:
   - Increase sensed current above maxAbsoluteCurrentA.
   - Verify immediate cutoff.
5. Recharge cycle:
   - After COMPLETE, drop sensed voltage below recharge threshold.
   - Verify controller re-enters CC.

## 9. What to Observe
1. Serial telemetry fields:
   - `state`
   - `fault`
   - `temp_C`
   - `volt_V`
   - `curr_A`
   - `pwm`
2. LED behavior:
   - Fault LED ON in FAULT state.
   - Status LED blinking on fault, ON at complete.

## 10. Calibration Notes
1. Tune `voltageDividerFactor` based on your sensor network.
2. Tune `currentZeroVoltage` and `currentSensitivity` to match selected current module.
3. Align threshold constants with your hardware limits and thermal profile.

## 11. Limitations
1. Proteus does not fully model real Qi communication/protocol behavior.
2. Coupling efficiency and EMI effects are simplified.
3. Hardware validation is still required before real deployment.

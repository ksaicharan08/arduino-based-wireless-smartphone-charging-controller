/*
  Arduino Based Wireless Smartphone Charging Controller
  -----------------------------------------------------
  Firmware scope:
  - Reads voltage, current, and temperature sensors
  - Implements staged charging control (Precharge -> CC -> CV -> Complete)
  - Protects against overtemperature, overvoltage, and overcurrent
  - Publishes telemetry to Serial Monitor for monitoring and validation

  Notes:
  - This reference implementation is intended for lab-scale validation.
  - Final smartphone products must comply with Qi protocol, EMC, and safety standards.
*/

#include "control_core.h"

// ------------------------------ Pin Mapping ------------------------------
const uint8_t PIN_TEMP_LM35 = A0;      // LM35 analog output
const uint8_t PIN_VOLT_SENSE = A1;     // Resistive divider output
const uint8_t PIN_CURR_SENSE = A2;     // ACS712 analog output

const uint8_t PIN_PWM_DRIVE = 9;       // PWM gate drive for power stage control
const uint8_t PIN_EN_POWER = 8;        // Enable pin for transmitter power stage

const uint8_t PIN_LED_STATUS = 13;
const uint8_t PIN_LED_FAULT = 12;

// ------------------------------ Charging Model ------------------------------
enum ChargerState {
  STATE_IDLE = 0,
  STATE_PRECHARGE,
  STATE_CC,
  STATE_CV,
  STATE_COMPLETE,
  STATE_FAULT
};

enum FaultCode {
  FAULT_NONE = 0,
  FAULT_OVERTEMP,
  FAULT_OVERVOLT,
  FAULT_OVERCURRENT,
  FAULT_SENSOR_INVALID,
  FAULT_TIMEOUT
};

struct ChargingConfig {
  // ADC and sensor scaling
  float adcRefVoltage = 5.0f;
  float adcMaxCount = 1023.0f;

  // Voltage divider (example: 100k top, 20k bottom => factor = 6.0)
  float voltageDividerFactor = 6.0f;

  // ACS712 current sensor parameters
  float currentZeroVoltage = 2.50f;
  float currentSensitivity = 0.100f; // V/A for ACS712-20A module

  // Safety thresholds
  float maxTemperatureC = 45.0f;
  float resumeTemperatureC = 40.0f;
  float maxAbsoluteVoltage = 4.30f;
  float maxAbsoluteCurrentA = 2.20f;

  // Charging profile (single-cell equivalent model)
  float batteryDetectMinV = 2.50f;
  float prechargeToCCVoltage = 3.20f;
  float ccToCVVoltage = 4.15f;
  float cvTargetVoltage = 4.20f;
  float ccTargetCurrentA = 1.20f;
  float prechargeCurrentA = 0.50f;
  float terminateCurrentA = 0.15f;
  float rechargeVoltage = 4.00f;

  // Timing
  uint32_t telemetryPeriodMs = 500;
  uint32_t controlPeriodMs = 100;
  uint32_t maxChargeTimeMs = 3UL * 60UL * 60UL * 1000UL; // 3 hours
  uint32_t terminationHoldMs = 30000;
};

ChargingConfig cfg;

// ------------------------------ Runtime Variables ------------------------------
ChargerState chargerState = STATE_IDLE;
FaultCode faultCode = FAULT_NONE;

float filtTempC = 25.0f;
float filtVoltageV = 0.0f;
float filtCurrentA = 0.0f;

uint8_t pwmDuty = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastControlMs = 0;
uint32_t chargeStartMs = 0;
uint32_t terminateStartMs = 0;

// ------------------------------ Utility Functions ------------------------------
const char* stateToString(ChargerState s) {
  switch (s) {
    case STATE_IDLE: return "IDLE";
    case STATE_PRECHARGE: return "PRECHARGE";
    case STATE_CC: return "CONSTANT_CURRENT";
    case STATE_CV: return "CONSTANT_VOLTAGE";
    case STATE_COMPLETE: return "COMPLETE";
    case STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
  }
}

const char* faultToString(FaultCode f) {
  switch (f) {
    case FAULT_NONE: return "NONE";
    case FAULT_OVERTEMP: return "OVERTEMP";
    case FAULT_OVERVOLT: return "OVERVOLT";
    case FAULT_OVERCURRENT: return "OVERCURRENT";
    case FAULT_SENSOR_INVALID: return "SENSOR_INVALID";
    case FAULT_TIMEOUT: return "CHARGE_TIMEOUT";
    default: return "UNKNOWN_FAULT";
  }
}

void setPowerStage(bool enable) {
  digitalWrite(PIN_EN_POWER, enable ? HIGH : LOW);
}

void setPwmDuty(uint8_t duty) {
  pwmDuty = duty;
  analogWrite(PIN_PWM_DRIVE, pwmDuty);
}

void disableChargingOutput() {
  setPwmDuty(0);
  setPowerStage(false);
}

void enterFault(FaultCode code) {
  faultCode = code;
  chargerState = STATE_FAULT;
  disableChargingOutput();
}

// ------------------------------ Sensor Acquisition ------------------------------
float readTemperatureC() {
  int raw = analogRead(PIN_TEMP_LM35);
  float voltage = (raw * cfg.adcRefVoltage) / cfg.adcMaxCount;
  // LM35 slope: 10mV/degC => 100 degC/V
  return voltage * 100.0f;
}

float readVoltageV() {
  int raw = analogRead(PIN_VOLT_SENSE);
  float sensed = (raw * cfg.adcRefVoltage) / cfg.adcMaxCount;
  return sensed * cfg.voltageDividerFactor;
}

float readCurrentA() {
  int raw = analogRead(PIN_CURR_SENSE);
  float sensorV = (raw * cfg.adcRefVoltage) / cfg.adcMaxCount;
  float current = (sensorV - cfg.currentZeroVoltage) / cfg.currentSensitivity;
  if (current < 0.0f) {
    current = 0.0f;
  }
  return current;
}

bool sensorsAreValid(float tC, float v, float iA) {
  // Wide sanity window to catch disconnected sensors and ADC faults.
  bool tempOk = (tC > -10.0f && tC < 120.0f);
  bool voltOk = (v >= 0.0f && v < 10.0f);
  bool currOk = (iA >= 0.0f && iA < 10.0f);
  return tempOk && voltOk && currOk;
}

void updateSensors() {
  float tempNow = readTemperatureC();
  float voltNow = readVoltageV();
  float currNow = readCurrentA();

  filtTempC = cc_low_pass(filtTempC, tempNow, 0.2f);
  filtVoltageV = cc_low_pass(filtVoltageV, voltNow, 0.2f);
  filtCurrentA = cc_low_pass(filtCurrentA, currNow, 0.2f);

  if (!sensorsAreValid(filtTempC, filtVoltageV, filtCurrentA)) {
    enterFault(FAULT_SENSOR_INVALID);
  }
}

// ------------------------------ Control Helpers ------------------------------
void regulateCurrent(float targetCurrentA) {
  // Simple incremental controller for robust 8-bit MCU operation.
  float err = targetCurrentA - filtCurrentA;

  pwmDuty = cc_adjust_pwm_step(pwmDuty, err, 0.08f, -0.08f, 2, 2, 0, 250);

  setPowerStage(true);
  setPwmDuty(pwmDuty);
}

void regulateVoltage(float targetVoltageV) {
  float err = targetVoltageV - filtVoltageV;

  pwmDuty = cc_adjust_pwm_step(pwmDuty, err, 0.02f, -0.02f, 1, 1, 0, 250);

  setPowerStage(true);
  setPwmDuty(pwmDuty);
}

bool checkProtectionTrip() {
  if (filtTempC >= cfg.maxTemperatureC) {
    enterFault(FAULT_OVERTEMP);
    return true;
  }

  if (filtVoltageV >= cfg.maxAbsoluteVoltage) {
    enterFault(FAULT_OVERVOLT);
    return true;
  }

  if (filtCurrentA >= cfg.maxAbsoluteCurrentA) {
    enterFault(FAULT_OVERCURRENT);
    return true;
  }

  if (chargerState != STATE_IDLE && chargerState != STATE_COMPLETE) {
    if (millis() - chargeStartMs > cfg.maxChargeTimeMs) {
      enterFault(FAULT_TIMEOUT);
      return true;
    }
  }

  return false;
}

// ------------------------------ Main State Machine ------------------------------
void processChargingState() {
  switch (chargerState) {
    case STATE_IDLE:
      disableChargingOutput();
      if (filtVoltageV >= cfg.batteryDetectMinV) {
        chargerState = STATE_PRECHARGE;
        chargeStartMs = millis();
      }
      break;

    case STATE_PRECHARGE:
      regulateCurrent(cfg.prechargeCurrentA);
      if (filtVoltageV >= cfg.prechargeToCCVoltage) {
        chargerState = STATE_CC;
      }
      break;

    case STATE_CC:
      regulateCurrent(cfg.ccTargetCurrentA);
      if (filtVoltageV >= cfg.ccToCVVoltage) {
        chargerState = STATE_CV;
      }
      break;

    case STATE_CV:
      regulateVoltage(cfg.cvTargetVoltage);

      if (filtCurrentA <= cfg.terminateCurrentA) {
        if (terminateStartMs == 0) {
          terminateStartMs = millis();
        }

        if (millis() - terminateStartMs >= cfg.terminationHoldMs) {
          chargerState = STATE_COMPLETE;
          terminateStartMs = 0;
        }
      } else {
        terminateStartMs = 0;
      }
      break;

    case STATE_COMPLETE:
      disableChargingOutput();
      // Auto top-up if voltage drops due to load/idle losses.
      if (filtVoltageV <= cfg.rechargeVoltage) {
        chargerState = STATE_CC;
        chargeStartMs = millis();
      }
      break;

    case STATE_FAULT:
      disableChargingOutput();
      // Auto recovery for overtemperature after cooldown.
      if (faultCode == FAULT_OVERTEMP && filtTempC <= cfg.resumeTemperatureC) {
        faultCode = FAULT_NONE;
        chargerState = STATE_IDLE;
      }
      break;

    default:
      enterFault(FAULT_SENSOR_INVALID);
      break;
  }
}

void updateIndicators() {
  bool fault = (chargerState == STATE_FAULT);
  digitalWrite(PIN_LED_FAULT, fault ? HIGH : LOW);

  if (fault) {
    digitalWrite(PIN_LED_STATUS, (millis() / 200) % 2);
  } else {
    digitalWrite(PIN_LED_STATUS, (chargerState == STATE_COMPLETE) ? HIGH : LOW);
  }
}

void publishTelemetry() {
  Serial.print("t_ms=");
  Serial.print(millis());
  Serial.print(",state=");
  Serial.print(stateToString(chargerState));
  Serial.print(",fault=");
  Serial.print(faultToString(faultCode));
  Serial.print(",temp_C=");
  Serial.print(filtTempC, 2);
  Serial.print(",volt_V=");
  Serial.print(filtVoltageV, 3);
  Serial.print(",curr_A=");
  Serial.print(filtCurrentA, 3);
  Serial.print(",pwm=");
  Serial.println(pwmDuty);
}

// ------------------------------ Arduino Lifecycle ------------------------------
void setup() {
  pinMode(PIN_EN_POWER, OUTPUT);
  pinMode(PIN_PWM_DRIVE, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);

  disableChargingOutput();
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_LED_FAULT, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println("Wireless Charging Controller Boot");
}

void loop() {
  uint32_t now = millis();

  if (now - lastControlMs >= cfg.controlPeriodMs) {
    lastControlMs = now;

    updateSensors();

    if (chargerState != STATE_FAULT) {
      checkProtectionTrip();
    }

    processChargingState();
    updateIndicators();
  }

  if (now - lastTelemetryMs >= cfg.telemetryPeriodMs) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}

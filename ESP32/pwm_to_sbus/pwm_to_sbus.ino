// ====== INPUT PINS (PWM from AR610x) ======
#define THRO 34
#define AILE 35
#define ELEV 32
#define RUDD 33
#define GEAR 25
#define AUX1 26

// ====== OUTPUT PIN (S-BUS to Pixhawk RCIN) ======
#define SBUS_TX_PIN 4

#define CHANNELS 6

const int pwmPins[CHANNELS] = {THRO, AILE, ELEV, RUDD, GEAR, AUX1};
volatile uint32_t pulseStart[CHANNELS];
volatile uint16_t pwmValues[CHANNELS] = {1500, 1500, 1500, 1500, 1500, 1500};

// ====== PWM INPUT ISRs ======
void IRAM_ATTR handlePWM(int ch) {
    if (digitalRead(pwmPins[ch]) == HIGH) {
        pulseStart[ch] = micros();
    } else {
        uint32_t width = micros() - pulseStart[ch];
        if (width >= 900 && width <= 2100) {   // sanity filter
            pwmValues[ch] = width;
        }
    }
}

// Separate wrappers because attachInterrupt requires a parameterless function
void IRAM_ATTR pwmISR0(){ handlePWM(0); }
void IRAM_ATTR pwmISR1(){ handlePWM(1); }
void IRAM_ATTR pwmISR2(){ handlePWM(2); }
void IRAM_ATTR pwmISR3(){ handlePWM(3); }
void IRAM_ATTR pwmISR4(){ handlePWM(4); }
void IRAM_ATTR pwmISR5(){ handlePWM(5); }

// ====== SETUP ======
void setup() {
    Serial.begin(115200); // For USB debugging

    // 1. Initialize PWM Input pins and attach interrupts
    for (int i = 0; i < CHANNELS; i++) {
        pinMode(pwmPins[i], INPUT);
    }
    
    attachInterrupt(digitalPinToInterrupt(THRO), pwmISR0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AILE), pwmISR1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ELEV), pwmISR2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RUDD), pwmISR3, CHANGE);
    attachInterrupt(digitalPinToInterrupt(GEAR), pwmISR4, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AUX1), pwmISR5, CHANGE);

    // 2. Initialize Hardware Serial 2 for S-BUS
    // Protocol: 100,000 baud, 8 data bits, Even parity, 2 stop bits.
    // The final 'true' tells the ESP32 to invert the logic signals (0V = HIGH),
    // which eliminates the need for an external hardware inverter.
    // RX is mapped to pin 16 (unused here), TX is mapped to 17.
    Serial2.begin(100000, SERIAL_8E2, 16, SBUS_TX_PIN, true);
}

// ====== MAIN LOOP ======
void loop() {
    static uint32_t lastSbusTime = 0;
    static uint32_t lastDebugTime = 0;

    // Send an S-BUS packet every 10 milliseconds (100Hz frame rate)
    if (millis() - lastSbusTime >= 10) {
        lastSbusTime = millis();

        uint8_t sbusPacket[25];
        memset(sbusPacket, 0, 25); // Zero out the buffer

        sbusPacket[0] = 0x0F; // Standard S-BUS Start Byte

        // A. Map our 6 PWM channels to the S-BUS 11-bit range
        // Standard S-BUS translates 1000us-2000us to ~172-1811
        uint16_t sbusData[16];
        for (int i = 0; i < 16; i++) {
            if (i < CHANNELS) {
                // Map active channels and constrain to prevent 11-bit overflow
                sbusData[i] = constrain(map(pwmValues[i], 1000, 2000, 172, 1811), 0, 2047);
            } else {
                // Set unused channels 7 through 16 to neutral center (992)
                sbusData[i] = 992; 
            }
        }

        // B. Pack the 16 channels (11 bits each) seamlessly into 22 bytes
        int byteIdx = 1;
        int bitIdx = 0;
        for (int i = 0; i < 16; i++) {
            uint16_t chValue = sbusData[i] & 0x07FF; // Mask to 11 bits
            for (int b = 0; b < 11; b++) {
                if (chValue & (1 << b)) {
                    sbusPacket[byteIdx] |= (1 << bitIdx);
                }
                bitIdx++;
                if (bitIdx >= 8) {
                    bitIdx = 0;
                    byteIdx++;
                }
            }
        }

        // C. Finalize packet with Flags and Stop Byte
        sbusPacket[23] = 0x00; // Flags (Frame Lost, Failsafe, etc - all 0 for now)
        sbusPacket[24] = 0x00; // Standard S-BUS Stop Byte

        // D. Transmit via UART2
        Serial2.write(sbusPacket, 25);
    }

    // Print debug values to the USB Serial monitor twice a second
    if (millis() - lastDebugTime >= 500) {
        lastDebugTime = millis();
        Serial.printf("PWM -> T:%d A:%d E:%d R:%d G:%d X:%d\n",
                      pwmValues[0], pwmValues[1], pwmValues[2], 
                      pwmValues[3], pwmValues[4], pwmValues[5]);
    }
}
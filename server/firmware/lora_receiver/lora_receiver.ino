// LoRa Receiver — bridges DX-LR22-433T22D ↔ USB CDC ACM
//
// Wiring:
//   LoRa VCC  → Leonardo 5V
//   LoRa GND  → Leonardo GND
//   LoRa TXD  → Leonardo Pin 0 (RX / Serial1)
//   LoRa RXD  → Leonardo Pin 1 (TX / Serial1)
//
// DX-LR22 defaults: MODE0 (transparent), 9600 8N1, CHANNEL00, LEVEL2

#define LORA Serial1
#define BAUD_LORA 9600
#define BAUD_USB  115200
#define LED 13

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  Serial.begin(BAUD_USB);
  while (!Serial);

  LORA.begin(BAUD_LORA);

  Serial.println(F("[lora-receiver] ready"));
  Serial.print(F("[lora-receiver] LoRa baud="));
  Serial.println(BAUD_LORA);
}

void loop() {
  bool rx = false;

  // LoRa → USB
  while (LORA.available()) {
    Serial.write(LORA.read());
    rx = true;
  }

  // USB → LoRa (pass-through for AT config or test sends)
  while (Serial.available()) {
    LORA.write(Serial.read());
    rx = true;
  }

  if (rx) {
    digitalWrite(LED, HIGH);
    delay(50);
    digitalWrite(LED, LOW);
  }
}

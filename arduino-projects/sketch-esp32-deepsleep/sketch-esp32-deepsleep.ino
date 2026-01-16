
#define uS_TO_S_FACTOR 1000000ULL   // Microseconds to seconds
#define TIME_TO_SLEEP 60            // Deep sleep duration in seconds
#define LED_PIN 8                   // LED connected to GPIO 8

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);

  // Blink LED (on-board LED works with inverted logic)
  digitalWrite(LED_PIN, LOW);
  delay(5000);
  digitalWrite(LED_PIN, HIGH);

  delay(15000);

  // Set deep sleep timer
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  Serial.println("sleep enabled1");

  // Enter deep sleep
  esp_deep_sleep_start();

    Serial.println("sleep enabled2");
}

void loop() {
  
}
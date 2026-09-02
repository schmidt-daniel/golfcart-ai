/*
 * Golf Cart Joystick Interface — Arduino Uno R3
 *
 * Reads a 2-axis analog joystick (X, Y) and a push button, and sends the
 * values over USB serial to the Raspberry Pi 5.
 *
 * Wiring:
 *   - Joystick X wiper  -> A0
 *   - Joystick Y wiper  -> A1
 *   - Push button       -> D2 (other side to GND, internal pull-up)
 *   - Arduino USB       -> Raspberry Pi 5 USB
 *
 * Serial protocol (one line per sample, newline-terminated):
 *   x:<0-1023>,y:<0-1023>,btn:<0|1>
 *
 * The button is used for safety (enable/stop). It is sent as a raw state;
 * the ROS 2 node detects press edges and calls the safety services.
 */

const int PIN_X = A0;
const int PIN_Y = A1;
const int PIN_BTN = 2;

const unsigned long SAMPLE_INTERVAL_MS = 20;  // 50 Hz

void setup()
{
  pinMode(PIN_X, INPUT);
  pinMode(PIN_Y, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  Serial.begin(115200);
}

void loop()
{
  static unsigned long last_sample = 0;
  const unsigned long now = millis();
  if (now - last_sample >= SAMPLE_INTERVAL_MS) {
    last_sample = now;

    const int x = analogRead(PIN_X);
    const int y = analogRead(PIN_Y);
    // Button is active-low (INPUT_PULLUP): pressed = LOW.
    const int btn = (digitalRead(PIN_BTN) == LOW) ? 1 : 0;

    Serial.print("x:");
    Serial.print(x);
    Serial.print(",y:");
    Serial.print(y);
    Serial.print(",btn:");
    Serial.println(btn);
  }
}
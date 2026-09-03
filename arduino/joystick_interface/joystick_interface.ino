/*
 * Golf Cart Joystick Interface — Arduino Uno R3
 *
 * Reads a 2-axis analog joystick (X, Y), a push button (for HMI), and a
 * dedicated safety arm switch, and sends the values over USB serial to the
 * Raspberry Pi 5.
 *
 * Wiring:
 *   - Joystick X wiper  -> A0
 *   - Joystick Y wiper  -> A1
 *   - Joystick button   -> D2 (other side to GND, internal pull-up) [HMI]
 *   - Safety arm switch -> D3 (other side to GND, internal pull-up)
 *   - Arduino USB       -> Raspberry Pi 5 USB
 *
 * Serial protocol (one line per sample, newline-terminated):
 *   x:<0-1023>,y:<0-1023>,btn:<0|1>,safety:<0|1>
 *
 * The joystick button is used for HMI navigation (short/double/long press).
 * The safety arm switch controls arm/disarm; the ROS 2 node detects its state
 * and calls the safety services.
 */

const int PIN_X = A0;
const int PIN_Y = A1;
const int PIN_BTN = 2;      // joystick button (HMI)
const int PIN_SAFETY = 3;   // dedicated safety arm switch

const unsigned long SAMPLE_INTERVAL_MS = 20;  // 50 Hz

void setup()
{
  pinMode(PIN_X, INPUT);
  pinMode(PIN_Y, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_SAFETY, INPUT_PULLUP);
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
    // Buttons are active-low (INPUT_PULLUP): pressed = LOW.
    const int btn = (digitalRead(PIN_BTN) == LOW) ? 1 : 0;
    // Safety switch: active-low. 1 = armed, 0 = disarmed.
    const int safety = (digitalRead(PIN_SAFETY) == LOW) ? 1 : 0;

    Serial.print("x:");
    Serial.print(x);
    Serial.print(",y:");
    Serial.print(y);
    Serial.print(",btn:");
    Serial.print(btn);
    Serial.print(",safety:");
    Serial.println(safety);
  }
}
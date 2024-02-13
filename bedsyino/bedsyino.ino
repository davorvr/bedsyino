/* TODO: Update this block comment for Bedsyino v1.0 (this refers to the prototype)
 * 
 * This code intends to provide a flexible triggering device for synchronising multiple devices
 * that need to operate concurrently. Currently, it outputs two different kinds of signals:
 *      - square wave to trigger many Basler cameras (1 pulse = 1 frame)
 *      - active-low signal for the DIN input of a single AviSoft UltraSound Gate
 *        (can be used to trigger all connected ultrasonic microphones simultaneously)
 * Block diagram:
 *      ________
 *     |        |      _________
 *     |        |     |         |
 *     | Teensy |---->|         |----->Cam 1 connector
 *     |  4.0   |     |         |----->Cam 2 connector
 *     |        |     | 74HC244 |----->...
 *     |        |     |         |----->USG connector
 *     |________|     |_________|
 *
 * The 74HC244 serves as a line buffer. It's powered by the USB 5V line, and pulls the signal
 * lines of the devices high or low based on input from the Teensy. It also gets two extra "ENABLE"
 * inputs from the Teensy - turning these HIGH switches the 244's pins from active high or low
 * to high impedance (meaning it will stop actively sending a signal). This is useless for now,
 * but might come in handy some time in the future. For more info, check the 74HC244's datasheet.
 *
 * The device accepts "s" to start and "x" to stop via USB serial. It also automatically stops
 * for a user-defined period of time, and restarts, to enable rollover of logs into a new file.
 * When any of these events happens, a message is send with a machine-scannable code in square brackets.
 * 
 * /TODO
 */
#define PIN_USG_OUT 6
#define PIN_BASLER_OUT 9
#define PIN_ENABLE_1 35
#define PIN_ENABLE_2 34

/* USER CONFIG */
// in milliseconds
const unsigned long rollover_period = 60 * 1000;
const unsigned long rollover_delay = 1000;
bool running = false;
/* END USER CONFIG */

char serial_input;

/*
 * Since the cameras need a square wave signal, PWM (analogWrite) is used to produce it. However,
 * when a PWM signal turns on, it starts at a random point in the cycle. Therefore, it is best
 * to use the same PWM peripheral for all devices - see Teensy docs. All pins connected to the
 * same interal PWM peripheral will share frequencies and be synchronised in phase regardless of
 * the duty cycle, so for a continuous signal, we just set the USG cycle to always on (which is
 * 4096 for 12-bit PWM - 2^12 = 4096). See also: https://docs.arduino.cc/learn/microcontrollers/analog-output
 */
// Turn the signals on
void signals_on() {
  analogWrite(PIN_USG_OUT, 4096); // 4096 is always on
  // 1024 is 1024/4096=25% duty cycle - it just needs to stay high
  // for long enough, and then low for long enough for the camera
  // to recognise it. 25% for freqs like 30-50 Hz/FPS is just fine.
  analogWrite(PIN_BASLER_OUT, 1024);
  //digitalWrite(PIN_ENABLE_1, LOW);
  //digitalWrite(PIN_ENABLE_2, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
}

void signals_off() {
  analogWrite(PIN_USG_OUT, 0);
  analogWrite(PIN_BASLER_OUT, 0);
  //digitalWrite(PIN_ENABLE_1, HIGH);
  //digitalWrite(PIN_ENABLE_2, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
}

// function to report time until rollover upon request from the serial port (command: "?")
void report_state(unsigned long timer, unsigned long timeout) {
  if (running) {
    Serial.printf("Running. Timeout in: %lu s. Timeout period: %lu s\n", (timeout - timer) / 1000, timeout / 1000);
  } else {
    Serial.println("Not running.");
  }
}

void outputs_enable() {
  digitalWrite(PIN_ENABLE_1, LOW);
  digitalWrite(PIN_ENABLE_2, HIGH);
}

void outputs_disable() {
  digitalWrite(PIN_ENABLE_1, HIGH);
  digitalWrite(PIN_ENABLE_2, LOW);
}

void setup() {
  // initialize ENABLE pins and use them to disable the 241's output
  pinMode(PIN_ENABLE_1, OUTPUT);
  pinMode(PIN_ENABLE_2, OUTPUT);
  outputs_disable();

  /* set up the PWM internal peripheral */
  // 12 bits resolution - PWM values from 0 (always off) to
  // 4096 (always on). values in between set the duty cycle
  analogWriteResolution(12); 
  // set 30 Hz frequency. this is important for the camera, but in reality,
  // the USG_OUT will just be always on or always off.
  // we still use analogWrite for USG_OUT so it's synchronised with BASLER_OUT
  analogWriteFrequency(PIN_USG_OUT, 30);
  analogWriteFrequency(PIN_BASLER_OUT, 30);
  // initialize the LED pin on the Teensy board
  pinMode(LED_BUILTIN, OUTPUT);

  // enable the 241's output now that everything is set up
  outputs_enable();

  // start with signals on or off based on the initial setting of the "running" flag
  if (running) signals_on();
  else signals_off();
}

void loop() {
  // initialise the timer for the rollover period. for more info on how it's used,
  // see here: https://www.pjrc.com/teensy/td_timing_elaspedMillis.html (Teensy-specific)
  elapsedMillis waiting;
  // if we're not running, or if we're waiting for the rollover period to elapse
  // (this is most of the time), we wait for a command via serial.
  while ((waiting < rollover_period) || (!running)) {
    // we wait for a command via serial
    if (Serial.available()) {
      serial_input = Serial.read();
      while (Serial.available()) Serial.read();
      // "s" command - START
      if (serial_input == 's') {
        signals_on();
        Serial.println("[START] Signals on. Resetting the timer.");
        running = true;
        waiting = 0; // timer reset
      }
      // "x" command - STOP completely and permanently
      if (serial_input == 'x') {
        signals_off();
        Serial.println("[STOP_PERMANENT] Signals off.");
        running = false;
      }
      // "?" command - get timing status while the device is running
      if (serial_input == '?') {
        report_state(waiting, rollover_period);
      }
    }
    delay(1);
  }
  // this part executes only in between rollovers:
  waiting = 0; // timer reset
  signals_off(); // turn off signals
  Serial.println("[STOP_ROLLOVER] Turning signals off for 1 second..."); // inform the user
  // wait a bit until we restart so the user has time to do whatever in between rollovers
  while (waiting < rollover_delay);
  // finally, start the signal and inform the user
  signals_on(); 
  Serial.println("[START] Signals on. (automatic)");
}

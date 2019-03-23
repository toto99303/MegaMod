#include <Arduino.h>

volatile static byte pad_status;
         int i = 0;

#define ARDUINO328

#define RESET_IN_PIN A3
#define RESET_OUT_PIN A2
#define VIDEOMODE_PIN 8 //A4
#define LANGUAGE_PIN MISO //A5
#define MODE_LED_R_PIN 9
#define MODE_LED_G_PIN 10
#define MODE_LED_B_PIN 11
#define AUDIO_MODE_PIN 7
#define PAD_LED_PIN LED_BUILTIN
#define ENABLE_SERIAL_DEBUG
//#define FORCE_RESET_ACTIVE_LEVEL 0


enum PadButton {
  MD_BTN_START = 1 << 7,
  MD_BTN_A =     1 << 6,
  MD_BTN_C =     1 << 5,
  MD_BTN_B =     1 << 4,
  MD_BTN_RIGHT = 1 << 3,
  MD_BTN_LEFT =  1 << 2,
  MD_BTN_DOWN =  1 << 1,
  MD_BTN_UP =    1 << 0
};

/* Button combo that enables the other combos
 *
 * Note: That vertical bar ("pipe") means that the buttons must be pressed
 *       together.
 */

#define TRIGGER_COMBO (MD_BTN_START | MD_BTN_B)
//#define TRIGGER_COMBO (MD_BTN_START)

#define TRIGGER_COMBO2 (MD_BTN_START | MD_BTN_A) //additional combo

/* Button combos to perform other actions. These are to be considered in
 * addition to TRIGGER_COMBO.
 *
 * Note that we cannot detect certain buttons on some platforms
 */
#define RESET_COMBO (MD_BTN_A)



#if defined ARDUINO328

  #define AUDIO_COMBO MD_BTN_UP
  #define EUR_COMBO MD_BTN_DOWN
  #define USA_COMBO MD_BTN_RIGHT
  #define JAP_COMBO MD_BTN_LEFT
  #define COMBO_50 MD_BTN_LEFT
  #define COMBO_60 MD_BTN_RIGHT
#endif

#define MODE_ROM_OFFSET 42
#define AUDIO_ROM_OFFSET 50

// Time to wait after mode change before saving the new mode (milliseconds)
#define MODE_SAVE_DELAY 2000L

// Force the reset line level when active. Undefine to enable auto-detection.
//#define FORCE_RESET_ACTIVE_LEVEL LOW

/* Colors to use to indicate the video mode, in 8-bit RGB componentes. You can
 * use any value here if your led is connected to PWM-capable pins, otherwise
 * values specified here will be interpreted as either fully off (if 0) or fully
 * on (if anything else).
 *
 * Note that using PWM-values here sometimes causes unpredictable problems. This
 * happened to me on an ATtiny861, and it's probably due to how pins and timers
 * interact. It seems to work fine on a full Arduino, but unless you really want
 * weird colors, use only 0x00 and 0xFF.
 */

#define MODE_LED_EUR_COLOR {0x00, 0xFF, 0x00}  // Green
#define MODE_LED_USA_COLOR {0x00, 0x00, 0xFF}  // Blue
#define MODE_LED_JAP_COLOR {0xFF, 0x00, 0x00}  // Red
#define MODE_VID_MOD_COLOR {0xFF, 0xFF, 0x00}  // Yellow


// Define this if your led is common-anode, comment out for common-cathode
#define MODE_LED_COMMON_ANODE



/* Presses of the reset button longer than this amount of milliseconds will
 * switch to the next mode, shorter presses will reset the console.
 */
#define LONGPRESS_LEN 700

// Debounce duration for the reset button
#define DEBOUNCE_MS 20

// Duration of the reset pulse (milliseconds)
#define RESET_LEN 350

/*******************************************************************************
 * END OF SETTINGS
 ******************************************************************************/


#ifdef ENABLE_SERIAL_DEBUG
  #define debug(...) Serial.print (__VA_ARGS__)
  #define debugln(...) Serial.println (__VA_ARGS__)
#else
  #define debug(...)
  #define debugln(...)
#endif

#ifdef MODE_ROM_OFFSET
  #include <EEPROM.h>
#endif

enum VideoMode {
  EUR,
  USA,
  JAP,
  MODES_NO, // End of modes
  VID50,
  VID60
};

// This will be handy
#if (defined MODE_LED_R_PIN || defined MODE_LED_G_PIN || defined MODE_LED_B_PIN)
  #define ENABLE_MODE_LED_RGB

byte mode_led_colors[][MODES_NO] = {
  MODE_LED_EUR_COLOR,
  MODE_LED_USA_COLOR,
  MODE_LED_JAP_COLOR,
  MODE_VID_MOD_COLOR
};
#endif

void set_mode (VideoMode m);

VideoMode current_mode;
bool audio_mode;
long mode_last_changed_time;
long audio_mode_last_changed_time;

// Reset level when NOT ACTIVE
byte reset_inactive_level;

void save_mode () {
#ifdef MODE_ROM_OFFSET
  if (mode_last_changed_time > 0 && millis () - mode_last_changed_time >= MODE_SAVE_DELAY) {
    debug ("Saving video mode to EEPROM: ");
    debugln (current_mode);
    byte saved_mode = EEPROM.read (MODE_ROM_OFFSET);
    if (current_mode != saved_mode) {
      EEPROM.write (MODE_ROM_OFFSET, static_cast<byte> (current_mode));
    } else {
      debugln ("Mode unchanged, not saving");
    }
    mode_last_changed_time = 0;    // Don't save again

    // Blink led to tell the user that mode was saved
	set_RGB_leds(0, 0, 0);

    // Keep off for a bit
    delay (200);
  
    // Turn led back on
    update_mode_leds ();

  }
#endif  // MODE_ROM_OFFSET
}

void save_audio() {
#ifdef AUDIO_ROM_OFFSET
	if (audio_mode_last_changed_time > 0 && millis() - audio_mode_last_changed_time >= MODE_SAVE_DELAY) {
		debug("Saving audio mode to EEPROM: ");
		debugln(audio_mode);
		EEPROM.write(AUDIO_ROM_OFFSET, static_cast<byte> (audio_mode));

		audio_mode_last_changed_time = 0;    // Don't save again

		// Blink led to tell the user that mode was saved
		set_RGB_leds(0, 0, 0);

		// Keep off for a bit
		delay(200);

		// Turn led back on
		update_mode_leds();

}
#endif  // AUDIO_ROM_OFFSET
}


void change_mode (int increment) {
  // This also loops in [0, MODES_NO) backwards
  VideoMode new_mode = static_cast<VideoMode> ((current_mode + increment + MODES_NO) % MODES_NO);
  set_mode (new_mode);
}

void next_mode () {
  change_mode (+1);
}

void prev_mode () {
  change_mode (-1);
}

void set_RGB_leds(byte R, byte G, byte B) {

#ifdef MODE_LED_COMMON_ANODE
	R = 255 - R;
	G = 255 - G;
	B = 255 - B;
#endif
	analogWrite(MODE_LED_R_PIN, R);
	analogWrite(MODE_LED_G_PIN, G);
	analogWrite(MODE_LED_B_PIN, B);
}

void update_mode_leds () {
#ifdef ENABLE_MODE_LED_RGB
  byte *colors = mode_led_colors[current_mode];
  set_RGB_leds(colors[0], colors[1], colors[2]);

#endif  // ENABLE_MODE_LED_RGB
}



void flash_vid_mode_leds(bool mode, int b_delay) {

	byte *colors = mode_led_colors[3];

	if (mode == 0) //50Hz
	{
		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);
	}
	else //60Hz
	{
		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

		set_RGB_leds(colors[0], colors[1], colors[2]);
		delay(b_delay);
		set_RGB_leds(0, 0, 0);
		delay(b_delay);

    set_RGB_leds(colors[0], colors[1], colors[2]);
    delay(b_delay);
    set_RGB_leds(0, 0, 0);
    delay(b_delay);

    set_RGB_leds(colors[0], colors[1], colors[2]);
    delay(b_delay);
    set_RGB_leds(0, 0, 0);
    delay(b_delay);
	}

	update_mode_leds();
	
}

void set_mode (VideoMode m) {
	if (m == VID50) {
		digitalWrite(VIDEOMODE_PIN, LOW);
		flash_vid_mode_leds(0,100);
	} else if (m == VID60) {
    digitalWrite(VIDEOMODE_PIN, HIGH);
    flash_vid_mode_leds(1,50);
  }
	else {
		switch (m) {
		default:
		case EUR:
			digitalWrite(VIDEOMODE_PIN, LOW);    // PAL 50Hz
			digitalWrite(LANGUAGE_PIN, HIGH);    // ENG

			PORTB &= ~(1 << PORTB6); //low Sega CD BIOS EPROM pin 38
			PORTB |= (1 << PORTB7); //high Sega CD BIOS EPROM pin 39
			break;
		case USA:
			digitalWrite(VIDEOMODE_PIN, HIGH);   // NTSC 60Hz
			digitalWrite(LANGUAGE_PIN, HIGH);    // ENG

			PORTB &= ~((1 << PORTB6) | (1 << PORTB7)); //both low Sega CD BIOS EPROM pins 38 and 39
			break;
		case JAP:
			digitalWrite(VIDEOMODE_PIN, HIGH);   // NTSC 60Hz
			digitalWrite(LANGUAGE_PIN, LOW);     // JAP

			PORTB |= (1 << PORTB6); //high Sega CD BIOS EPROM pin 38
			PORTB &= ~(1 << PORTB7); //low Sega CD BIOS EPROM pin 39
			break;
		}

		current_mode = m;
		update_mode_leds();

		mode_last_changed_time = millis();
	}
}

void set_audio_mode(bool m) {
	digitalWrite(AUDIO_MODE_PIN, m); 
	audio_mode = m;
	update_mode_leds();

	audio_mode_last_changed_time = millis();
}


void handle_reset_button () {
  static byte debounce_level = LOW;
  static bool reset_pressed_before = false;
  static long last_int = 0, reset_press_start = 0;
  static unsigned int hold_cycles = 0;

  byte reset_level = digitalRead (RESET_IN_PIN);
  if (reset_level != debounce_level) {
    // Reset debouncing timer
    last_int = millis ();
    debounce_level = reset_level;
  } else if (millis () - last_int > DEBOUNCE_MS) {
    // OK, button is stable, see if it has changed
    if (reset_level != reset_inactive_level && !reset_pressed_before) {
      // Button just pressed
      reset_press_start = millis ();
      hold_cycles = 0;
    }
    else if (reset_level == reset_inactive_level && reset_pressed_before) {
      // Button released
      if (hold_cycles == 0) {
        debugln ("Reset button pushed for a short time");
        reset_console ();
		}

    } else {
      // Button has not just been pressed/released
      if (reset_level != reset_inactive_level && millis () % reset_press_start >= LONGPRESS_LEN * (hold_cycles + 1)) {
        // Reset has been hold for a while
        debugln ("Reset button hold");
        ++hold_cycles;
        next_mode ();
      }

    }

    reset_pressed_before = (reset_level != reset_inactive_level);
  }
}

void reset_console () {
  debugln ("Resetting console");

  digitalWrite (RESET_OUT_PIN, !reset_inactive_level);
  delay (RESET_LEN);
  digitalWrite (RESET_OUT_PIN, reset_inactive_level);
}


void setup () {

  OSCCAL=134; //manual oscillator calibration for serial debug

#ifdef ENABLE_SERIAL_DEBUG
  Serial.begin (9600);
#endif

DDRB |= (1 << DDB6) | (1 << DDB7); //enable CD bios outputs

  debugln ("Starting up...");

/* Rant: As per D4s's installation schematics out there (which we use too), it
 * seems that on consoles with an active low reset signal, the Reset In input
 * is taken before the pull-up resistor, while on consoles with active-high
 * reset it is taken AFTER the pull-down resistor. This means that detecting
 * the reset level by sampling the same line on both consoles is tricky, as in
 * both cases one of the Reset In/Out signals is left floating :(. The
 * following should work reliably, but we allow for a way to force the reset
 * line level.
 */
#ifndef FORCE_RESET_ACTIVE_LEVEL
  // Let things settle down and then sample the reset line
  delay (100);
  pinMode (RESET_IN_PIN, INPUT_PULLUP);
  reset_inactive_level = digitalRead (RESET_IN_PIN);
  debug ("Reset line is ");
  debug (reset_inactive_level ? "HIGH" : "LOW");
  debugln (" at startup");
#else
  reset_inactive_level = !FORCE_RESET_ACTIVE_LEVEL;
  debug ("Reset line is forced to active-");
  debugln (FORCE_RESET_ACTIVE_LEVEL ? "HIGH" : "LOW");
#endif

  if (reset_inactive_level == LOW) {
    // No need for pull-up
    pinMode (RESET_IN_PIN, INPUT);
  } else {
    pinMode (RESET_IN_PIN, INPUT_PULLUP);
  }

  // Enable reset
  pinMode (RESET_OUT_PIN, OUTPUT);
  digitalWrite (RESET_OUT_PIN, !reset_inactive_level);

  // Setup leds
#ifdef MODE_LED_R_PIN
  pinMode (MODE_LED_R_PIN, OUTPUT);
#endif

#ifdef MODE_LED_G_PIN
  pinMode (MODE_LED_G_PIN, OUTPUT);
#endif

#ifdef MODE_LED_B_PIN
  pinMode (MODE_LED_B_PIN, OUTPUT);
#endif

#ifdef PAD_LED_PIN
  pinMode (PAD_LED_PIN, OUTPUT);
#endif

#ifdef AUDIO_MODE_PIN
  pinMode(AUDIO_MODE_PIN, OUTPUT);
#endif


  // Init video mode
  pinMode (VIDEOMODE_PIN, OUTPUT);
  pinMode (LANGUAGE_PIN, OUTPUT);
  current_mode = EUR;
  audio_mode = 1;

#ifdef MODE_ROM_OFFSET
  byte tmp = EEPROM.read (MODE_ROM_OFFSET);
  debug ("Loaded video mode from EEPROM: ");
  debugln (tmp);
  if (tmp < MODES_NO) {
    // Palette EEPROM value is good
    current_mode = static_cast<VideoMode> (tmp);
  }
#endif

#ifdef AUDIO_ROM_OFFSET
  byte tmp2 = EEPROM.read(AUDIO_ROM_OFFSET);
  debug("Loaded audio mode from EEPROM: ");
  debugln(tmp2);
  audio_mode = static_cast<bool> (tmp2);
#endif

  set_mode (current_mode);
  set_audio_mode(audio_mode);
  mode_last_changed_time = 0; 
  audio_mode_last_changed_time = 0;  // No need to save what we just loaded

  // Prepare to read pad
  setup_pad ();

  // Finally release the reset line
  digitalWrite (RESET_OUT_PIN, reset_inactive_level);
}

void setup_pad () {
  // Set port directions
#if defined ARDUINO328
  DDRC &= ~((1 << DDC1) | (1 << DDC0));
  DDRD &= ~((1 << DDD6) | (1 << DDD5) | (1 << DDD4) | (1 << DDD3) | (1 << DDD2));
  attachInterrupt(digitalPinToInterrupt(2), select_low, FALLING);
#endif
}

/******************************************************************************/
/*
 * The basic idea here is to make up a byte where each bit represents the state
 * of a button, where 1 means pressed, for commodity's sake. The bit-button
 * mapping is defined in the PadButton enum above.
 *
 * To get consistent readings, we should really read all of the pad pins at
 * once, since some of them must be interpreted according to the value of the
 * SELECT signal. In order to do this we could connect all pins to a single port
 * of our MCU, but this is a luxury we cannot often afford, for various reasons.
 * Luckily, the UP and DOWN signals do not depend upon the SELECT pins, so we
 * can read them anytime, and this often takes us out of trouble.
 *
 * Note that printing readings through serial slows down the code so much that
 * it misses most of the readings with SELECT low!
 */


void select_low() {
	// Select is low, we have Start & A
	byte portdd = PIND;
	pad_status = (pad_status & 0x3F) | ((~portdd & ((1 << PD6) | (1 << PD5))) << 1);
 // delayMicroseconds(15000);
}

inline byte read_pad () {

    // Update UP and DOWN, which are always valid and on PORTC alone
    pad_status = (pad_status & 0xFC) | (~PINC & ((1 << PC1) | (1 << PC0)));

    byte portd = PIND;
    // Select is high, we have Right, Left, C & B
    pad_status = (pad_status & 0xC3) | ((~portd & ((1 << PIND6) | (1 << PD5) | (1 << PD4) | (1 << PD3))) >> 1);
    
    return pad_status;
}

#define IGNORE_COMBO_MS LONGPRESS_LEN

#define MIN_START_BUTTON_PRESS 3 // minimum Start button detections (filter junk), default is 50 times

void handle_pad() {
	static long last_combo_time = 0;

	read_pad();

#ifdef PAD_LED_PIN
	digitalWrite(PAD_LED_PIN, pad_status);
#endif



#if 1
	if (pad_status != 0) {
		//Serial.print("Pad status = ");
		Serial.println(pad_status, BIN);
}
#endif

    // Filtering start button false triggers to avoid combos.
//    if ((pad_status >> 7) & 0x01) {
//    i++;
//    } else i = 0;
//
//    if (i < MIN_START_BUTTON_PRESS) {
//    pad_status &= ~(1 << 7);     
//    } else i = 0;
   
  if ((pad_status & TRIGGER_COMBO) == TRIGGER_COMBO && millis () - last_combo_time > IGNORE_COMBO_MS) {
    Serial.print("combo status = ");
    Serial.println(pad_status, BIN);
    if ((pad_status & RESET_COMBO) == RESET_COMBO) {
      debugln ("Reset combo detected");
     // reset_console ();
      pad_status = 0;     // Avoid continuous reset (pad_status might keep the last value during reset!)
      last_combo_time = millis ();
      Serial.print("reset status = ");
      Serial.println(pad_status, BIN);
#ifdef EUR_COMBO
    } else if ((pad_status & EUR_COMBO) == EUR_COMBO) {
      debugln ("EUR mode combo detected");
      set_mode (EUR);
      last_combo_time = millis ();
#endif
#ifdef USA_COMBO
    } else if ((pad_status & USA_COMBO) == USA_COMBO) {
      debugln ("USA mode combo detected");
      set_mode (USA);
      last_combo_time = millis ();
#endif
#ifdef JAP_COMBO
    } else if ((pad_status & JAP_COMBO) == JAP_COMBO) {
      debugln ("JAP mode combo detected");
      set_mode (JAP);
      last_combo_time = millis ();
#endif
#ifdef NEXT_MODE_COMBO
    } else if ((pad_status & NEXT_MODE_COMBO) == NEXT_MODE_COMBO) {
      debugln ("Next mode combo detected");
      next_mode ();
      last_combo_time = millis ();
#endif
#ifdef PREV_MODE_COMBO
    } else if ((pad_status & PREV_MODE_COMBO) == PREV_MODE_COMBO) {
      debugln ("Previous mode combo detected");
      prev_mode ();
      last_combo_time = millis ();
#endif
#ifdef AUDIO_COMBO
    } else if ((pad_status & AUDIO_COMBO) == AUDIO_COMBO) {
      debugln ("Audio combo detected");
	  set_audio_mode(!audio_mode);
      last_combo_time = millis ();
#endif
    }
  }

  if ((pad_status & TRIGGER_COMBO2) == TRIGGER_COMBO2 && millis() - last_combo_time > IGNORE_COMBO_MS) {
	  if ((pad_status & COMBO_50) == COMBO_50) {
		  debugln("50Hz mode combo detected");
		  set_mode(VID50);
		  last_combo_time = millis();
	  } else if ((pad_status & COMBO_60) == COMBO_60) {
      debugln("60Hz mode combo detected");
      set_mode(VID60);
      last_combo_time = millis();
	  }
   
  }
}

void loop () {
  handle_reset_button ();
  handle_pad ();
  save_mode ();
  save_audio ();
}


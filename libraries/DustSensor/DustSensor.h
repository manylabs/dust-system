// Manylabs WifiSender Library 0.4.0
// copyright Manylabs 2015; MIT license
// --------
// This library provides a simple interface pulse-based dust sensors.
#ifndef _MANYLABS_DUST_SENSOR_H_
#define _MANYLABS_DUST_SENSOR_H_
#include "Arduino.h"


// The DustSensor class provides a simple interface pulse-based dust sensors.
class DustSensor {
public:

	// create a new DustSensor object
	DustSensor() {
		_pin = 0;
		_dustPulseDuration = 0;
		_dustPulseStart = 0;
	}

	// initialize the given pin (assumed to be a hardware interrupt pin)
	void init( byte pin ) {
		pinMode( pin, INPUT_PULLUP );
		_pin = pin;
	}

	inline byte pin() const { return _pin; }

	void change() {
		if (digitalRead( _pin ) == HIGH) {
			_dustPulseDuration += micros() - _dustPulseStart; // compute duration
		} else {
			_dustPulseStart = micros(); // start timing when pin is low
		}
	}

	float pulseRatio( unsigned long elapsedMsecs ) {
		float ratio = _dustPulseDuration / (float) (elapsedMsecs * 1000); // * 1000 to convert to microseconds
		_dustPulseDuration = 0;
		return ratio;
	}

private:

	byte _pin;
	volatile unsigned long _dustPulseDuration;
	volatile unsigned long _dustPulseStart;
};


#endif // _MANYLABS_DUST_SENSOR_H_

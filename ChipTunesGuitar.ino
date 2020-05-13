// URL: https://www.instructables.com/id/Arduino-Chiptunes/
#include <avr/io.h>
#include <avr/signal.h>
#include <avr/interrupt.h>

#define FIELDS_OF_COMMAND 3
#define NUM_OF_CHANNELS 4 //4
#define START_CHANNEL 0 //0

#include <avr/pgmspace.h>

extern "C" {

	typedef unsigned char u8;
	typedef unsigned short u16;
	typedef char s8;
	typedef short s16;
	typedef unsigned long u32;

	volatile u8 callbackwait;
	volatile u8 lastsample;

	volatile u8 timetoplay;

	u32 noiseseed = 1;

	const u16 freqtable[] = {
		0x0085, 0x008d, 0x0096, 0x009f, 0x00a8, 0x00b2, 0x00bd, 0x00c8, 0x00d4, // ~NOTE_C3... ~NOTE_GS3
		0x00e1, 0x00ee, 0x00fc, 0x010b, 0x011b, 0x012c, 0x013e, 0x0151, 0x0165,
		0x017a, 0x0191, 0x01a9, 0x01c2, 0x01dd, 0x01f9, 0x0217, 0x0237, 0x0259,
		0x027d, 0x02a3, 0x02cb, 0x02f5, 0x0322, 0x0352, 0x0385, 0x03ba, 0x03f3,
		0x042f, 0x046f, 0x04b2, 0x04fa, 0x0546, 0x0596, 0x05eb, 0x0645, 0x06a5,
		0x070a, 0x0775, 0x07e6, 0x085f, 0x08de, 0x0965, 0x09f4, 0x0a8c, 0x0b2c,
		0x0bd6, 0x0c8b, 0x0d4a, 0x0e14, 0x0eea, 0x0fcd, 0x10be, 0x11bd, 0x12cb,
		0x13e9, 0x1518, 0x1659, 0x17ad, 0x1916, 0x1a94, 0x1c28, 0x1dd5, 0x1f9b,
		0x217c, 0x237a, 0x2596, 0x27d3, 0x2a31, 0x2cb3, 0x2f5b, 0x322c, 0x3528,
		0x3851, 0x3bab, 0x3f37
	};

	const s8 sinetable[] = {  //Wave
		0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
		127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
		0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122,
		-125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81,
		-71, -60, -49, -37, -25, -12
	};

	const u8 validcmds[] = "0dfijlmtvw~+=";

	enum {
		WF_TRI, // Triangle sound
		WF_SAW, // Saw sound
		WF_PUL,
		WF_NOI  // Whire noise
	};

	volatile struct oscillator {
		u16 freq;
		u16 phase;
		u16 duty;
		u8 waveform;
		u8 volume; // 0-255
	} osc[NUM_OF_CHANNELS];

	struct channel {
		u8   inote;
		s8   bendd;
		s16   bend;
		s8   volumed;
		s16   dutyd;
		u8   vdepth;
		u8   vrate;
		u8   vpos;
		s16   inertia;
		u16   slur;
	} channel[NUM_OF_CHANNELS];

	void watchdogoff()
	{

	}
	
	struct commandHolder {
		String command[FIELDS_OF_COMMAND];
		int bufInd = 0;
	} holder;

	void runcmd(u8 ch, u8 cmd, u8 param) {
		switch(cmd) {
		case 'd':
			osc[ch].duty = param << 8;
			break;
		case 'f':
			channel[ch].volumed = param;
			break;
		case 'i':
			channel[ch].inertia = param << 1;
			break;
		case 'l':
			channel[ch].bendd = param;
			break;
		case 'm':
			channel[ch].dutyd = param << 6;
			break;
		case 'v':
			osc[ch].volume = param;
			break;
		case 'w':
			osc[ch].waveform = param;
			break;
		case '!':
			channel[ch].bend = 0;
			channel[ch].bendd = 0;
			channel[ch].volumed = 0;
			channel[ch].dutyd = 0;
			channel[ch].vdepth = 0;
			break;
		case '=':
			channel[ch].inote = param;
			break;
		case '~':
			if(channel[ch].vdepth != (param >> 4)) {
				channel[ch].vpos = 0;
			}
			channel[ch].vdepth = param >> 4;
			channel[ch].vrate = param & 15;
			break;
		}
	}

	void checkForCmd() {
		while (Serial.available()) {
			char line = (char) Serial.read();
			switch (line) {
				case '\n':
				case ';':
					runcmd((u8) atoi(holder.command[0].c_str()), 
						holder.command[1].c_str()[0], (u8) atoi(holder.command[2].c_str()));
					Serial.print ("Command:");
					Serial.print ((u8) atoi(holder.command[0].c_str()));
					Serial.print (holder.command[1]);
					Serial.println ((u8) atoi(holder.command[2].c_str()));
					holder.bufInd = -1;
				case ':':
					holder	.bufInd++;
					if (holder.bufInd >= 3 || holder.bufInd < 0) holder.bufInd = 0;
					holder.command[holder.bufInd] = "";
					break;
				default :
					holder.command[holder.bufInd] += line;
			}
		}
	}
	
	void playroutine() { // called at 50 Hz
		u8 ch;

		// Проверка наличия и выполнение команды
		checkForCmd();

		for(ch = START_CHANNEL; ch < NUM_OF_CHANNELS; ch++) {
			s16 vol;
			u16 duty;
			u16 slur;

			if(channel[ch].inertia) {
				s16 diff;

				slur = channel[ch].slur;
				diff = freqtable[channel[ch].inote] - slur;
				if(diff > 0) {
					if(diff > channel[ch].inertia) diff = channel[ch].inertia;
				} else if(diff < 0) {
					if(diff < -channel[ch].inertia) diff = -channel[ch].inertia;
				}
				slur += diff;
				channel[ch].slur = slur;
			} else {
				slur = freqtable[channel[ch].inote];
			}
			osc[ch].freq =
				slur +
				channel[ch].bend +
				((channel[ch].vdepth * sinetable[channel[ch].vpos & 63]) >> 2);
			channel[ch].bend += channel[ch].bendd;
			vol = osc[ch].volume + channel[ch].volumed;
			if(vol < 0) vol = 0;
			if(vol > 255) vol = 255;
			osc[ch].volume = vol;

			duty = osc[ch].duty + channel[ch].dutyd;
			if(duty > 0xe000) duty = 0x2000;
			if(duty < 0x2000) duty = 0xe000;
			osc[ch].duty = duty;

			channel[ch].vpos += channel[ch].vrate;
		}
	}

	int main() {
		Serial.begin(57600);
		
		asm("cli");
		watchdogoff();
		CLKPR = 0x80;
		CLKPR = 0x80;

		DDRC = 0x12;
		DDRD = 0xff;

		//PORTC = 0;

		pinMode(10,OUTPUT);
		pinMode(12,OUTPUT);
		
		timetoplay = 0;
		
		for (int i = 0; i < NUM_OF_CHANNELS; i++) {
			osc[i].volume = 0;
		}
		for (int i = 0; i < FIELDS_OF_COMMAND; i++ ) {
			holder.command[i] = "";
		}

		TCCR0A = 0x02;
		TCCR0B = 0x02; // clkIO/8, so 1/8 MHz
		OCR0A = 125;//125; // 8 KHz

		TCCR2A=0b10100011;
		TCCR2B=0b00000001;

		TIMSK0 = 0x02;

		asm("sei");
		Serial.println("READY");
		for(;;) {
			while(!timetoplay);

			timetoplay--;
			playroutine();
		}
	}


	ISR(TIMER0_COMPA_vect) // called at 8 KHz
	{
		u8 i;
		s16 acc;
		u8 newbit;

		OCR2B = lastsample;

		newbit = 0;
		if(noiseseed & 0x80000000L) newbit ^= 1;
		if(noiseseed & 0x01000000L) newbit ^= 1;
		if(noiseseed & 0x00000040L) newbit ^= 1;
		if(noiseseed & 0x00000200L) newbit ^= 1;
		noiseseed = (noiseseed << 1) | newbit;

		if(callbackwait) {
			callbackwait--;
		} else {
			timetoplay++;
			callbackwait = 180 - 1;
		}

		acc = 0;
		for(i = START_CHANNEL; i < NUM_OF_CHANNELS; i++) {
			s8 value; // [-32,31]

			switch(osc[i].waveform) {
			case WF_TRI:
				if(osc[i].phase < 0x8000) {
					value = -32 + (osc[i].phase >> 9);
				} else {
					value = 31 - ((osc[i].phase - 0x8000) >> 9);
				}
				break;
			case WF_SAW:
				value = -32 + (osc[i].phase >> 10);
				break;
			case WF_PUL:
				value = (osc[i].phase > osc[i].duty)? -32 : 31;
				break;
			case WF_NOI:
				value = (noiseseed & 63) - 32;
				break;
			default:
				value = 0;
				break;
			}
			osc[i].phase += osc[i].freq;

			acc += value * osc[i].volume; // rhs = [-8160,7905]
		}
		// acc [-32640,31620]
		lastsample = 128 + (acc >> 8); // [1,251]
	}

}

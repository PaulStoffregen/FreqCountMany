// Measures 10 frequencies by counting rising edges.  Best for 10kHz to 30 MHz
// Connect frequencies to pins 6, 9, 10, 11, 12, 13, 14, 15, 18, 19

// https://forum.pjrc.com/threads/71193-Teensy-4-Measuring-multiple-frequencies

// Timer	Pin   Pad	ALT	input mux
// QuadTimer4_1   6   B0_10	1
// QuadTimer4_2   9   B0_11	1
// QuadTimer1_0  10   B0_00	1
// QuadTimer1_2  11   B0_02	1
// QuadTimer1_1  12   B0_01	1
// QuadTimer2_0  13   B0_03	1	IOMUXC_QTIMER2_TIMER0_SELECT_INPUT=1
// QuadTimer3_2  14   AD_B1_02	1	IOMUXC_QTIMER3_TIMER2_SELECT_INPUT=1
// QuadTimer3_3  15   AD_B1_03	1	IOMUXC_QTIMER3_TIMER3_SELECT_INPUT=1
// QuadTimer3_1  18   AD_B1_01	1	IOMUXC_QTIMER3_TIMER1_SELECT_INPUT=1
// QuadTimer3_0  19   AD_B1_00	1	IOMUXC_QTIMER3_TIMER0_SELECT_INPUT=1

#define GATE_INTERVAL 2000  // microseconds for each gate interval
#define GATE_ACCUM    100   // number of intervals to accumulate
#define MULT_FACTOR   5     // multiply to get Hz output

typedef struct {
	IMXRT_TMR_t *timer;
	int timerchannel;
	int pin;
	int pinconfig;
	volatile uint32_t *inputselectreg;
	int inputselectval;
} timerinfo_t;

const timerinfo_t timerlist[] = {
	// Timer     Ch  Pin  Alt Input Select
	{&IMXRT_TMR4, 1,   6,  1, NULL, 0},
	{&IMXRT_TMR4, 2,   9,  1, NULL, 0},
	{&IMXRT_TMR1, 0,  10,  1, NULL, 0},
	{&IMXRT_TMR1, 2,  11,  1, NULL, 0},
	{&IMXRT_TMR1, 1,  12,  1, NULL, 0},
	{&IMXRT_TMR2, 0,  13,  1, &IOMUXC_QTIMER2_TIMER0_SELECT_INPUT, 1},
	{&IMXRT_TMR3, 2,  14,  1, &IOMUXC_QTIMER3_TIMER2_SELECT_INPUT, 1},
	{&IMXRT_TMR3, 3,  15,  1, &IOMUXC_QTIMER3_TIMER3_SELECT_INPUT, 1},
	{&IMXRT_TMR3, 1,  18,  1, &IOMUXC_QTIMER3_TIMER1_SELECT_INPUT, 0},
	{&IMXRT_TMR3, 0,  19,  1, &IOMUXC_QTIMER3_TIMER0_SELECT_INPUT, 1},
	// TODO: can 6 more be used with XBAR1 and GPR6 ?
};

#define NUM_TIMERS (sizeof(timerlist) / sizeof(timerinfo_t))

// gate interval interrupt deposits data here
volatile bool count_update = false;
volatile uint32_t count_output[NUM_TIMERS];

uint16_t read_count(unsigned int n) {
	static uint16_t prior[NUM_TIMERS];
	if (n >= NUM_TIMERS) return 0;
	uint16_t count = (timerlist[n].timer)->CH[timerlist[n].timerchannel].CNTR;
	uint16_t inc = count - prior[n];
	prior[n] = count;
	return inc;
}

void gate_timer() {
	static unsigned int count = 0;
	static uint32_t accum[NUM_TIMERS];

	for (unsigned int i=0; i < NUM_TIMERS; i++) {
		accum[i] += read_count(i);
	}
	if (++count >= GATE_ACCUM) {
		for (unsigned int i=0; i < NUM_TIMERS; i++) {
			count_output[i] = accum[i];
			accum[i] = 0;
		}
		count_update = true;
		count = 0;
	}
}

void setup() {
	// create some test frequencies
	analogWriteFrequency(0, 3000000);
	analogWriteFrequency(1, 15000000);
	analogWriteFrequency(2, 220000);
	analogWriteFrequency(4, 10000000);
	analogWriteFrequency(5, 455000);
	analogWrite(0, 128);
	analogWrite(1, 128);
	analogWrite(2, 128);
	analogWrite(4, 128);
	analogWrite(5, 128);

	// Welcome message
	Serial.begin(9600);
	Serial.print("FreqCountMany, maximum frequency = ");
	Serial.print(65535.0 / ((double)GATE_INTERVAL), 3);
	Serial.println(" MHz");

	// turn on clock to all quad timers
	CCM_CCGR6 |= CCM_CCGR6_QTIMER1(CCM_CCGR_ON) | CCM_CCGR6_QTIMER2(CCM_CCGR_ON)
		| CCM_CCGR6_QTIMER3(CCM_CCGR_ON) | CCM_CCGR6_QTIMER4(CCM_CCGR_ON);

	// configure all counting timers
	for (unsigned int i=0; i < NUM_TIMERS; i++) {
		IMXRT_TMR_t *timer = timerlist[i].timer;
		int ch = timerlist[i].timerchannel;
		timer->CH[ch].CTRL = 0;
		timer->CH[ch].CNTR = 0;
		timer->CH[ch].LOAD = 0;
		timer->CH[ch].COMP1 = 65535;
		timer->CH[ch].CMPLD1 = 65535;
		timer->CH[ch].SCTRL = 0;
		timer->CH[ch].CTRL = TMR_CTRL_CM(1) | TMR_CTRL_PCS(ch) | TMR_CTRL_LENGTH;
		int pin = timerlist[i].pin;
		*portConfigRegister(pin) = timerlist[i].pinconfig;
		if (timerlist[i].inputselectreg) {
			*timerlist[i].inputselectreg = timerlist[i].inputselectval;
		}
	}

	// start gate interval timer
	static IntervalTimer t;
	t.begin(gate_timer, GATE_INTERVAL);
}

void loop() {
	if (count_update) {
		for (unsigned int i=0; i < NUM_TIMERS; i++) {
			Serial.printf("%8u ", count_output[i] * MULT_FACTOR);
		}
		Serial.println();
		count_update = 0;
	}
}

/*
	Copyright (C) 2017 CurlyMo & easy12 & r41d

	This file is part of pilight.

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "rev_v4.h"

#define PULSE_MULTIPLIER	3
#define MIN_PULSE_LENGTH	250 // 253
#define MAX_PULSE_LENGTH	265 // 269
#define AVG_PULSE_LENGTH	258 // 264
#define RAW_LENGTH			50  // = (8 analog + 4 digital) * 4 + 2 sync

static int validate(void) {
	int i;
	for (i = 0; i < rev4_switch->rawlen; i++) {
		if (rev4_switch->raw[i] == 0) // no value may be zero
			return -1;
	}
	if (rev4_switch->rawlen == RAW_LENGTH) {
		if(rev4_switch->raw[rev4_switch->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
		   rev4_switch->raw[rev4_switch->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV)) {
			return 0;
		}
	}
	return -1;
}

static void createMessage(int sys, int unit, int state) {
	rev4_switch->message = json_mkobject();
	json_append_member(rev4_switch->message, "sys", json_mknumber(sys, 0));
	json_append_member(rev4_switch->message, "unit", json_mknumber(unit, 0));
	if(state == 1) {
		json_append_member(rev4_switch->message, "state", json_mkstring("on"));
	} else {
		json_append_member(rev4_switch->message, "state", json_mkstring("off"));
	}
}

// TODO: NOT IMPLEMENTED
static void parseCode(void) {
	printf("Parsing for rev4_switch is NOT IMPLEMENTED!\n");
	//createMessage(system, unit, state);
}

static void create0(int start) { // always 4 ints
	//printf("create 0 at %d\n", start);
	rev4_switch->raw[start]   = (AVG_PULSE_LENGTH);						// 128 high
	rev4_switch->raw[start+1] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 low
	rev4_switch->raw[start+2] = (AVG_PULSE_LENGTH);						// 128 high
	rev4_switch->raw[start+3] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 low
}

static void create1(int start) { // always 4 ints
	//printf("create 1 at %d\n", start);
	rev4_switch->raw[start]   = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 high
	rev4_switch->raw[start+1] = (AVG_PULSE_LENGTH);						// 128 low
	rev4_switch->raw[start+2] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 high
	rev4_switch->raw[start+3] = (AVG_PULSE_LENGTH);						// 128 low
}

static void createFloating(int start) { // always 4 ints
	//printf("create F at %d\n", start);
	rev4_switch->raw[start]   = (AVG_PULSE_LENGTH);						// 128 high
	rev4_switch->raw[start+1] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 low
	rev4_switch->raw[start+2] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 high
	rev4_switch->raw[start+3] = (AVG_PULSE_LENGTH);						// 128 low
}

static void createSync(int start) { // always 2 ints
	//printf("create sync at %d\n", start);
	rev4_switch->raw[start]   = (AVG_PULSE_LENGTH);						// 128  high
	rev4_switch->raw[start+1] = (34*AVG_PULSE_LENGTH);					// 3968 low
}


static void clearCode(void) {
	// initialize with zeros
	int i;
	for (i = 0; i < rev4_switch->rawlen; i++) {
		rev4_switch->raw[i] = 0;
	}
}

static void createSys(int sys) { // system code A0-A3, generates 4 bytes
	int i;
	for (i = 1; i <= 4; i++) {
		int addr = (i-1) * 4;
		if (sys == i)
			create1(addr);
		else
			createFloating(addr);
	}
}

static void createUnit(int unit) { // switch code A4-A7, generates 4 bytes
	int start = 16, i;
	for (i = 1; i <= 3; i++) {
		int addr = start + (i-1)*4;
		if (unit == i)
			create1(addr);
		else
			createFloating(addr);
	}
	create0(start + 12); // A7 is ground
}

static void createState(int state) { // D3-D0, generates 4 bytes
	int start = 32;
	create0(start);   // D3 = 0
	create0(start+4); // D2 = 0
	if (state == 1) { // turn on
		create1(start+8);  // D1 = 1
		create0(start+12); // D0 = 0
	} else { // turn off
		create0(start+8);  // D1 = 0
		create1(start+12); // D0 = 1
	}
}

static void createFooter(void) { // generate 4 bytes at the end
	createSync(48);
}

// TODO
static int createCode(struct JsonNode *code) {
	int sys = -1;
	int unit = -1;
	int state = -1;

	double itmp = -1;

	if(json_find_number(code, "sys", &itmp) == 0)
		sys = (int) round(itmp);

	if(json_find_number(code, "unit", &itmp) == 0)
		unit = (int) round(itmp);

	if(json_find_number(code, "off", &itmp) == 0)
		state = 0;
	else if(json_find_number(code, "on", &itmp) == 0)
		state = 1;

	if(sys == -1 || unit == -1 || state == -1) {
		logprintf(LOG_ERR, "rev4_switch: insufficient number of arguments");
		return EXIT_FAILURE;
	} else if(sys < 1 || sys > 4) {
		logprintf(LOG_ERR, "rev4_switch: invalid system id range");
		return EXIT_FAILURE;
	} else if(unit < 0 || unit > 3) {
		logprintf(LOG_ERR, "rev4_switch: invalid unit id range");
		return EXIT_FAILURE;
	} else {
		createMessage(sys, unit, state);
		clearCode();
		createSys(sys); // must generate 4 bytes (A0-A3)
		createUnit(unit); // must generate 4 bytes (A4-A7)
		createState(state); // must generate 4 bytes (D3-D0)
		createFooter(); // just creates sync
		rev4_switch->rawlen = RAW_LENGTH;
		//int i; for (i=0; i<RAW_LENGTH; i++) {
		//	printf("%3d ", rev4_switch->raw[i]); if (i%4==3) printf ("\n");
		//} printf ("\n");
	}
	return EXIT_SUCCESS;
}

// TODO
static void printHelp(void) {
	printf("\t -t --on\t\t\tsend an on signal\n");
	printf("\t -f --off\t\t\tsend an off signal\n");
	printf("\t -s --sys=sys\t\t\tspecify system code (A=1, B=2, C=3, D=4)\n"); // power socket code (A4 - A6)
	printf("\t -u --unit=unit\t\t\tspecify unit code of receiver (1-3)\n"); // system code (A0 - A3)
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
// TODO
void rev4Init(void) {

	protocol_register(&rev4_switch);
	protocol_set_id(rev4_switch, "rev4_switch");
	protocol_device_add(rev4_switch, "rev4_switch", "Rev 4 Switches");
	rev4_switch->devtype = SWITCH;
	rev4_switch->hwtype = RF433;
	rev4_switch->minrawlen = RAW_LENGTH;
	rev4_switch->maxrawlen = RAW_LENGTH;
	rev4_switch->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
	rev4_switch->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;

	options_add(&rev4_switch->options, 's', "sys", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([1-4]{1})$");
	options_add(&rev4_switch->options, 'u', "unit", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([1-4]{1})$");
	options_add(&rev4_switch->options, 't', "on", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&rev4_switch->options, 'f', "off", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);

	options_add(&rev4_switch->options, 0, "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&rev4_switch->options, 0, "confirm", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	rev4_switch->parseCode = &parseCode;
	rev4_switch->createCode = &createCode;
	rev4_switch->printHelp = &printHelp;
	rev4_switch->validate = &validate;
}

#if defined(MODULE) && !defined(_WIN32)
// TODO
void compatibility(struct module_t *module) {
	module->name = "rev4_switch";
	module->version = "0.13";
	module->reqversion = "6.0";
	module->reqcommit = "84";
}

void init(void) {
	rev4Init();
}
#endif

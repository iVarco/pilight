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
#include "rev_3_onoff_switch.h"

#define PULSE_MULTIPLIER	3
#define MIN_PULSE_LENGTH	124 // 253
#define MAX_PULSE_LENGTH	132 // 269
#define AVG_PULSE_LENGTH	128 // 264
#define RAW_LENGTH			64  // = (8 analog + 4 digital + 4 sync) * 4

static int validate(void) {
	if(rev_3_onoff_switch->rawlen == RAW_LENGTH) {
		if(rev_3_onoff_switch->raw[rev_3_onoff_switch->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
			rev_3_onoff_switch->raw[rev_3_onoff_switch->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV)) {
			return 0;
		}
	}
	return -1;
}

static void createMessage(int unit, int id, int state) {
	rev_3_onoff_switch->message = json_mkobject();
	json_append_member(rev_3_onoff_switch->message, "unit", json_mknumber(unit, 0));
	json_append_member(rev_3_onoff_switch->message, "id", json_mknumber(id, 0));
	if(state == 1) {
		json_append_member(rev_3_onoff_switch->message, "state", json_mkstring("on"));
	} else {
		json_append_member(rev_3_onoff_switch->message, "state", json_mkstring("off"));
	}
}

// DOESN'T WORK
static void parseCode(void) {
	int x = 0, i = 0, binary[RAW_LENGTH/4];

	/* Convert the one's and zero's into binary */
	for(x=0;x<rev_3_onoff_switch->rawlen-2;x+=4) {
		if(rev_3_onoff_switch->raw[x+3] > (int)((double)AVG_PULSE_LENGTH*((double)PULSE_MULTIPLIER/2))) {
			binary[i++] = 1;
		} else {
			binary[i++] = 0;
		}
	}

	int unit = binToDec(binary, 6, 9);
	int id = binToDec(binary, 0, 5);
	int state = binary[11];

	createMessage(unit, id, state);
}

// Note: 1 bit = 1024α

static void create0(int first) { // length = 1024 pulses
	rev_3_onoff_switch->raw[first]   = (AVG_PULSE_LENGTH);						// 128 high
	rev_3_onoff_switch->raw[first+1] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 low
	rev_3_onoff_switch->raw[first+2] = (AVG_PULSE_LENGTH);						// 128 high
	rev_3_onoff_switch->raw[first+3] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 low
}

static void create1(int first) { // length = 1024 pulses
	rev_3_onoff_switch->raw[first]   = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 high
	rev_3_onoff_switch->raw[first+1] = (AVG_PULSE_LENGTH);						// 128 low
	rev_3_onoff_switch->raw[first+2] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 high
	rev_3_onoff_switch->raw[first+3] = (AVG_PULSE_LENGTH);						// 128 low
}

static void createFloating(int first) { // length = 1024 pulses
	rev_3_onoff_switch->raw[first]   = (AVG_PULSE_LENGTH);						// 128 high
	rev_3_onoff_switch->raw[first+1] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 low
	rev_3_onoff_switch->raw[first+2] = (PULSE_MULTIPLIER*AVG_PULSE_LENGTH);		// 384 high
	rev_3_onoff_switch->raw[first+3] = (AVG_PULSE_LENGTH);						// 128 low
}

static void createSync(int first) { // length = 4096 pulses
	rev_3_onoff_switch->raw[first]   = (AVG_PULSE_LENGTH);						// 128  high
	rev_3_onoff_switch->raw[first+1] = (31*AVG_PULSE_LENGTH);					// 3968 low
}


static void clearCode(void) {
	// ???

	// old:
	//create1(0,3);
	//create0(4,47);
}

static void createUnit(int unit) { // system code A0-A3, generates 4 bytes
	int binary[255];
	int length = 0, i = 0, x = 0;
	length = decToBinRev(unit, binary);
	for(i = 0; i <= length; i++) {
		x = i * 4;
		if (binary[i] == 1)
			create1(x);
		else if (binary[i] == 0)
			createFloating(x);
		else
			printf("ERROR: Neither 0 nor 1\n");
	}
}

static void createId(int id) { // switch code A4-A7, generates 4 bytes
	int start = 16, i;
	for (i = 1; i <= 3; i++) {
		if (id == i)
			create1(start + (id-1)*4);
		else
			createFloating(start + (id-1)*4);
	}
	create0(start + 12);
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
	int unit = -1;
	int id = -1;
	int state = -1;
	double itmp = -1;

	if(json_find_number(code, "unit", &itmp) == 0)
		unit = (int)round(itmp);

	if(json_find_number(code, "id", &itmp) == 0)
		id = (int)round(itmp);

	if(json_find_number(code, "off", &itmp) == 0)
		state = 0;
	else if(json_find_number(code, "on", &itmp) == 0)
		state = 1;

	if(unit == -1 || id == -1 || state == -1) {
		logprintf(LOG_ERR, "rev_3_onoff_switch: insufficient number of arguments");
		return EXIT_FAILURE;
	} else if(id > 63 || id < 0) {
		logprintf(LOG_ERR, "rev_3_onoff_switch: invalid id range");
		return EXIT_FAILURE;
	} else if(unit > 15 || unit < 0) {
		logprintf(LOG_ERR, "rev_3_onoff_switch: invalid unit range");
		return EXIT_FAILURE;
	} else {
		createMessage(unit, id, state);
		clearCode();
		createUnit(unit); // must generate 4 bytes (A0-A3)
		createId(id); // must generate 4 bytes (A4-A7)
		createState(state); // must generate 4 bytes (D3-D0)
		createFooter(); // must generate 1 byte (Sync)
		rev_3_onoff_switch->rawlen = RAW_LENGTH;
	}
	return EXIT_SUCCESS;
}

// TODO
static void printHelp(void) {
	printf("\t -t --on\t\t\tsend an on signal\n");
	printf("\t -f --off\t\t\tsend an off signal\n");
	printf("\t -u --unit=unit\t\t\tcontrol a device with this unit code\n"); // system code (A0 - A3)
	printf("\t -i --id=id\t\t\tcontrol a device with this id\n"); // power socket code (A4 - A6)
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
// TODO
void rev3OnOffInit(void) {

	protocol_register(&rev_3_onoff_switch);
	protocol_set_id(rev_3_onoff_switch, "rev_3_onoff_switch");
	protocol_device_add(rev_3_onoff_switch, "rev_3_onoff_switch", "Rev 3 OnOff Switches");
	rev_3_onoff_switch->devtype = SWITCH;
	rev_3_onoff_switch->hwtype = RF433;
	rev_3_onoff_switch->minrawlen = RAW_LENGTH;
	rev_3_onoff_switch->maxrawlen = RAW_LENGTH;
	rev_3_onoff_switch->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
	rev_3_onoff_switch->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;

	options_add(&rev_3_onoff_switch->options, 'u', "unit", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([0-9]|[1][0-5])$");
	options_add(&rev_3_onoff_switch->options, 'i', "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^(6[0123]|[12345][0-9]|[0-9]{1})$");
	options_add(&rev_3_onoff_switch->options, 't', "on", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&rev_3_onoff_switch->options, 'f', "off", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);


	options_add(&rev_3_onoff_switch->options, 0, "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&rev_3_onoff_switch->options, 0, "confirm", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	rev_3_onoff_switch->parseCode = &parseCode;
	rev_3_onoff_switch->createCode = &createCode;
	rev_3_onoff_switch->printHelp = &printHelp;
	rev_3_onoff_switch->validate = &validate;
}

#if defined(MODULE) && !defined(_WIN32)
// TODO
void compatibility(struct module_t *module) {
	module->name = "rev_3_onoff_switch";
	module->version = "0.13";
	module->reqversion = "6.0";
	module->reqcommit = "84";
}

void init(void) {
	rev3OnOffInit();
}
#endif

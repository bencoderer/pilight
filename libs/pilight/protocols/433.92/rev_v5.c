/*
	Copyright (C) 2014 CurlyMo & wo-rasp

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
#include "rev_v5.h"

#define PULSE_MULTIPLIER	3
#define NORMAL_REPEATS		10
#define MIN_PULSE_LENGTH	300 //425
#define MAX_PULSE_LENGTH	610 //445
#define AVG_PULSE_LENGTH	433
#define RAW_LENGTH				50

static char idLetters[16] = {"ABIJCDGLKHOPEFMN"}; //A-P  //TODO !!!check if G and K is correct

static int validate(void) {
	if(rev5_switch->rawlen == RAW_LENGTH) {
		if(rev5_switch->raw[rev5_switch->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
		   rev5_switch->raw[rev5_switch->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV)) {
			return 0;
		}
	}

	return -1;
}

static void createMessage(int id, int unit, int state, int all) {
	rev5_switch->message = json_mkobject();
	json_append_member(rev5_switch->message, "id", json_mknumber(id, 0));
	if(all >= 1) {
		json_append_member(rev5_switch->message, "all", json_mknumber(all, 0));
	} else {
		json_append_member(rev5_switch->message, "unit", json_mknumber(unit, 0));
	}

	if(state == 0) {
		json_append_member(rev5_switch->message, "state", json_mkstring("off"));
	}
	else if(state == 1) {
		json_append_member(rev5_switch->message, "state", json_mkstring("on"));
	}
}

static void parseCode(void) {
	int x = 0, i = 0, binary[RAW_LENGTH/4];
    int binaryUnit[3];

	if(rev5_switch->rawlen>RAW_LENGTH) {
		logprintf(LOG_ERR, "rev5_switch: parsecode - invalid parameter passed %d", rev5_switch->rawlen);
		return;
	}

	/* Convert the one's and zero's into binary */
	for(x=0;x<rev5_switch->rawlen-2;x+=4) {
		if(rev5_switch->raw[x+3] > (int)((double)AVG_PULSE_LENGTH*((double)PULSE_MULTIPLIER/2))) {
			binary[i++] = 1;
		} else {
			binary[i++] = 0;
		}
	}

	binaryUnit[0] = binary[4];
	binaryUnit[1] = binary[7];
	binaryUnit[2] = binary[6];

	int unit = binToDec(binaryUnit, 0, 2);
	int rawState = binToDec(binary, 8, 9);
	int id = binToDec(binary, 0, 3);

	int state = 0;
	int all = 0;

	if (rawState == 1) {
		state = 1; //on
		all = 0;
	}
	else if (rawState == 2) {
		state = 0; //off
		all = 0;
	}
	else if (rawState == 3) {
		state = 0; //off
		all = 1;
	}
	else if (rawState == 0) {
		state = 1; //on
		all = 1;
	}

	unit++;

	if (all > 0) {
		if (unit >= 5) {
			all++;
		}
	}
	
	createMessage(id, unit, state, all);
}

static void createLow(int s, int e) {
    //method based on rev_v3.c
	int i;

	for(i=s;i<=e;i+=4) {
		rev5_switch->raw[i]=(PULSE_MULTIPLIER*AVG_PULSE_LENGTH); //long pulse for rev_v5
		rev5_switch->raw[i+1]=(AVG_PULSE_LENGTH);                    //short pause for rev_v5
		rev5_switch->raw[i+2]=(PULSE_MULTIPLIER*AVG_PULSE_LENGTH);
		rev5_switch->raw[i+3]=(AVG_PULSE_LENGTH);
	}
}
static void createHigh(int s, int e) {
	//copied from rev_v3.c
	int i;

	for(i=s;i<=e;i+=4) {
		rev5_switch->raw[i]=(AVG_PULSE_LENGTH);
		rev5_switch->raw[i+1]=(PULSE_MULTIPLIER*AVG_PULSE_LENGTH);
		rev5_switch->raw[i+2]=(AVG_PULSE_LENGTH);
		rev5_switch->raw[i+3]=(PULSE_MULTIPLIER*AVG_PULSE_LENGTH);
	}
}

static void clearCode(void) {
	//based on rev_v3
	createLow(0,3); //make low by default
	createLow(4,47);
}

static int createIdFromLetter(int l) {
	int i=0;

	for(i=0;i<(16-1);i++) {
		if((int)idLetters[i] == l) { //lookup the position of the char
            return i;
		}
	}
        return -1;
}

static void createId(int id) {
	//copied from rev_v3.c
	int binary[255];
	int length = 0;
	int i=0, x=0;

	length = decToBinRev(id, binary);
	for(i=0;i<=length;i++) {
		x=i*4;
		if(binary[i]==1) {
			createHigh(x, x+3);
		}
	}
}

static int generateUnitValue(int unit, int areabit) {
	const int UNITMSBMASK = 0x02;
	const int UNITLSBMASK = 0x01;
	int result = unit-1; //unit 1 is int value 0
	int unitMsb = (result & UNITMSBMASK) << 2;
	areabit = areabit << 2;
	result = result & UNITLSBMASK;
	result = result | /*2nd bit is always 0 |*/ areabit | unitMsb;

	return result;
}

static void createUnit(int unit) {
	//based on rev_v3.c
	int binary[255];
	int length = 0;
	int i=0, x=0;

	logprintf(LOG_INFO, "rev_v5:sending unit value '%d'", unit);

	length = decToBinRev(unit, binary);
	for(i=0;i<=length;i++) {
		if(binary[i]==1) {
			x=i*4;
			createHigh(16+x, 16+x+3); //first unit pulse is 16
		}
	}
}


static void createState(bool on, bool all) {
	//based on rev_v3.c
	int binary[255];
	int length = 0;
	int i=0, x=0;
    int rawState;

	if(all) {
		rawState = on ? 0 : 3; //ALL-ON -> 0 //ALL-OFF -> 3
	}
	else {
		rawState = on ? 1 : 2; //ON -> 1 //OFF -> 2
	}
	
	

	length = decToBinRev(rawState, binary);

	logprintf(LOG_INFO, "rev_v5:sending state value '%d', bit-length:%d", rawState, length);

	for(i=0;i<=length;i++) {
		if(binary[i]==1) {
			x=i*4;
			createHigh(32+x, 32+x+3); //first unit pulse is 32
		}
	}
}

static void createPostfix(void) {
	//the last 2 bits are always set
	createHigh(40, 40+3); 
	createHigh(44, 44+3); 
}

static void createFooter(void) {
	//copied from rev_v3.c
	rev5_switch->raw[48]=(AVG_PULSE_LENGTH);
	rev5_switch->raw[49]=(PULSE_DIV*AVG_PULSE_LENGTH);
}

static int createCode(struct JsonNode *code) {
	char sId[4] = {'\0'};
	char *stmp = NULL;
	int id = -1;
	int unit = -1;
	int state = -1;
	int all = 0;
	int areabit = 0;
	double itmp = -1;

	strcpy(sId, "-1");

	if(json_find_string(code, "id", &stmp) == 0)
		strcpy(sId, stmp);

	if((int)(sId[0]) >= 48 /*0*/ && (int)(sId[0]) <= 57 /*9*/) {
		//Id is already a number
		id = atoi(sId);
	}
    else if((int)(sId[0]) >= 65 /*A*/ && (int)(sId[0]) <= 80 /*P*/) {
		//Id is defined by char
		id = createIdFromLetter((int)sId[0]);
	}
	else {
		logprintf(LOG_ERR, "rev5_switch: invalid id number specified");
	}
	

	if(json_find_number(code, "unit", &itmp) == 0)
		unit = (int)round(itmp);
	if(json_find_number(code, "all", &itmp) == 0)
		all = (int)round(itmp);
	if(json_find_number(code, "off", &itmp) == 0)
		state=0;
	if(json_find_number(code, "on", &itmp) == 0)
		state=1;

	if((id == -1) || (state == -1) || (unit == -1 && all == 0)) { //id is not set, state is not set, neither unit nor all is set
		logprintf(LOG_ERR, "rev5_switch: insufficient number of arguments");
		return EXIT_FAILURE;
	} else if(id > (16-1) || id < 0) {
		logprintf(LOG_ERR, "rev5_switch: invalid id range");
		return EXIT_FAILURE;
	} else if(unit > -1 && all > 0) {
		logprintf(LOG_ERR, "rev5_switch: only unit-parameter or all-parameter can be specified");
		return EXIT_FAILURE;
	} else if((unit > 8 || unit < 1) && (all == 0)) {
		logprintf(LOG_ERR, "rev5_switch: invalid unit range");
		return EXIT_FAILURE;
	} else if((unit == -1) && (all > 2 || (all < 1))) {
		logprintf(LOG_ERR, "rev5_switch: invalid all value (use 1 for units 1-4 and 2 for units 5-8)");
		return EXIT_FAILURE;
	}
	else {
		if (unit > 0)
			areabit = (unit > 4) ? 1 : 0; 
			
		if(all > 0) {
			unit = 1;
			areabit = all-1;
		}

		createMessage(id, unit, state, all);
		clearCode();
		createId(id);
		createUnit(generateUnitValue(unit, areabit));
		createState(state > 0, all > 0);
		createPostfix();
		createFooter();
		rev5_switch->rawlen = RAW_LENGTH;

		logprintf(LOG_INFO, "rev5_switch: send raw %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",rev5_switch->raw[0], rev5_switch->raw[1], rev5_switch->raw[2], rev5_switch->raw[3], rev5_switch->raw[4], rev5_switch->raw[5], rev5_switch->raw[6], rev5_switch->raw[7], rev5_switch->raw[8], rev5_switch->raw[9], rev5_switch->raw[10], rev5_switch->raw[11], rev5_switch->raw[12], rev5_switch->raw[13], rev5_switch->raw[14], rev5_switch->raw[15], rev5_switch->raw[16], rev5_switch->raw[17], rev5_switch->raw[18], rev5_switch->raw[19], rev5_switch->raw[20], rev5_switch->raw[21], rev5_switch->raw[22], rev5_switch->raw[23], rev5_switch->raw[24], rev5_switch->raw[25], rev5_switch->raw[26], rev5_switch->raw[27], rev5_switch->raw[28], rev5_switch->raw[29], rev5_switch->raw[30], rev5_switch->raw[31], rev5_switch->raw[32], rev5_switch->raw[33], rev5_switch->raw[34], rev5_switch->raw[35], rev5_switch->raw[36], rev5_switch->raw[37], rev5_switch->raw[38], rev5_switch->raw[39], rev5_switch->raw[40], rev5_switch->raw[41], rev5_switch->raw[42], rev5_switch->raw[43], rev5_switch->raw[44], rev5_switch->raw[45], rev5_switch->raw[46], rev5_switch->raw[47], rev5_switch->raw[48], rev5_switch->raw[49]);
	}
	return EXIT_SUCCESS;
}


static void printHelp(void) {
	printf("\t -t --on\t\t\tsend an on signal\n");
	printf("\t -f --off\t\t\tsend an off signal\n");
	printf("\t -u --unit=unit\t\t\tcontrol a device with this unit code\n");
	printf("\t -i --id=id\t\t\tcontrol a device with this id\n");
	printf("\t -a --all=1/2\t\t\tsend command to all devices with this id and the defined group\n");
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void rev5Init(void) {

	protocol_register(&rev5_switch);
	protocol_set_id(rev5_switch, "rev5_switch");
	protocol_device_add(rev5_switch, "rev5_switch", "rev5_switch Switches");
	rev5_switch->devtype = SWITCH;
	rev5_switch->hwtype = RF433;
	rev5_switch->minrawlen = RAW_LENGTH;
	rev5_switch->maxrawlen = RAW_LENGTH;
	rev5_switch->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
	rev5_switch->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;

	options_add(&rev5_switch->options, 't', "on", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&rev5_switch->options, 'f', "off", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&rev5_switch->options, 'u', "unit", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([1-8])$");
	options_add(&rev5_switch->options, 'i', "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^(6[0123]|[12345][0-9]|[0-9]{1})$");
	options_add(&rev5_switch->options, 'a', "all", OPTION_HAS_VALUE, 0, JSON_NUMBER, NULL, "^([1-2])$");

	options_add(&rev5_switch->options, 0, "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&rev5_switch->options, 0, "confirm", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	rev5_switch->parseCode=&parseCode;
	rev5_switch->createCode=&createCode;
	rev5_switch->printHelp=&printHelp;
	rev5_switch->validate=&validate;
}


#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "rev5_switch";
	module->version = "0.9";
	module->reqversion = "6.0";
	module->reqcommit = "84";
}

void init(void) {
	rev5Init();
}
#endif



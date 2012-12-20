/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/input.h>

#include "recovery_ui.h"
#include "common.h"
#include "extendedcommands.h"

//Device specific boundaries for touch recognition
/*
	WARNING
	these might not be the same as resX, resY (from below)
	these have to be found by setting them to zero and then in debug mode
	check the values returned by on screen touch output by click on the
	touch panel extremeties
*/
int maxX=1535;		//Set to 0 for debugging
int maxY=2559;		//Set to 0 for debugging

/*
	the values of following two variables are dependent on specifc device resolution
	and can be obtained using the outputs of the gr_fb functions
*/
int resX=768;		//Value obtained from function 'gr_fb_width()'
int resY=1280;		//Value obtained from function 'gr_fb_height()'

char* MENU_HEADERS[] = { "developed by Napstar",
			 "",
			NULL };

char* MENU_ITEMS[] = { "reboot system now",
                       "install zip from sdcard",
                       "install zip from sideload",
                       "wipe data/factory reset",
                       "wipe cache partition",
                       "backup and restore",
                       "mounts and storage",
                       "advanced",
                       NULL };

void device_ui_init(UIParameters* ui_parameters) {
}

int device_recovery_start() {
    return 0;
}

int device_reboot_now(volatile char* key_pressed, int key_code) {
    return 0;
}

int device_perform_action(int which) {
    return which;
}

int device_wipe_data() {
    return 0;
}


//For those devices which has skewed X axis and Y axis detection limit (Not similar to XY resolution of device), So need normalization
int MT_X(int x)
{
	int out;
	out = maxX ? (int)((float)x*gr_fb_width()/maxX) : x;

	return out;
}

int MT_Y(int y)
{
	int out;
	out = maxY ? (int)((float)y*gr_fb_height()/maxY) : y;

	return out;
}

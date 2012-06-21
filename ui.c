/*
 * Copyright (C) 2007 The Android Open Source Project
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

//these are included in the original kernel's linux/input.h but are missing from AOSP

#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT 2
#define ABS_MT_TOUCH_MAJOR  0x30  /* Major axis of touching ellipse */
#define ABS_MT_WIDTH_MAJOR  0x32  /* Major axis of approaching ellipse */
#define ABS_MT_POSITION_X 0x35  /* Center X ellipse position */
#define ABS_MT_POSITION_Y 0x36  /* Center Y ellipse position */
#define ABS_MT_TRACKING_ID 0x39  /* Center Y ellipse position */
#endif

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "minui/minui.h"
#include "recovery_ui.h"

extern int __system(const char *command);

#ifdef BOARD_HAS_NO_SELECT_BUTTON
static int gShowBackButton = 1;
#else
static int gShowBackButton = 0;
#endif

#define MENU_HEIGHT gr_get_height(gMenuIcon[MENU_BUTTON_L])				//For touch based graphical menu
#define MENU_CENTER (gr_get_height(gMenuIcon[MENU_BUTTON_L])/2)			//To bring menu text at the center of button. Text location from bottom of menu button.
#define MENU_INCREMENT (gr_get_height(gMenuIcon[MENU_BUTTON_L])/2)		//Used for plotting menu buttons - specify spacing between two successive buttons location (X-start, Y-start)
#define MENU_ITEM_LEFT_OFFSET 0.05*resX						//X location relative to screen width for placement of menu items inside two cloumns of menu buttons
#define MENU_ITEM_RIGHT_OFFSET 0.55*resX
#define MENU_TITLE_BGK_HEIGHT gr_get_height(gMenuIcon[MENU_TITLE_BGK])

//In this case MENU_SELECT icon has maximum possible height.
#define MENU_MAX_HEIGHT gr_get_height(gMenuIcon[MENU_SELECT])		//Maximum allowed height for navigation icons

#define BUTTON_MAX_ROWS (int)(0.8*resY/MENU_INCREMENT)		//80% of screen length is allowed to have menu buttons
#define BUTTON_EQUIVALENT(x) (int)((x*CHAR_HEIGHT)/MENU_INCREMENT)		//Conversion of normal line to menu button icons

#define MAX_COLS 96
#define MAX_ROWS 40

#define MENU_MAX_COLS 64
#define MENU_MAX_ROWS 250

#ifndef BOARD_LDPI_RECOVERY
  #define CHAR_WIDTH 10
  #define CHAR_HEIGHT 18
#else
  #define CHAR_WIDTH 7
  #define CHAR_HEIGHT 16
#endif

#define PROGRESSBAR_INDETERMINATE_STATES 6
#define PROGRESSBAR_INDETERMINATE_FPS 24

static pthread_mutex_t gUpdateMutex = PTHREAD_MUTEX_INITIALIZER;
static gr_surface gBackgroundIcon[NUM_BACKGROUND_ICONS];
static gr_surface gMenuIcon[NUM_MENU_ICON];
gr_surface *gMenuIco = &gMenuIcon[0];
static gr_surface gProgressBarIndeterminate[PROGRESSBAR_INDETERMINATE_STATES];
static gr_surface gProgressBarEmpty;
static gr_surface gProgressBarFill;
static int ui_has_initialized = 0;
static int ui_log_stdout = 1;
static int selMenuIcon = 0;
static int selMenuButtonIcon = -1;

static const struct { gr_surface* surface; const char *name; } BITMAPS[] = {
    { &gBackgroundIcon[BACKGROUND_ICON_INSTALLING], "icon_installing" },
    { &gBackgroundIcon[BACKGROUND_ICON_ERROR],      "icon_error" },
    { &gBackgroundIcon[BACKGROUND_ICON_CLOCKWORK],  "icon_clockwork" },
    { &gBackgroundIcon[BACKGROUND_ICON_FIRMWARE_INSTALLING], "icon_firmware_install" },
    { &gBackgroundIcon[BACKGROUND_ICON_FIRMWARE_ERROR], "icon_firmware_error" },
	{ &gMenuIcon[MENU_BACK],      "icon_back" },
    { &gMenuIcon[MENU_DOWN],  	  "icon_down" },
    { &gMenuIcon[MENU_UP], 		  "icon_up" },
    { &gMenuIcon[MENU_SELECT],    "icon_select" },
	{ &gMenuIcon[MENU_BACK_M],    "icon_backM" },
    { &gMenuIcon[MENU_DOWN_M],    "icon_downM" },
    { &gMenuIcon[MENU_UP_M], 	  "icon_upM" },
    { &gMenuIcon[MENU_SELECT_M],  "icon_selectM" },
	{ &gMenuIcon[MENU_BUTTON_L],    	"button_L" },
	{ &gMenuIcon[MENU_BUTTON_L_SEL],	"button_L_sel" },
	{ &gMenuIcon[MENU_BUTTON_R],    	"button_R" },
	{ &gMenuIcon[MENU_BUTTON_R_SEL],	"button_R_sel" },
	{ &gMenuIcon[MENU_BUTTON_L_LOWHALF],	"button_L_Lowhalf" },
	{ &gMenuIcon[MENU_BUTTON_R_LOWHALF],	"button_R_Lowhalf" },
	{ &gMenuIcon[MENU_BUTTON_R_HALF],	"button_R_half" },
	{ &gMenuIcon[MENU_TITLE_BGK], 	 	"menu_title_bgk" },
    { &gProgressBarIndeterminate[0],    "indeterminate1" },
    { &gProgressBarIndeterminate[1],    "indeterminate2" },
    { &gProgressBarIndeterminate[2],    "indeterminate3" },
    { &gProgressBarIndeterminate[3],    "indeterminate4" },
    { &gProgressBarIndeterminate[4],    "indeterminate5" },
    { &gProgressBarIndeterminate[5],    "indeterminate6" },
    { &gProgressBarEmpty,               "progress_empty" },
    { &gProgressBarFill,                "progress_fill" },
    { NULL,                             NULL },
};

static gr_surface gCurrentIcon = NULL;

static enum ProgressBarType {
    PROGRESSBAR_TYPE_NONE,
    PROGRESSBAR_TYPE_INDETERMINATE,
    PROGRESSBAR_TYPE_NORMAL,
} gProgressBarType = PROGRESSBAR_TYPE_NONE;

// Progress bar scope of current operation
static float gProgressScopeStart = 0, gProgressScopeSize = 0, gProgress = 0;
static time_t gProgressScopeTime, gProgressScopeDuration;

// Set to 1 when both graphics pages are the same (except for the progress bar)
static int gPagesIdentical = 0;

// Log text overlay, displayed when a magic key is pressed
static char text[MAX_ROWS][MAX_COLS];
static int text_cols = 0, text_rows = 0;
static int text_col = 0, text_row = 0, text_top = 0;
static int show_text = 0;

static char menu[MENU_MAX_ROWS][MENU_MAX_COLS];
static char submenu[MENU_MAX_ROWS][MENU_MAX_COLS];					//Added to print huge file name in two rows
static int show_menu = 0;
static int menu_top = 0, menu_items = 0, menu_sel = 0;
static int menu_show_start = 0;             // this is line which menu display is starting at

// Key event input queue
static pthread_mutex_t key_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_queue_cond = PTHREAD_COND_INITIALIZER;
static int key_queue[256], key_queue_len = 0, key_queue_len_back = 0;
static volatile char key_pressed[KEY_MAX + 1];

// Threads
static pthread_t pt_ui_thread;
static pthread_t pt_input_thread;
static volatile int pt_ui_thread_active = 1;
static volatile int pt_input_thread_active = 1;

// Desire/Nexus and similar have 2, SGS has 5, SGT has 10, we take the max as it's cool. We'll only use 1 however
#define MAX_MT_POINTS 10

// Struct to store mouse events
static struct mousePosStruct {
  int x;
  int y;
  int pressure; // 0:up or 255:down
  int size;
  int num;
  int length; // length of the line drawn while in touch state
  int Xlength; // length of the line drawn along X axis while in touch state
} actPos, grabPos, oldMousePos[MAX_MT_POINTS], mousePos[MAX_MT_POINTS];
//Struct to return key events to recovery.c through ui_wait_key()
volatile struct keyStruct key;


// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with gUpdateMutex locked.
static void draw_background_locked(gr_surface icon)
{
    gPagesIdentical = 0;
    gr_color(0, 0, 0, 255);
    gr_fill(0, 0, resX, resY);

    if (icon) {
        int iconWidth = gr_get_width(icon);
        int iconHeight = gr_get_height(icon);
        int iconX = (resX - iconWidth) / 2;
        int iconY = (resY - iconHeight) / 2;
        gr_blit(icon, 0, 0, iconWidth, iconHeight, iconX, iconY);
    }
}

// Draw the currently selected icon (if any) at given location.
// Should only be called with gUpdateMutex locked.
static void draw_icon_locked(gr_surface icon,int locX, int locY, int Width, int Height)
{
    gPagesIdentical = 0;

    if (icon) {
		int iconWidth;
		int iconHeight;
        if(Width > 0 && Height > 0)
		{
			iconWidth = Width;
			iconHeight = Height;
		}
		else
		{
			iconWidth = gr_get_width(icon);
			iconHeight = gr_get_height(icon);
		}
        int iconX = locX - iconWidth / 2;
        int iconY = locY - iconHeight / 2;
        gr_blit(icon, 0, 0, iconWidth, iconHeight, iconX, iconY);
    }
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_progress_locked()
{
    if (gProgressBarType == PROGRESSBAR_TYPE_NONE) return;

    int iconHeight = gr_get_height(gBackgroundIcon[BACKGROUND_ICON_INSTALLING]);
    int width = gr_get_width(gProgressBarEmpty);
    int height = gr_get_height(gProgressBarEmpty);

    int dx = (resX - width)/2;
    int dy = (3*resY + iconHeight - 2*height)/4;

    // Erase behind the progress bar (in case this was a progress-only update)
    gr_color(0, 0, 0, 255);
    gr_fill(dx, dy, width, height);

    if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL) {
        float progress = gProgressScopeStart + gProgress * gProgressScopeSize;
        int pos = (int) (progress * width);

        if (pos > 0) {
          gr_blit(gProgressBarFill, 0, 0, pos, height, dx, dy);
        }
        if (pos < width-1) {
          gr_blit(gProgressBarEmpty, pos, 0, width-pos, height, dx+pos, dy);
        }
    }

    if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE) {
        static int frame = 0;
        gr_blit(gProgressBarIndeterminate[frame], 0, 0, width, height, dx, dy);
        frame = (frame + 1) % PROGRESSBAR_INDETERMINATE_STATES;
    }
}

static void draw_text_line(int row, const char* t, int rowOffset, int isMenu, int xOffset) {
  if (t[0] != '\0') {
    if (isMenu == 1)
		gr_text(xOffset, rowOffset + (row+1)*MENU_INCREMENT-1+(MENU_HEIGHT/2), t);
	else
		gr_text(xOffset, rowOffset + (row+1)*CHAR_HEIGHT-1, t);
  }
}

//#define MENU_TEXT_COLOR 255, 0, 0, 255
#define MENU_TEXT_COLOR 200, 200, 200, 255
#define NORMAL_TEXT_COLOR 200, 200, 200, 255
#define HEADER_TEXT_COLOR NORMAL_TEXT_COLOR
//#define HEADER_TEXT_COLOR 0, 0, 0, 255

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_screen_locked(void)
{
    if (!ui_has_initialized) return;

//ToDo: Following structure should be global
	struct { int x; int y; int xL; int xR; } MENU_ICON[] = {
		{  get_menu_icon_info(MENU_BACK,MENU_ICON_X),	get_menu_icon_info(MENU_BACK,MENU_ICON_Y), get_menu_icon_info(MENU_BACK,MENU_ICON_XL), get_menu_icon_info(MENU_BACK,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_DOWN,MENU_ICON_X),	get_menu_icon_info(MENU_DOWN,MENU_ICON_Y), get_menu_icon_info(MENU_DOWN,MENU_ICON_XL), get_menu_icon_info(MENU_DOWN,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_UP,MENU_ICON_X),	get_menu_icon_info(MENU_UP,MENU_ICON_Y), get_menu_icon_info(MENU_UP,MENU_ICON_XL), get_menu_icon_info(MENU_UP,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_SELECT,MENU_ICON_X),	get_menu_icon_info(MENU_SELECT,MENU_ICON_Y), get_menu_icon_info(MENU_SELECT,MENU_ICON_XL), get_menu_icon_info(MENU_SELECT,MENU_ICON_XR) },
	};

    draw_background_locked(gCurrentIcon);
    draw_progress_locked();

    if (show_text) {
        gr_color(0, 0, 0, 160);
        gr_fill(0, 0, resX, resY);

        int i = 0;
        int j = 0;
		int isMenu = 1;
		int rowOffset = 0;
        int row = 0;            // current row that we are drawing on
        if (show_menu) {

			draw_icon_locked(gMenuIcon[MENU_BACK], MENU_ICON[MENU_BACK].x, MENU_ICON[MENU_BACK].y, 0, 0 );
			draw_icon_locked(gMenuIcon[MENU_DOWN], MENU_ICON[MENU_DOWN].x, MENU_ICON[MENU_DOWN].y, 0, 0);
			draw_icon_locked(gMenuIcon[MENU_UP], MENU_ICON[MENU_UP].x, MENU_ICON[MENU_UP].y, 0, 0 );
			draw_icon_locked(gMenuIcon[MENU_SELECT], MENU_ICON[MENU_SELECT].x, MENU_ICON[MENU_SELECT].y, 0, 0 );
            //gr_color(MENU_TEXT_COLOR);
            //gr_fill(0, (menu_top + menu_sel - menu_show_start) * CHAR_HEIGHT,
            //        resX, (menu_top + menu_sel - menu_show_start + 1)*CHAR_HEIGHT+1);

			draw_icon_locked(gMenuIcon[MENU_TITLE_BGK], resX/2, MENU_TITLE_BGK_HEIGHT/2 , 0, 0 );            
			gr_color(HEADER_TEXT_COLOR);
            for (i = 0; i < menu_top; ++i) {
                draw_text_line(i, menu[i], rowOffset, !isMenu, 0);
                row++;
            }

            if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
                j = BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top) - 1;
            else
                j = menu_items - menu_show_start;

			rowOffset = menu_top*CHAR_HEIGHT-MENU_CENTER;		//Offset for printing menu text. 1/3rd of menu height so as to consider proper text placing inside button based on graphic shape.
            gr_color(MENU_TEXT_COLOR);
            for (i = menu_show_start + menu_top; i < (menu_show_start + menu_top + j); ++i) {
				if (i == menu_top + menu_sel) {
					if ((i - menu_top - menu_show_start)%2 == 0)
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_L_SEL], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT, 0, 0 );
						gr_color(255, 0, 0, 255);
						if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_LEFT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2, isMenu, MENU_ITEM_LEFT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2, isMenu, MENU_ITEM_LEFT_OFFSET);    
				                }
				                	
					}
					else
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_R_SEL], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT, 0, 0 );
						gr_color(255, 0, 0, 255);
	                    			if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2, isMenu, MENU_ITEM_RIGHT_OFFSET);    
				                }
					}					
                    gr_color(MENU_TEXT_COLOR);
                } else {
					if ((i - menu_top - menu_show_start)%2 == 0)
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_L], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT, 0, 0 );
				                gr_color(MENU_TEXT_COLOR);
	                			if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_LEFT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2, isMenu, MENU_ITEM_LEFT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2, isMenu, MENU_ITEM_LEFT_OFFSET);    
				                }
					}
					else
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_R], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT, 0, 0 );
				                gr_color(MENU_TEXT_COLOR);
				                	                    			if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2, isMenu, MENU_ITEM_RIGHT_OFFSET);    
				                }
					}

                }
                row++;
            }
			if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
			{
				if((BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top))%2 == 0)
					draw_icon_locked(gMenuIcon[MENU_BUTTON_L_LOWHALF], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT - MENU_INCREMENT/2, 0, 0 );
				else
					draw_icon_locked(gMenuIcon[MENU_BUTTON_R_LOWHALF], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT - MENU_INCREMENT/2, 0, 0 );
			}
			if (menu_show_start > 0)
			{
				draw_icon_locked(gMenuIcon[MENU_BUTTON_R_HALF], resX/2, menu_top*CHAR_HEIGHT + MENU_INCREMENT * 0.5, 0, 0 );
			}
        }
		rowOffset = menu_top*CHAR_HEIGHT + (row - menu_top + 2)*MENU_INCREMENT;
		if(row == 0)
			rowOffset=0;

        gr_color(NORMAL_TEXT_COLOR);
        for (i=0; (rowOffset/CHAR_HEIGHT + i) < text_rows; ++i) {
            draw_text_line(i, text[(rowOffset/CHAR_HEIGHT + i + text_top) % text_rows], rowOffset, !isMenu, 0);
        }
    }
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with gUpdateMutex locked.
static void update_screen_locked(void)
{
    if (!ui_has_initialized) return;
    draw_screen_locked();
    gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with gUpdateMutex locked.
static void update_progress_locked(void)
{
    if (!ui_has_initialized) return;
    if (show_text || !gPagesIdentical) {
        draw_screen_locked();    // Must redraw the whole screen
        gPagesIdentical = 1;
    } else {
        draw_progress_locked();  // Draw only the progress bar
    }
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
static void *progress_thread(void *cookie)
{
    for (;;) {
        usleep(1000000 / PROGRESSBAR_INDETERMINATE_FPS);
        pthread_mutex_lock(&gUpdateMutex);

        // update the progress bar animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE && !show_text) {
            update_progress_locked();
        }

        // move the progress bar forward on timed intervals, if configured
        int duration = gProgressScopeDuration;
        if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && duration > 0) {
            int elapsed = time(NULL) - gProgressScopeTime;
            float progress = 1.0 * elapsed / duration;
            if (progress > 1.0) progress = 1.0;
            if (progress > gProgress) {
                gProgress = progress;
                update_progress_locked();
            }
        }

        pthread_mutex_unlock(&gUpdateMutex);
    }
    return NULL;
}

// handle the action associated with user input touch events inside the ui handler
int device_handle_mouse(struct keyStruct *key, int visible)
{
	int j=0;
	if(show_menu && visible)
	{
		if((key->code == KEY_SCROLLUP || key->code == KEY_SCROLLDOWN) && (abs(abs(key->length) - abs(key->Xlength)) < 0.2*resY ))
		{
			if(key->code == KEY_SCROLLDOWN)
			{
				selMenuButtonIcon = -1;
				if (menu_show_start > 0)
				{
					menu_show_start = menu_show_start-BUTTON_MAX_ROWS+BUTTON_EQUIVALENT(menu_top);
					if (menu_show_start < 0)
						menu_show_start = 0;
						selMenuButtonIcon = 0;
					return menu_show_start;
				}			

				return GO_BACK;
			}
			else if	(key->code == KEY_SCROLLUP)
			{
				if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
				{
					menu_show_start = menu_show_start+BUTTON_MAX_ROWS-BUTTON_EQUIVALENT(menu_top) -2;
					selMenuButtonIcon = 1;
					return menu_show_start+1;
				}
			}
		}
		else if((key->y < (resY - MENU_MAX_HEIGHT)) &&  (key->length < 0.1*resY))
		{
		    if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
    			j = BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top);
    		else
    			j = menu_items - menu_show_start;
		
			int rowOffset = menu_top*CHAR_HEIGHT;

			int sel_menu;
			if(key->x < resX/2)
			{
				sel_menu = (int)((key->y - rowOffset)/MENU_HEIGHT );
				sel_menu = sel_menu*2;
			}
			else
			{
				sel_menu = (int)((key->y - rowOffset - MENU_INCREMENT)/MENU_HEIGHT );
				sel_menu = sel_menu*2 + 1;
			}

			if(key->y > rowOffset && key->y < rowOffset + j*MENU_INCREMENT)
			{	
				selMenuButtonIcon = -1;
				return sel_menu+menu_show_start;
			}
		}
		else if((key->y > (resY - MENU_MAX_HEIGHT))  &&  (key->length < 0.1*resY)) 
		{

//ToDo: Following structure should be global
		struct { int x; int y; int xL; int xR; } MENU_ICON[] = {
			{  get_menu_icon_info(MENU_BACK,MENU_ICON_X),	get_menu_icon_info(MENU_BACK,MENU_ICON_Y), get_menu_icon_info(MENU_BACK,MENU_ICON_XL), get_menu_icon_info(MENU_BACK,MENU_ICON_XR) },
			{  get_menu_icon_info(MENU_DOWN,MENU_ICON_X),	get_menu_icon_info(MENU_DOWN,MENU_ICON_Y), get_menu_icon_info(MENU_DOWN,MENU_ICON_XL), get_menu_icon_info(MENU_DOWN,MENU_ICON_XR) },
			{  get_menu_icon_info(MENU_UP,MENU_ICON_X),	get_menu_icon_info(MENU_UP,MENU_ICON_Y), get_menu_icon_info(MENU_UP,MENU_ICON_XL), get_menu_icon_info(MENU_UP,MENU_ICON_XR) },
			{  get_menu_icon_info(MENU_SELECT,MENU_ICON_X),	get_menu_icon_info(MENU_SELECT,MENU_ICON_Y), get_menu_icon_info(MENU_SELECT,MENU_ICON_XL), get_menu_icon_info(MENU_SELECT,MENU_ICON_XR) },
		};
		int position;

		position = key->x;

		if(position > MENU_ICON[MENU_BACK].xL && position < MENU_ICON[MENU_BACK].xR)
			return GO_BACK;
		else if(position > MENU_ICON[MENU_DOWN].xL && position < MENU_ICON[MENU_DOWN].xR)
			return HIGHLIGHT_DOWN;
		else if(position > MENU_ICON[MENU_UP].xL && position < MENU_ICON[MENU_UP].xR)
			return HIGHLIGHT_UP;
		else if(position > MENU_ICON[MENU_SELECT].xL && position < MENU_ICON[MENU_SELECT].xR)
			return SELECT_ITEM;
		}
	}
	return NO_ACTION;
}

// handle the user input events (mainly the touch events) inside the ui handler
static void ui_handle_mouse_input(int* curPos)
{
	pthread_mutex_lock(&key_queue_mutex);

//ToDo: Following structure should be global
	struct { int x; int y; int xL; int xR; } MENU_ICON[] = {
		{  get_menu_icon_info(MENU_BACK,MENU_ICON_X),	get_menu_icon_info(MENU_BACK,MENU_ICON_Y), get_menu_icon_info(MENU_BACK,MENU_ICON_XL), get_menu_icon_info(MENU_BACK,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_DOWN,MENU_ICON_X),	get_menu_icon_info(MENU_DOWN,MENU_ICON_Y), get_menu_icon_info(MENU_DOWN,MENU_ICON_XL), get_menu_icon_info(MENU_DOWN,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_UP,MENU_ICON_X),	get_menu_icon_info(MENU_UP,MENU_ICON_Y), get_menu_icon_info(MENU_UP,MENU_ICON_XL), get_menu_icon_info(MENU_UP,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_SELECT,MENU_ICON_X),	get_menu_icon_info(MENU_SELECT,MENU_ICON_Y), get_menu_icon_info(MENU_SELECT,MENU_ICON_XL), get_menu_icon_info(MENU_SELECT,MENU_ICON_XR) },
	};

if(TOUCH_CONTROL_DEBUG)
{
	ui_print("Touch gr_fb_width:\t%d,\tgr_fb_height:\t%d\n",resX,resY);
	ui_print("Touch X:\t%d,\tY:\t%d\n",curPos[1],curPos[2]);
}

  if (show_menu) {
    if (curPos[0] > 0) {
		int positionX,positionY;

		positionX = curPos[1];
		positionY = curPos[2];

		pthread_mutex_lock(&gUpdateMutex);
		if(positionY < (resY - MENU_MAX_HEIGHT)) {
			int j=0;
	
		    if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
    			j = BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top);
    		else
    			j = menu_items - menu_show_start;
		
			int rowOffset = menu_top*CHAR_HEIGHT;
			int sel_menu;
			if(positionX < resX/2)
			{
				sel_menu = (int)((positionY - rowOffset)/MENU_HEIGHT );
				sel_menu = sel_menu*2;
			}
			else
			{
				sel_menu = (int)((positionY - rowOffset - MENU_INCREMENT)/MENU_HEIGHT );
				sel_menu = sel_menu*2 + 1;
			}

			if(selMenuButtonIcon < 0)
				selMenuButtonIcon = 0;

			if(positionY > rowOffset && positionY < rowOffset + j*MENU_INCREMENT && selMenuButtonIcon != sel_menu)
			{	
				if (sel_menu %2 == 0)
				{
					draw_icon_locked(gMenuIcon[MENU_BUTTON_L_SEL], resX/2, menu_top*CHAR_HEIGHT + (sel_menu+1)*MENU_INCREMENT, 0, 0 );
	                gr_color(255, 0, 0, 255);
					if(menu[sel_menu + menu_show_start + menu_top][0] != '-')
	                    draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_LEFT_OFFSET);
	                else
	                {
	                    draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2, 1, MENU_ITEM_LEFT_OFFSET);
	                    draw_text_line(sel_menu , submenu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2, 1, MENU_ITEM_LEFT_OFFSET);
	                }
				}
				else
				{
					draw_icon_locked(gMenuIcon[MENU_BUTTON_R_SEL], resX/2, menu_top*CHAR_HEIGHT + (sel_menu+1)*MENU_INCREMENT, 0, 0 );
	                gr_color(255, 0, 0, 255);
					if(menu[sel_menu + menu_show_start + menu_top][0] != '-')
	                    draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_RIGHT_OFFSET);
	                else
	                {
	                    draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2, 1, MENU_ITEM_RIGHT_OFFSET);
	                    draw_text_line(sel_menu , submenu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2, 1, MENU_ITEM_RIGHT_OFFSET);
	                }
				}
				if (selMenuButtonIcon %2 == 0)
				{
					draw_icon_locked(gMenuIcon[MENU_BUTTON_L], resX/2, menu_top*CHAR_HEIGHT + (selMenuButtonIcon+1)*MENU_INCREMENT, 0, 0 );
	                gr_color(MENU_TEXT_COLOR);
					if(menu[selMenuButtonIcon + menu_show_start + menu_top][0] != '-')
	                    draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_LEFT_OFFSET);
	                else
	                {
	                    draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2, 1, MENU_ITEM_LEFT_OFFSET);
	                    draw_text_line(selMenuButtonIcon , submenu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2, 1, MENU_ITEM_LEFT_OFFSET);
	                }

				}
				else
				{
					draw_icon_locked(gMenuIcon[MENU_BUTTON_R], resX/2, menu_top*CHAR_HEIGHT + (selMenuButtonIcon+1)*MENU_INCREMENT, 0, 0 );
	                gr_color(MENU_TEXT_COLOR);
					if(menu[selMenuButtonIcon + menu_show_start + menu_top][0] != '-')
	                    draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_RIGHT_OFFSET);
	                else
	                {
	                    draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2, 1, MENU_ITEM_RIGHT_OFFSET);
	                    draw_text_line(selMenuButtonIcon , submenu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2, 1, MENU_ITEM_RIGHT_OFFSET);
	                }

				}
				selMenuButtonIcon = sel_menu;
				menu_sel = sel_menu;
				gr_flip();
			}
		}
		else {
			if(positionX > MENU_ICON[MENU_BACK].xL && positionX < MENU_ICON[MENU_BACK].xR) {
				draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y, 0, 0 );
				draw_icon_locked(gMenuIcon[MENU_BACK_M], MENU_ICON[MENU_BACK].x, MENU_ICON[MENU_BACK].y, 0, 0 );
				selMenuIcon = MENU_BACK;
				gr_flip();
			}
			else if(positionX > MENU_ICON[MENU_DOWN].xL && positionX < MENU_ICON[MENU_DOWN].xR) {			
				draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y, 0, 0 );
				draw_icon_locked(gMenuIcon[MENU_DOWN_M], MENU_ICON[MENU_DOWN].x, MENU_ICON[MENU_DOWN].y, 0, 0);
				selMenuIcon = MENU_DOWN;
				gr_flip();
			}
			else if(positionX > MENU_ICON[MENU_UP].xL && positionX < MENU_ICON[MENU_UP].xR) {
				draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y, 0, 0 );			
				draw_icon_locked(gMenuIcon[MENU_UP_M], MENU_ICON[MENU_UP].x, MENU_ICON[MENU_UP].y, 0, 0 );
				selMenuIcon = MENU_UP;
				gr_flip();
			}
			else if(positionX > MENU_ICON[MENU_SELECT].xL && positionX < MENU_ICON[MENU_SELECT].xR) {
				draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y, 0, 0 );			
				draw_icon_locked(gMenuIcon[MENU_SELECT_M], MENU_ICON[MENU_SELECT].x, MENU_ICON[MENU_SELECT].y, 0, 0 );
				selMenuIcon = MENU_SELECT;
				gr_flip();
			}
		}
		key_queue_len_back = key_queue_len;
		pthread_mutex_unlock(&gUpdateMutex);
     }
  }
  pthread_mutex_unlock(&key_queue_mutex);
}

// Reads input events, handles special hot keys, and adds to the key queue.
static void *input_thread(void *cookie)
{
    int rel_sum_x = 0;
    int rel_sum_y = 0;
    int fake_key = 0;
    int got_data = 0;
    while (pt_input_thread_active) {
        // wait for the next key event
        struct input_event ev;
        do {
          do {
            got_data = ev_get(&ev, 1000/PROGRESSBAR_INDETERMINATE_FPS);
            if (!pt_input_thread_active) {
              pthread_exit(NULL);
              return NULL;
            }
          } while (got_data==-1);

            if (ev.type == EV_SYN) {
                // end of a multitouch point
                if (ev.code == SYN_MT_REPORT) {
                  if (actPos.num>=0 && actPos.num<MAX_MT_POINTS) {
                    // create a fake keyboard event. We will use BTN_WHEEL, BTN_GEAR_DOWN and BTN_GEAR_UP key events to fake
                    // TOUCH_MOVE, TOUCH_DOWN and TOUCH_UP in this order
                    int type = BTN_WHEEL;
                    // new and old pressure state are not consistent --> we have touch down or up event
                    if ((mousePos[actPos.num].pressure!=0) != (actPos.pressure!=0)) {
                      if (actPos.pressure == 0) {
                        type = BTN_GEAR_UP;
                        if (actPos.num==0) {
                          if (mousePos[0].length<15) {
                            // consider this a mouse click
                            type = BTN_MOUSE;
                          }
                          memset(&grabPos,0,sizeof(grabPos));
                        }
                      } else if (actPos.pressure != 0) {
                        type == BTN_GEAR_DOWN;
                        if (actPos.num==0) {
                          grabPos = actPos;
                        }
                      }
                    }
                    fake_key = 1;
                    ev.type = EV_KEY;
                    ev.code = type;
                    ev.value = actPos.num+1;

                    // this should be locked, but that causes ui events to get dropped, as the screen drawing takes too much time
                    // this should be solved by making the critical section inside the drawing much much smaller
                    if (actPos.pressure) {
                      if (mousePos[actPos.num].pressure) {
                        actPos.length = mousePos[actPos.num].length + abs(mousePos[actPos.num].x-actPos.x) + abs(mousePos[actPos.num].y-actPos.y);
					  	if ( ((mousePos[actPos.num].x-actPos.x) < 0 && mousePos[actPos.num].Xlength > 0) || ((mousePos[actPos.num].x-actPos.x) > 0 && mousePos[actPos.num].Xlength < 0))
					  	{
							actPos.Xlength = mousePos[actPos.num].x-actPos.x;
						}
						else
					  	{
					  		actPos.Xlength = mousePos[actPos.num].Xlength + mousePos[actPos.num].x-actPos.x;
					  	}
                      } else {
                        actPos.length = 0;
                        actPos.Xlength = 0;
                      }
                    } else {
						if (abs(mousePos[actPos.num].Xlength) > (0.1*resX))
					  	{
					  		if (mousePos[actPos.num].Xlength > 0)
					  		{
					  			ev.code = KEY_SCROLLDOWN;
					  		}
					  		else
					  		{
					  			ev.code = KEY_SCROLLUP;
					  		}
					  	}
                      actPos.length = 0;
                      actPos.Xlength = 0;
                    }
                    oldMousePos[actPos.num] = mousePos[actPos.num];
                    mousePos[actPos.num] = actPos;
					int curPos[] = {actPos.pressure, actPos.x, actPos.y};
                    ui_handle_mouse_input(curPos);
                  }

                  memset(&actPos,0,sizeof(actPos));
                } else {
                  continue;
                }
            } else if (ev.type == EV_ABS) {
              // multitouch records are sent as ABS events. Well at least on the SGS-i9000
              if (ev.code == ABS_MT_POSITION_X) {
                actPos.x = MT_X(ev.value);
              } else if (ev.code == ABS_MT_POSITION_Y) {
                actPos.y = MT_Y(ev.value);
              } else if (ev.code == ABS_MT_TOUCH_MAJOR) {
                actPos.pressure = ev.value; // on SGS-i9000 this is 0 for not-pressed and 40 for pressed
              } else if (ev.code == ABS_MT_WIDTH_MAJOR) {
                // num is stored inside the high byte of width. Well at least on SGS-i9000
                if (actPos.num==0) {
                  // only update if it was not already set. On a normal device MT_TRACKING_ID is sent
                  actPos.num = ev.value >> 8;
                }
                actPos.size = ev.value & 0xFF;
              } else if (ev.code == ABS_MT_TRACKING_ID) {
                // on a normal device, the num is got from this value
                actPos.num = ev.value;
              }
            } else if (ev.type == EV_REL) {
                if (ev.code == REL_Y) {
                // accumulate the up or down motion reported by
                // the trackball.  When it exceeds a threshold
                // (positive or negative), fake an up/down
                // key event.
                    rel_sum_y += ev.value;
                    if (rel_sum_y > 3) { fake_key = 1; ev.type = EV_KEY; ev.code = KEY_DOWN; ev.value = 1; rel_sum_y = 0;
                    } else if (rel_sum_y < -3) { fake_key = 1; ev.type = EV_KEY; ev.code = KEY_UP; ev.value = 1; rel_sum_y = 0;
                    }
                }
                // do the same for the X axis
                if (ev.code == REL_X) {
                    rel_sum_x += ev.value;
                    if (rel_sum_x > 3) { fake_key = 1; ev.type = EV_KEY; ev.code = KEY_RIGHT; ev.value = 1; rel_sum_x = 0;
                    } else if (rel_sum_x < -3) { fake_key = 1; ev.type = EV_KEY; ev.code = KEY_LEFT; ev.value = 1; rel_sum_x = 0;
                    }
                }
            } else {
                rel_sum_y = 0;
                rel_sum_x = 0;
            }
        } while (ev.type != EV_KEY || ev.code > KEY_MAX);

        pthread_mutex_lock(&key_queue_mutex);
        if (!fake_key) {
            // our "fake" keys only report a key-down event (no
            // key-up), so don't record them in the key_pressed
            // table.
            key_pressed[ev.code] = ev.value;
        }
        fake_key = 0;
        const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
        if (ev.value > 0 && key_queue_len < queue_max) {
          // we don't want to pollute the queue with mouse move events
          if (ev.code!=BTN_WHEEL || key_queue_len==0 || key_queue[key_queue_len-1]!=BTN_WHEEL) {
            key_queue[key_queue_len++] = ev.code;
          }
          pthread_cond_signal(&key_queue_cond);
        }
        pthread_mutex_unlock(&key_queue_mutex);

        if (ev.value > 0 && device_toggle_display(key_pressed, ev.code)) {
            pthread_mutex_lock(&gUpdateMutex);
            show_text = !show_text;
            update_screen_locked();
            pthread_mutex_unlock(&gUpdateMutex);
        }

        if (ev.value > 0 && device_reboot_now(key_pressed, ev.code)) {
            reboot(RB_AUTOBOOT);
        }
    }
    return NULL;
}

void ui_init(void)
{
    ui_has_initialized = 1;
    gr_init();
    ev_init();

    text_col = text_row = 0;
    text_rows = resY / CHAR_HEIGHT;
    if (text_rows > MAX_ROWS) text_rows = MAX_ROWS;
    text_top = 1;

    text_cols = resX / CHAR_WIDTH;
    if (text_cols > MAX_COLS - 1) text_cols = MAX_COLS - 1;

    int i;
    for (i = 0; BITMAPS[i].name != NULL; ++i) {
        int result = res_create_surface(BITMAPS[i].name, BITMAPS[i].surface);
        if (result < 0) {
            if (result == -2) {
                LOGI("Bitmap %s missing header\n", BITMAPS[i].name);
            } else {
                LOGE("Missing bitmap %s\n(Code %d)\n", BITMAPS[i].name, result);
            }
            *BITMAPS[i].surface = NULL;
        }
    }

    memset(&actPos, 0, sizeof(actPos));
    memset(&grabPos, 0, sizeof(grabPos));
    memset(mousePos, 0, sizeof(mousePos));
    memset(oldMousePos, 0, sizeof(oldMousePos));

    pt_ui_thread_active = 1;
    pt_input_thread_active = 1;

    pthread_create(&pt_ui_thread, NULL, progress_thread, NULL);
    pthread_create(&pt_input_thread, NULL, input_thread, NULL);
}

char *ui_copy_image(int icon, int *width, int *height, int *bpp) {
    pthread_mutex_lock(&gUpdateMutex);
    draw_background_locked(gBackgroundIcon[icon]);
    *width = resX;
    *height = resY;
    *bpp = sizeof(gr_pixel) * 8;
    int size = *width * *height * sizeof(gr_pixel);
    char *ret = malloc(size);
    if (ret == NULL) {
        LOGE("Can't allocate %d bytes for image\n", size);
    } else {
        memcpy(ret, gr_fb_data(), size);
    }
    pthread_mutex_unlock(&gUpdateMutex);
    return ret;
}

void ui_set_background(int icon)
{
    pthread_mutex_lock(&gUpdateMutex);
    gCurrentIcon = gBackgroundIcon[icon];
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_indeterminate_progress()
{
    pthread_mutex_lock(&gUpdateMutex);
    if (gProgressBarType != PROGRESSBAR_TYPE_INDETERMINATE) {
        gProgressBarType = PROGRESSBAR_TYPE_INDETERMINATE;
        update_progress_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_progress(float portion, int seconds)
{
    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NORMAL;
    gProgressScopeStart += gProgressScopeSize;
    gProgressScopeSize = portion;
    gProgressScopeTime = time(NULL);
    gProgressScopeDuration = seconds;
    gProgress = 0;
    update_progress_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_set_progress(float fraction)
{
    pthread_mutex_lock(&gUpdateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && fraction > gProgress) {
        // Skip updates that aren't visibly different.
        int width = gr_get_width(gProgressBarIndeterminate[0]);
        float scale = width * gProgressScopeSize;
        if ((int) (gProgress * scale) != (int) (fraction * scale)) {
            gProgress = fraction;
            update_progress_locked();
        }
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_reset_progress()
{
    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NONE;
    gProgressScopeStart = gProgressScopeSize = 0;
    gProgressScopeTime = gProgressScopeDuration = 0;
    gProgress = 0;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    if (ui_log_stdout)
        fputs(buf, stdout);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_printlogtail(int nb_lines) {
    char * log_data;
    char tmp[PATH_MAX];
    FILE * f;
    int line=0;
    //don't log output to recovery.log
    ui_log_stdout=0;
    sprintf(tmp, "tail -n %d /tmp/recovery.log > /tmp/tail.log", nb_lines);
    __system(tmp);
    f = fopen("/tmp/tail.log", "rb");
    if (f != NULL) {
        while (line < nb_lines) {
            log_data = fgets(tmp, PATH_MAX, f);
            if (log_data == NULL) break;
            ui_print("%s", tmp);
            line++;
        }
        fclose(f);
    }
    ui_log_stdout=1;
}

void ui_reset_text_col()
{
    pthread_mutex_lock(&gUpdateMutex);
    text_col = 0;
    pthread_mutex_unlock(&gUpdateMutex);
}

#define MENU_ITEM_HEADER "-"
#define MENU_ITEM_HEADER_LENGTH strlen(MENU_ITEM_HEADER)
#define ALLOWED_CHAR (int)(resX*0.4)/CHAR_WIDTH

int ui_start_menu(char** headers, char** items, int initial_selection) {
    int i,j;
	int remChar;
	selMenuButtonIcon=0;
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        for (i = 0; i < text_rows; ++i) {
            if (headers[i] == NULL) break;
			remChar = (int)(resX - strlen(headers[i])*CHAR_WIDTH)/(CHAR_WIDTH*2);            //To centre align text from header
			for (j = 0; j < remChar; j++) {
				strcpy(menu[i]+j, " ");
			}
            strncpy(menu[i]+remChar, headers[i], text_cols- remChar);
            menu[i][text_cols-remChar] = '\0';
        }
        menu_top = i;
        for (; i < MENU_MAX_ROWS; ++i) {
            if (items[i-menu_top] == NULL) break;
            if (strlen(items[i-menu_top]) > ALLOWED_CHAR )			//Here "resX*0.4" is the maximum menu text length in each column. 
		{
		    strcpy(menu[i], MENU_ITEM_HEADER);
		    strncpy(menu[i] + MENU_ITEM_HEADER_LENGTH, items[i-menu_top], ALLOWED_CHAR - 2*MENU_ITEM_HEADER_LENGTH);
		    strcpy(menu[i] - MENU_ITEM_HEADER_LENGTH + ALLOWED_CHAR, MENU_ITEM_HEADER);
		    if(strlen(items[i-menu_top]) > (2*ALLOWED_CHAR - 1) )
		    {
		    	strncpy(submenu[i], items[i-menu_top] + ALLOWED_CHAR - 2*MENU_ITEM_HEADER_LENGTH, ALLOWED_CHAR-3);
		    	strcpy(submenu[i] + ALLOWED_CHAR-3, "..." );
		    }
		    else
		    	strncpy(submenu[i], items[i-menu_top] + ALLOWED_CHAR - 2*MENU_ITEM_HEADER_LENGTH, text_cols-1 - 2*MENU_ITEM_HEADER_LENGTH);
		}
		else
		{
			strncpy(menu[i], items[i-menu_top], text_cols-1);
		}
            menu[i][text_cols-1] = '\0';
        }

        if (gShowBackButton) {
            strcpy(menu[i], " - +++++Go Back+++++");
            ++i;
        }

        menu_items = i - menu_top;
        show_menu = 1;
        menu_sel = menu_show_start = initial_selection;
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
    if (gShowBackButton) {
        return menu_items - 1;
    }
    return menu_items;
}

int ui_menu_select(int sel) {
    int old_sel;
    pthread_mutex_lock(&gUpdateMutex);
    if (show_menu > 0) {
        old_sel = menu_sel;
        menu_sel = sel;

        if (menu_sel < 0) menu_sel = menu_items + menu_sel;
        if (menu_sel >= menu_items) menu_sel = menu_sel - menu_items;


        if (menu_sel < menu_show_start && menu_show_start > 0) {
            menu_show_start = menu_sel;
        }

        if (menu_sel - menu_show_start + menu_top >= BUTTON_MAX_ROWS) {
            menu_show_start = menu_sel + menu_top - BUTTON_MAX_ROWS + 1;
        }

        sel = menu_sel;

        if (menu_sel != old_sel) update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
    return sel;
}

void ui_end_menu() {
    int i;
    pthread_mutex_lock(&gUpdateMutex);
    if (show_menu > 0 && text_rows > 0 && text_cols > 0) {
        show_menu = 0;
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

int ui_text_visible()
{
    pthread_mutex_lock(&gUpdateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&gUpdateMutex);
    return visible;
}

void ui_show_text(int visible)
{
    pthread_mutex_lock(&gUpdateMutex);
    show_text = visible;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

struct keyStruct *ui_wait_key()
{

    pthread_mutex_lock(&key_queue_mutex);
    while (key_queue_len == 0) {
        pthread_cond_wait(&key_queue_cond, &key_queue_mutex);
    }
	key.code = key_queue[0];
    memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);

if(TOUCH_CONTROL_DEBUG)
	ui_print("[UI_WAIT_KEY] key code:\t%d\n",key.code);

	if((key.code == BTN_GEAR_UP || key.code == BTN_MOUSE) && !actPos.pressure && oldMousePos[actPos.num].pressure && key_queue_len_back != (key_queue_len -1))
	{	
		key.code = ABS_MT_POSITION_X;
		key.x = oldMousePos[actPos.num].x;
		key.y = oldMousePos[actPos.num].y;
	}

	key.length = oldMousePos[actPos.num].length;
	key.Xlength = oldMousePos[actPos.num].Xlength;
    pthread_mutex_unlock(&key_queue_mutex);
	return &key;
}

int ui_key_pressed(int key)
{
    // This is a volatile static array, don't bother locking
    return key_pressed[key];
}

void ui_clear_key_queue() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue_len = 0;
    pthread_mutex_unlock(&key_queue_mutex);
}

void ui_set_show_text(int value) {
    show_text = value;
}

void ui_set_showing_back_button(int showBackButton) {
    gShowBackButton = showBackButton;
}

int ui_get_showing_back_button() {
    return gShowBackButton;
}

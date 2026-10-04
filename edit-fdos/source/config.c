/* ------------- config.c ------------- */

#include "dflat.h"

/* ----- default colors for color video system ----- */
unsigned char color[CLASSCOUNT] [4] [2] = {
    /* ------------ NORMAL ------------ */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {WHITE, BLUE},      /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ---------- APPLICATION --------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {WHITE, BLUE},      /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ------------ TEXTBOX ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ------------ LISTBOX ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ----------- EDITBOX ------------ */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ---------- MENUBAR ------------- */
   {{WHITE, BLUE},      /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {WHITE, BLUE},      /* FRAME_COLOR  */
    {YELLOW, BLUE}},    /* HILITE_COLOR
                          Inactive, Shortcut (both FG) */

    /* ---------- POPDOWNMENU --------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, YELLOW}},  /* HILITE_COLOR
                           Inactive ,Shortcut (both FG) */

#ifdef INCLUDE_PICTUREBOX
    /* ------------ PICTUREBOX ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */
#endif

    /* ------------- DIALOG ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

	/* ------------ BOX --------------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {WHITE, BLACK},     /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ------------ BUTTON ------------ */
   {{WHITE, BLUE},      /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {WHITE, BLUE},      /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR
                           Inactive ,Shortcut (both FG) */
    /* ------------ COMBOBOX ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ------------- TEXT ----------- */
   {{0xff, 0xff},  /* STD_COLOR    */
    {0xff, 0xff},  /* SELECT_COLOR */
    {0xff, 0xff},  /* FRAME_COLOR  */
    {0xff, 0xff}}, /* HILITE_COLOR */

    /* ------------- RADIOBUTTON ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ------------- CHECKBOX ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ------------ SPINBUTTON ----------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ----------- ERRORBOX ----------- */
   {{WHITE, RED},       /* STD_COLOR    */
    {WHITE, RED},       /* SELECT_COLOR */
    {WHITE, RED},       /* FRAME_COLOR  */
    {WHITE, RED}},      /* HILITE_COLOR */

    /* ----------- MESSAGEBOX --------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ----------- HELPBOX ------------ */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ---------- STATUSBAR ------------- */
   {{WHITE, BLUE},      /* STD_COLOR    */
    {WHITE, BLUE},      /* SELECT_COLOR */
    {WHITE, BLUE},      /* FRAME_COLOR  */
    {YELLOW, BLUE}},    /* HILITE_COLOR */

    /* ----------- EDITOR ------------ */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}},   /* HILITE_COLOR */

    /* ---------- TITLEBAR ------------ */
   {{BLACK, YELLOW},    /* STD_COLOR    */
    {BLACK, YELLOW},    /* SELECT_COLOR */
    {BLACK, YELLOW},    /* FRAME_COLOR  */
    {BLACK, YELLOW}},   /* HILITE_COLOR */

    /* ------------ DUMMY ------------- */
   {{WHITE, BLACK},     /* STD_COLOR    */
    {WHITE, BLACK},     /* SELECT_COLOR */
    {YELLOW, BLACK},    /* FRAME_COLOR  */
    {YELLOW, BLACK}}    /* HILITE_COLOR */
};

/* ----- default colors for mono video system ----- */
unsigned char bw[CLASSCOUNT] [4] [2] = {
    /* ------------ NORMAL ------------ */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},/* HILITE_COLOR */

    /* ---------- APPLICATION --------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},/* HILITE_COLOR */

    /* ------------ TEXTBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ------------ LISTBOX ----------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ----------- EDITBOX ------------ */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ---------- MENUBAR ------------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {DARKGRAY, WHITE}}, /* HILITE_COLOR
                           Inactive, Shortcut (both FG) */

    /* ---------- POPDOWNMENU --------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {DARKGRAY, WHITE}}, /* HILITE_COLOR
                           Inactive ,Shortcut (both FG) */

#ifdef INCLUDE_PICTUREBOX
    /* ------------ PICTUREBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */
#endif

    /* ------------- DIALOG ----------- */
   {{LIGHTGRAY, BLACK},  /* STD_COLOR    */
    {BLACK, LIGHTGRAY},  /* SELECT_COLOR */
    {LIGHTGRAY, BLACK},  /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}}, /* HILITE_COLOR */

	/* ------------ BOX --------------- */
   {{LIGHTGRAY, BLACK},  /* STD_COLOR    */
    {LIGHTGRAY, BLACK},  /* SELECT_COLOR */
    {LIGHTGRAY, BLACK},  /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}}, /* HILITE_COLOR */

    /* ------------ BUTTON ------------ */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {DARKGRAY, WHITE}}, /* HILITE_COLOR
                           Inactive ,Shortcut (both FG) */
    /* ------------ COMBOBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ------------- TEXT ----------- */
   {{0xff, 0xff},  /* STD_COLOR    */
    {0xff, 0xff},  /* SELECT_COLOR */
    {0xff, 0xff},  /* FRAME_COLOR  */
    {0xff, 0xff}}, /* HILITE_COLOR */

    /* ------------- RADIOBUTTON ----------- */
   {{LIGHTGRAY, BLACK},  /* STD_COLOR    */
    {BLACK, LIGHTGRAY},  /* SELECT_COLOR */
    {LIGHTGRAY, BLACK},  /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}}, /* HILITE_COLOR */

    /* ------------- CHECKBOX ----------- */
   {{LIGHTGRAY, BLACK},  /* STD_COLOR    */
    {BLACK, LIGHTGRAY},  /* SELECT_COLOR */
    {LIGHTGRAY, BLACK},  /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}}, /* HILITE_COLOR */

    /* ------------ SPINBUTTON ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ----------- ERRORBOX ----------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},/* HILITE_COLOR */

    /* ----------- MESSAGEBOX --------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},/* HILITE_COLOR */

    /* ----------- HELPBOX ------------ */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {WHITE, BLACK},     /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ---------- STATUSBAR ------------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ----------- EDITOR ------------ */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},/* HILITE_COLOR */

    /* ---------- TITLEBAR ------------ */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ------------ DUMMY ------------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}} /* HILITE_COLOR */
};
/* ----- default colors for reverse mono video ----- */
unsigned char reverse[CLASSCOUNT] [4] [2] = {
    /* ------------ NORMAL ------------ */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ---------- APPLICATION --------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ------------ TEXTBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ------------ LISTBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ----------- EDITBOX ------------ */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ---------- MENUBAR ------------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {DARKGRAY, WHITE}}, /* HILITE_COLOR
                           Inactive, Shortcut (both FG) */

    /* ---------- POPDOWNMENU --------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {DARKGRAY, WHITE}}, /* HILITE_COLOR
                           Inactive ,Shortcut (both FG) */

#ifdef INCLUDE_PICTUREBOX
    /* ------------ PICTUREBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */
#endif

    /* ------------- DIALOG ----------- */
   {{BLACK, LIGHTGRAY},  /* STD_COLOR    */
    {LIGHTGRAY, BLACK},  /* SELECT_COLOR */
    {BLACK, LIGHTGRAY},  /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}}, /* HILITE_COLOR */

	/* ------------ BOX --------------- */
   {{BLACK, LIGHTGRAY},  /* STD_COLOR    */
    {BLACK, LIGHTGRAY},  /* SELECT_COLOR */
    {BLACK, LIGHTGRAY},  /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}}, /* HILITE_COLOR */

    /* ------------ BUTTON ------------ */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {WHITE, BLACK},     /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {DARKGRAY, WHITE}}, /* HILITE_COLOR
                           Inactive ,Shortcut (both FG) */
    /* ------------ COMBOBOX ----------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ------------- TEXT ----------- */
   {{0xff, 0xff},  /* STD_COLOR    */
    {0xff, 0xff},  /* SELECT_COLOR */
    {0xff, 0xff},  /* FRAME_COLOR  */
    {0xff, 0xff}}, /* HILITE_COLOR */

    /* ------------- RADIOBUTTON ----------- */
   {{BLACK, LIGHTGRAY},  /* STD_COLOR    */
    {LIGHTGRAY, BLACK},  /* SELECT_COLOR */
    {BLACK, LIGHTGRAY},  /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}}, /* HILITE_COLOR */

    /* ------------- CHECKBOX ----------- */
   {{BLACK, LIGHTGRAY},  /* STD_COLOR    */
    {LIGHTGRAY, BLACK},  /* SELECT_COLOR */
    {BLACK, LIGHTGRAY},  /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}}, /* HILITE_COLOR */

    /* ------------ SPINBUTTON ----------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ----------- ERRORBOX ----------- */
   {{BLACK, LIGHTGRAY},      /* STD_COLOR    */
    {BLACK, LIGHTGRAY},      /* SELECT_COLOR */
    {BLACK, LIGHTGRAY},      /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},     /* HILITE_COLOR */

    /* ----------- MESSAGEBOX --------- */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ----------- HELPBOX ------------ */
   {{BLACK, LIGHTGRAY}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {BLACK, LIGHTGRAY}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

    /* ---------- STATUSBAR ------------- */
   {{LIGHTGRAY, BLACK},      /* STD_COLOR    */
    {LIGHTGRAY, BLACK},      /* SELECT_COLOR */
    {LIGHTGRAY, BLACK},      /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},     /* HILITE_COLOR */

    /* ----------- EDITOR ------------ */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {BLACK, LIGHTGRAY}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {BLACK, LIGHTGRAY}},/* HILITE_COLOR */

	/* ---------- TITLEBAR ------------ */
   {{LIGHTGRAY, BLACK},      /* STD_COLOR    */
    {LIGHTGRAY, BLACK},      /* SELECT_COLOR */
    {LIGHTGRAY, BLACK},      /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}},     /* HILITE_COLOR */

    /* ------------ DUMMY ------------- */
   {{LIGHTGRAY, BLACK}, /* STD_COLOR    */
    {LIGHTGRAY, BLACK}, /* SELECT_COLOR */
    {LIGHTGRAY, BLACK}, /* FRAME_COLOR  */
    {LIGHTGRAY, BLACK}} /* HILITE_COLOR */
};

/* ------ default configuration values ------- */
CONFIG cfg = {
    VERSION,
    0,          /* Color			*/
    0,          /* Amber theme            */
    FALSE,	/* Snowy CGA			*/
    TRUE,       /* Editor Insert Mode		*/
    8,          /* Editor tab stop size		*/ /* was 4 before 0.7b */
    FALSE,      /* Editor word wrap		*/
#ifdef INCLUDE_WINDOWOPTIONS
    TRUE,       /* Application Border		*/
    TRUE,       /* Application Title		*/
    TRUE,       /* Status Bar			*/
#endif
    FALSE,      /* Textured application window	*/
    SCREENHEIGHT, /* Number of screen lines */
    "Lpt1",	/* Printer Port			*/
    66,         /* Lines per printer page	*/
    80,		/* characters per printer line	*/
    6,		/* Left printer margin		*/
    70,		/* Right printer margin		*/
    3,		/* Top printer margin		*/
    55,		/* Bottom printer margin	*/
    FALSE	/* Read only mode               */
};


/* --------- set window colors --------- */
void SetStandardColor(WINDOW wnd)
{
    foreground = WndForeground(wnd);
    background = WndBackground(wnd);
}

void SetReverseColor(WINDOW wnd)
{
    foreground = SelectForeground(wnd);
    background = SelectBackground(wnd);
}

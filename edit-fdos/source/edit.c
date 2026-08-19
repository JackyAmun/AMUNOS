/*  FreeDOS Editor

*/

/* I N C L U D E S /////////////////////////////////////////////////////// */

#include "dflat.h"

/* D E F I N E S ///////////////////////////////////////////////////////// */

#define CHARSLINE 80
#define LINESPAGE 66
/* define DELFILE to create a "delete file" menu item */

/* G L O B A L S ///////////////////////////////////////////////////////// */

char DFlatApplication[] = "Edit";
static char Untitled[] = "Untitled";
static int wndpos; /* LineStartsAt, StartLine, LineCtr, CharCtr (打印已删) */

/* P R O T O T Y P E S /////////////////////////////////////////////////// */

int classify_args(int, char *[], char *[], char *[]);
static int MemoPadProc(WINDOW, MESSAGE, PARAM, PARAM);
static void NewFile(WINDOW,char *);
static void SelectFile(WINDOW);
static void PadWindow(WINDOW, char *);
static void OpenPadWindow(WINDOW, char *,char *);
static void LoadFile(WINDOW);
static void SaveFile(WINDOW, int);
#ifdef DELFILE
static void DeleteFile(WINDOW);
#endif
static int PadWndProc(WINDOW, MESSAGE, PARAM, PARAM);
static char *NameComponent(char *);
static int stricmp(const char *, const char *);  /* AMUNOS libc 无 stricmp */
static void FixTabMenu(void);

/* ---- 大小写不敏感比较 (原 stricmp, AMUNOS libc 未提供) ---- */
static int stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb)
            return ca - cb;
        a++; b++;
    }
    return (*a ? (*a >= 'A' && *a <= 'Z' ? *a + 32 : *a)
               : (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b));
}

/* F U N C T I O N S ///////////////////////////////////////////////////// */

int classify_args(int argc, char *rawargs[], char *fileargs[], char *optargs[])
{
    int index, jndex, kndex;
    char *argptr;

    /* skip index=0, aka argv[0]==edit.exe */
    for (index=1,jndex=0,kndex=0;index<argc;index++)
        {
        argptr = rawargs[index];
        if (*argptr == '/')
            {
            argptr++;
            optargs[kndex++] = argptr;
            } /* end if. */
        else
            {
            fileargs[jndex++] = argptr;
            } /* end else. */

        } /* end for. */

   return kndex;

} /* end classify_args. */

void main(int argc, char *argv[])
{
    WINDOW wnd;
    FILE *fp;
    char *fileargs[64], *optargs[64];
    int n_options, n_files, index, help_flag=0;

    /* AMUNOS: 无配置文件, 固定 80x25; /B 选项在下方循环里处理 */
    cfg.ScreenLines = SCREENHEIGHT;

    n_options = classify_args(argc, argv, fileargs, optargs);
    n_files = argc - n_options - 1; /* the (-)1 is argv[0] */

    for (index=0; index<n_options; index++)
        {
        if (optargs[index][0] == '?') help_flag=1;
        else if (optargs[index][0] == 'B' || optargs[index][0] == 'b')
            cfg.mono=1; /* "monochrome" color scheme */
        else if (optargs[index][0] == 'i' || optargs[index][0] == 'I')
            cfg.mono=2;	/* "reverse" color scheme */
        else if (optargs[index][0] == 'r' || optargs[index][0] == 'R')
            cfg.ReadOnlyMode=TRUE;
        else if (optargs[index][0] == 'H' || optargs[index][0] == 'h') {
            cfg.ScreenLines = (isEGA() || isVGA()) ? (isVGA() ? 50 : 43)
                : SCREENHEIGHT; /* improved in 0.7b */
            }
            /* 0.7a and before: cfg.ScreenLines = SCREENHEIGHT; */
        else
            {
            printf("Invalid parameter - /%s\n", optargs[index]);
            exit(1);
            }

        }

    if (help_flag)
        {
        printf("FreeDOS Editor    Version " VERSION ".\n\n"
               "Syntax: EDIT [/B] [/H] [/?] [file(s)]\n"
               "  /B     Force monochrome mode\n"
               "  /I     Use inverse color scheme\n"
               "  /H     Use 43/50 lines on EGA/VGA\n"
               "  /R     Open all files read-only\n"
               "  /?     Display this help message\n"
               "  [file] Specify file(s) to load.\n"
               "         Wildcards can be used here.\n");
        exit(1);
        }

    if (!init_messages())
        return;

#ifdef ENABLEGLOBALARGV
    Argv = argv;
#endif

    /* (LoadConfig was called at this place in pre-0.7b versions) */

    if (cfg.ReadOnlyMode) {
        wnd = CreateWindow(APPLICATION, "FreeDOS Edit " VERSION " (viewer mode)", 0, 0, -1, -1,
            &MainMenu, NULL, MemoPadProc, MOVEABLE | SIZEABLE | HASBORDER | MINMAXBOX | HASSTATUSBAR);
    } else {
        wnd = CreateWindow(APPLICATION, "FreeDOS Edit " VERSION, 0, 0, -1, -1,
            &MainMenu, NULL, MemoPadProc, MOVEABLE | SIZEABLE | HASBORDER | MINMAXBOX | HASSTATUSBAR);
    }

    LoadHelpFile(DFlatApplication);
    SendMessage(wnd, SETFOCUS, TRUE, 0);

    /*  Load the files from args - if the file does not exist, open a new
        window.... */
    for (index=0;index<n_files;index++)
        {
        /*  Check if the file exists... */
        /* added by Eric: Do NOT try to open files with names */
        /* that contain wildcards, otherwise you may NewFile  */
        /* files with wildcards in their names. Sigh! 11/2002 */
        fp = NULL;
        if (((strchr(fileargs[index],'*') == NULL)) && ((strchr(fileargs[index],'?') == NULL)) && ((fp = fopen(fileargs[index],"rt")) == NULL))
            {
            NewFile(wnd,fileargs[index]);
            }
        else
            {
            if (fp != NULL)
                 fclose(fp);  /* don't leave open file handle [from test if exists in above if] */

            PadWindow(wnd, fileargs[index]);
            }

        }

    /* Set global underline cursor */
    underline_cursor();
    unhidecursor();
    while (dispatch_message());

}

/* ------ open text files and put them into editboxes -----
 * AMUNOS: 无 findfirst/findnext 通配符展开, 命令行直接单文件打开 */
static void PadWindow(WINDOW wnd, char *FileName)
{
    OpenPadWindow(wnd, FileName, NULL);
}

/* ------- window processing module for the Edit application window ----- */
static int MemoPadProc(WINDOW wnd,MESSAGE msg,PARAM p1,PARAM p2)
{
    int rtn;

    switch (msg)
        {
        case CREATE_WINDOW:
            rtn = DefaultWndProc(wnd, msg, p1, p2);
            if (cfg.InsertMode)
                SetCommandToggle(&MainMenu, ID_INSERT);

            if (cfg.WordWrap)
                SetCommandToggle(&MainMenu, ID_WRAP);

            FixTabMenu();
            return rtn;
        case COMMAND:
	    switch ((int)p1)
                {
		case ID_NEW:
		    NewFile(wnd,NULL);
		    return TRUE;
		case ID_OPEN:
		    SelectFile(wnd);
		    return TRUE;
		case ID_SAVE:
		    SaveFile(inFocus, FALSE);
		    return TRUE;
		case ID_SAVEAS:
		    SaveFile(inFocus, TRUE);
		    return TRUE;
                case ID_CLOSE:
                    SendMessage(inFocus, CLOSE_WINDOW, 0, 0);
                    SkipApplicationControls();
                    return TRUE;
#ifdef DELFILE
                case ID_DELETEFILE:
		    DeleteFile(inFocus);
		    return TRUE;
#endif
                case ID_EXIT: 
                    break;
                case ID_WRAP:
                    cfg.WordWrap = GetCommandToggle(&MainMenu, ID_WRAP);
		    return TRUE;
                case ID_INSERT:
                    cfg.InsertMode = GetCommandToggle(&MainMenu, ID_INSERT);
                    return TRUE;
		case ID_TAB0:
		    cfg.Tabs = 1; /* type-through TAB char mode -ea */
                    FixTabMenu(); /* show current value in tab menu */
		    return TRUE;
                case ID_TAB2:
                    cfg.Tabs = 2;
                    FixTabMenu(); /* show current value in tab menu */
		    return TRUE;
                case ID_TAB4:
                    cfg.Tabs = 4;
                    FixTabMenu();
		    return TRUE;
                case ID_TAB6:
                    cfg.Tabs = 6; 
                    FixTabMenu();
		    return TRUE;
                case ID_TAB8:
                    cfg.Tabs = 8;
                    FixTabMenu();
		    return TRUE;
#ifndef NOCALENDAR
                case ID_CALENDAR:
                    Calendar(wnd); 
                    return TRUE;
#endif
#if WITH_ASCIITAB			/* new 0.7c */
                case ID_ASCIITAB:
                    Asciitable(wnd); 
                    return TRUE;
#endif
		case ID_ABOUT:
		    {
		    char aboutMsg [] =
		               "          FreeDOS Edit           \n"
                               "          Version @              \n"
                               "                                 \n"
                               "   FreeDOS Edit is based on the  \n"
                               "   D-Flat application published  \n"
                               "   in Dr. Dobb's Journal.        \n"
                               "                                 \n"
                               "    �������������������������    \n"
                               "                                 \n"
                               "FreeDOS Edit is a clone of MS-DOS\n"
                               "editor for the FreeDOS Project   \n"
                               "Released under the GNU GPL License";
                    if (strchr(aboutMsg,'@') != NULL)
                        strncpy(strchr(aboutMsg,'@'), VERSION,
                            strlen(VERSION));
                        /* intentionally not terminating after VERSION! */
                    MessageBox("About FreeDOS Edit", aboutMsg);
                    }
		    return TRUE;
		default:
		    break;

                }
	    break;
	default:
	    break;
        }

    return DefaultWndProc(wnd, msg, p1, p2);

}

/* The New command. Open an empty editor window */
static void NewFile(WINDOW wnd, char *FileName)
{
    OpenPadWindow(wnd, Untitled,FileName);
}

/* --- The Open... command. Select a file  --- */
static void SelectFile(WINDOW wnd)
{
    char FileName[64];

    if (OpenFileDialogBox("*.*", FileName))
        {
        /* See if the document is already in a window */
	WINDOW wnd1 = FirstWindow(wnd);

        while (wnd1 != NULL)
            {
	    if (stricmp(FileName, wnd1->extension) == 0)
                {
		SendMessage(wnd1, SETFOCUS, TRUE, 0);
		SendMessage(wnd1, RESTORE, 0, 0);
		return;
                }

            wnd1 = NextWindow(wnd1);
            }

        OpenPadWindow(wnd, FileName,NULL);
        }

}

/* --- open a document window and load a file --- */
static void OpenPadWindow(WINDOW wnd, char *FileName,char *NewFileName)
{
    static WINDOW wnd1 = NULL;
    WINDOW wwnd;
    char *Fname = FileName, *ermsg;
/*	char *Fnewname = NewFileName; */ /* ??? why is this here, not used */

    if (strcmp(FileName, Untitled))
        {
	/* AMUNOS: stat 桩返回 -1, 改用 fopen+fseek/ftell 检查存在与大小 */
	FILE *fp = fopen(FileName, "rb");
	if (fp == NULL)
            {
	    ermsg = DFmalloc(strlen(FileName)+20);
	    strcpy(ermsg, "No such file:\n");
	    strcat(ermsg, FileName);
	    ErrorMessage(ermsg);
	    free(ermsg);
	    return;
            }

	Fname = NameComponent(FileName);

        /* check file size */
        fseek(fp, 0L, SEEK_END);
        if (ftell(fp) > 64000L)
            {
            ermsg = DFmalloc(strlen(FileName)+100); /* alloc fixed 0.7a */
	    strcpy(ermsg, "File too large for this version of Edit:\n");
	    strcat(ermsg, FileName);
	    ErrorMessage(ermsg);
	    fclose(fp);
	    free(ermsg);
	    return;
            }
        fclose(fp);

        } /* actual filename given */

    wwnd = WatchIcon();
    wndpos += 2;
    if (NewFileName != NULL)
        Fname = NameComponent(NewFileName);

    if (wndpos == 20)
        wndpos = 2;

    wnd1 = CreateWindow(EDITBOX,
		Fname,
		(wndpos-1)*2, wndpos, 10, 40,
		NULL, wnd, PadWndProc,
		SHADOW     |
		MINMAXBOX  |
		CONTROLBOX |
		VSCROLLBAR |
		HSCROLLBAR |
		MOVEABLE   |
		HASBORDER  |
		SIZEABLE   |
                MULTILINE);

    if (cfg.ReadOnlyMode)		/* new feature in 0.7b */
        AddAttribute(wnd1, READONLY);
        /* needed because ReadOnlyMode must not make ALL text */
        /* entry fields read only, only EDITBOXes become r/o! */

    /* suggested code change to ix saveas bug -
       contrib: James Sandys-Lumsdaine 

    OLD CODE SEGMENT! 

    if (strcmp(FileName, Untitled))    {
	wnd1->extension = DFmalloc(strlen(FileName)+1);
	strcpy(wnd1->extension, FileName);
	LoadFile(wnd1);
    }

    NEW CODE SEGMENT!
    */

    if (NewFileName != NULL)
        {
	/* Either a command line new file or one that's on the 
	disk to load - Either way, must set the extension
	to the given filename */

	wnd1->extension = DFmalloc(strlen(NewFileName) + 1);
	strcpy(wnd1->extension,NewFileName);
        }
    else
        {
        if (strcmp(FileName,Untitled))
            wnd1->extension = DFmalloc(strlen(FileName)+1);

	strcpy(wnd1->extension, FileName);
	LoadFile(wnd1); /* Only load if not a new file */
        }

    SendMessage(wwnd, CLOSE_WINDOW, 0, 0);
    SendMessage(wnd1, SETFOCUS, TRUE, 0);
    SendMessage(wnd1, MAXIMIZE, 0, 0); 

}

/* --- Load the notepad file into the editor text buffer --- */
static void LoadFile(WINDOW wnd)
{
    char *Buf = NULL;
    unsigned int recptr = 0;
    FILE *fp;

    if (!strcmp(wnd->extension, Untitled))
    {
      SendMessage(wnd, SETTEXT, (PARAM) "", 0); /* fill with empty string */
      /* could show a messagebox of some kind here */
      return;
    } /* not a real file load */

    if ((fp = fopen(wnd->extension, "rt")) != NULL) /* why "t"ext mode? */
    {
        /* (could use ExpandTabs() here alternatively!?) */
        int expandTabs = -1;
        int theColumn = 0;
        unsigned int i;
        unsigned int rmax;
        while (!feof(fp))
        {
            handshake();			/* let messages flow */
            rmax = 1024;
            Buf = DFrealloc(Buf, recptr+rmax);	/* inflate buffer */
            memset(Buf+recptr, 0, rmax);	/* clear new area */
            {
                /* AMUNOS: libc 无 fgets, 用 fgetc 读一行 (≤511 字符, EOF/换行停) */
                int k = 0, ch;
                while (k < 511 && (ch = fgetc(fp)) != -1)
                {
                    Buf[recptr+k] = (char)ch;
                    k++;
                    if (ch == '\n')
                        break;
                }
                Buf[recptr+k] = '\0';   /* memset 已清零, 这里兜底 */
            }
            if ( (expandTabs == -1) && (cfg.Tabs > 1) &&
                 (strchr(Buf+recptr,'\t') != NULL) )
            {
                char tMsg[200];
                sprintf(tMsg,"Tabs detected in\n%s\nExpand them at tab width %d?",
                    (strlen(wnd->extension)>120) ? "file" : wnd->extension,
                    cfg.Tabs);
                expandTabs = (YesNoBox(tMsg))
                   ? 1 : 0;
            }
            for (i=0; Buf[recptr+i]; i++)
            {
                switch (Buf[recptr+i])
                {
                    case '\r':
                    case '\n':
                        theColumn = 0;
                        break;
                    /* backspace intentionally not handled */
                    case '\t':
                        if (expandTabs == 1)
                        {
	                    if ((strlen(Buf+recptr)+cfg.Tabs+8 /* 5 */) >= rmax)
	                    /* changed extra offset from 5 to 8 for 0.7b */
        	            {
                	        rmax += 512;
	                        Buf = DFrealloc(Buf, recptr+rmax);
        	                memset(Buf+recptr+rmax-512, 0, 512);
                	    };
                            {   /* limit scope of j */
                                int j = cfg.Tabs - (theColumn % cfg.Tabs);
                                /* move by dist. to next tab, pad with ' ' */
/* BROKEN 0.7a version:
                                strcpy(Buf+recptr+i+j, Buf+recptr+i+1);
 */ /* Fixed 0.7b version: */
				memmove(Buf+recptr+i+j, Buf+recptr+i+1, rmax-i-j);
/* end of 0.7b fix */
                                /* ... +1 as we do not copy the \t itself */
                                memset(Buf+recptr+i, ' ', j);
                                theColumn += j;
                                i += j; /* do not read padding again */
                                i--;	/* because of i++ in the loop */
                            };
                        } else
                            theColumn++; /* do not expand */
                        break;
                    default:
                        theColumn++;
                } /* switch */
            } /* for */
            recptr += strlen(Buf+recptr);	/* add read-len */
        } /* while not eof */

	fclose(fp);
        if (Buf != NULL)
        {
            SendMessage(wnd, SETTEXT, (PARAM) Buf, 0); /* paste read text */
            free(Buf); /* buffer no longer needed */
        }
        /* else ran out of memory? */

    }
    else
    {
        char fMsg[200];
        sprintf(fMsg,"Could not load %s",
                    (strlen(wnd->extension)>120) ? "file" : wnd->extension);
        ErrorMessage(fMsg);
    }

}

/* ---------- save a file to disk ------------ */
static void SaveFile(WINDOW wnd, int Saveas)
{
    FILE *fp;

    if (wnd->extension == NULL || Saveas) /* ask for new name? */
        {
        char FileName[64];

	FileName[0] = 0;
	trySaveAgain:	/* moved label up in 0.7c */

        if (SaveAsDialogBox("*.*", NULL, FileName))
            {
            if (wnd->extension != NULL)
                free(wnd->extension);

            wnd->extension = DFmalloc(strlen(FileName)+1);
            strcpy(wnd->extension, FileName);
            AddTitle(wnd, NameComponent(FileName));
            SendMessage(wnd, BORDER, 0, 0);
            }
        else
            {
            ErrorMessage("No name given - not saved.");
            return; /* abort if no name provided by user */
            }
        }

    if (wnd->extension != NULL)	/* if there is a filename for the window */
        {
        WINDOW mwnd;
        /* trySaveAgain: */
        mwnd = MomentaryMessage("Saving...");

        if ((fp = fopen(wnd->extension, "wt")) != NULL)
            {
            /* could use CollapseTabs() here if user wants us to do so!? */
            size_t howmuch = strlen(GetText(wnd));
            howmuch = fwrite(GetText(wnd), howmuch, 1, fp); /* ONE item, SIZE howmuch */
            fclose(fp);
            SendMessage(mwnd, CLOSE_WINDOW, 0, 0);
            if (howmuch != 1) /* ONE item actually written? */
                {
                if (YesNoBox("Ran out of disk space while saving. Try again?"))
                    {
                    Saveas = 1;	/* 0.7c: ask user for a new place for next try */
                    goto trySaveAgain;
                    }
                }
            else
                wnd->TextChanged = FALSE;	/* give up */
            }
        else
            {
            char fMsg[200];
            SendMessage(mwnd, CLOSE_WINDOW, 0, 0);
            sprintf(fMsg,"Could not save %s, try again?",
                    (strlen(wnd->extension)>120) ? "file" : wnd->extension);
            if (YesNoBox(fMsg))
                {
                Saveas = 1;	/* 0.7c: ask user for a new place for next try */
                goto trySaveAgain;
                }
            }
        } /* if any file loaded */
}

/* -------- delete a file ------------ */
#ifdef DELFILE
static void DeleteFile(WINDOW wnd)
{
    if (wnd->extension != NULL)    {
	if (strcmp(wnd->extension, Untitled))    {
	    char *fn = NameComponent(wnd->extension);
	    if (fn != NULL)    {
		char msg[150];
		sprintf(msg, "Delete %s?", (strlen(fn)>100) ? "file" : fn);
		if (YesNoBox(msg)) {
		    unlink(wnd->extension);
		    SendMessage(wnd, CLOSE_WINDOW, 0, 0);
		}
	    }
	}
    }
}
#endif

/* ------ display the row and column in the statusbar ------ */
static void ShowPosition(WINDOW wnd)
{
    /* This is where we place the "INS" display */
    char status[40], *InsModeText;
    if (wnd->InsertMode)
        {
        InsModeText = "INS ";           /* Not on */
        }
    else
        {
        InsModeText = "OVER";           /* "Insert" (Overtype!?) is on */
        }

    if (WindowWidth(wnd) < 50) /* auto-condense new in EDIT 0.7 */
        {
        sprintf(status, "%c %c Li:%d Co:%d", 
            (cfg.ReadOnlyMode) ? 'R' : (wnd->TextChanged ? '*' : ' '),
            InsModeText[0], (wnd->CurrLine)+1, (wnd->CurrCol)+1);
            /* 1-based column / row are nicer for humans (EDIT 0.7b) */
            /* new flag char R for readonly added 0.7b */
        }
    else
        sprintf(status, "%c %4s  Line: %4d  Col: %3d ",
            (cfg.ReadOnlyMode) ? 'R' : (wnd->TextChanged ? '*' : ' '),
            InsModeText, (wnd->CurrLine)+1, (wnd->CurrCol)+1);
            /* 1-based column / row are nicer for humans (EDIT 0.7b) */
            /* new flag char R for readonly added 0.7b */
    SendMessage(GetParent(wnd), ADDSTATUS, (PARAM) status, 0);

}

/* ----- window processing module for the editboxes ----- */
static int PadWndProc(WINDOW wnd,MESSAGE msg,PARAM p1,PARAM p2)
{
    int rtn;
    switch (msg)    {
	case SETFOCUS:
			if ((int)p1)    {
				wnd->InsertMode = GetCommandToggle(&MainMenu, ID_INSERT);
				wnd->WordWrapMode = GetCommandToggle(&MainMenu, ID_WRAP);
			}
	    rtn = DefaultWndProc(wnd, msg, p1, p2);
	    if ((int)p1 == FALSE)
		SendMessage(GetParent(wnd), ADDSTATUS, 0, 0);
	    else 
		ShowPosition(wnd);
	    return rtn;
	case KEYBOARD_CURSOR:
	    rtn = DefaultWndProc(wnd, msg, p1, p2);
	    ShowPosition(wnd);
	    return rtn;
	case COMMAND:
	    if (cfg.ReadOnlyMode && TestAttribute(wnd, READONLY)) {
    	    	/* read only mode added 0.7b */
	        switch ((int)p1) {
	            case ID_REPLACE:
	            case ID_CUT:
	            case ID_PASTE:
	            case ID_DELETETEXT:
	            case ID_CLEAR:
	            case ID_PARAGRAPH:
	                beep();
	                return TRUE;	/* consume event */
	        }
	    }
		switch ((int) p1)       {
			case ID_SEARCH:
				SearchText(wnd);
				return TRUE;
			case ID_REPLACE:
				ReplaceText(wnd);
				return TRUE;
			case ID_SEARCHNEXT:
				SearchNext(wnd);
				return TRUE;
			case ID_CUT:
				CopyToClipboard(wnd);
				SendMessage(wnd, COMMAND, ID_DELETETEXT, 0);
				SendMessage(wnd, PAINT, 0, 0);
				return TRUE;
			case ID_COPY:
				CopyToClipboard(wnd);
				ClearTextBlock(wnd);
				SendMessage(wnd, PAINT, 0, 0);
				return TRUE;
			case ID_PASTE:
				PasteFromClipboard(wnd);
				SendMessage(wnd, PAINT, 0, 0);
				return TRUE;
			case ID_DELETETEXT:
			case ID_CLEAR:
				rtn = DefaultWndProc(wnd, msg, p1, p2);
				SendMessage(wnd, PAINT, 0, 0);
				return rtn;
			case ID_HELP:
				DisplayHelp(wnd, "MEMOPADDOC");
				return TRUE;
			case ID_WRAP:
				SendMessage(GetParent(wnd), COMMAND, ID_WRAP, 0);
				wnd->WordWrapMode = cfg.WordWrap;
				return TRUE;
			case ID_INSERT:
				SendMessage(GetParent(wnd), COMMAND, ID_INSERT, 0);
				wnd->InsertMode = cfg.InsertMode;
				SendMessage(NULL, SHOW_CURSOR, wnd->InsertMode, 0);
				return TRUE;
			default:
				break;
	    	} 	/* end of switch int p1 */
	    break;	/* end of case COMMAND  */
	case CLOSE_WINDOW:
	    if (wnd->TextChanged)
            {
            char *cp;
            BOOL saveAsFlag = 0;
            cp = DFmalloc(75+strlen(GetTitle(wnd)));
            strcpy(cp, "             The file\n            '");
            strcat(cp, GetTitle(wnd));
            strcat(cp, "'\nhas not been saved yet.  Save it now?");

            saveAsOnClose:
            SendMessage(wnd, SETFOCUS, TRUE, 0);
            if (YesNoBox(cp)) {            
                SendMessage(GetParent(wnd), COMMAND,
                    (saveAsFlag ? ID_SAVEAS : ID_SAVE), 0);
                if (wnd->TextChanged) { /* still unsaved changes? */
                  ErrorMessage("File could not be saved! Try to save elsewhere.");
                  saveAsFlag = 1;
                  goto saveAsOnClose; /* do not let user leave yet */
                } /* still unsaved */
            } /* user selected "yes", save before closing window */
            free(cp);
            } /* modified file - suggested to save */

        wndpos = 0;
	    if (wnd->extension != NULL)
            {
            free(wnd->extension);
            wnd->extension = NULL;
            }
	    break;
	default:
	    break;
    }
    return DefaultWndProc(wnd, msg, p1, p2);
}

/* -- point to the name component of a file specification -- */
static char *NameComponent(char *FileName)
{
    char *Fname;
    if ((Fname = strrchr(FileName, '/')) == NULL)   /* AMUNOS 用 '/' */
	if ((Fname = strrchr(FileName, '\\')) == NULL)
	    if ((Fname = strrchr(FileName, ':')) == NULL)
		Fname = FileName-1;
    return Fname + 1;
}

static void FixTabMenu(void)
{
	char *cp = GetCommandText(&MainMenu, ID_TABS);
	if (cp != NULL) {
		cp = strchr(cp, '(');
		if (cp != NULL) {
			*(cp+1) = (cfg.Tabs>1) ? (cfg.Tabs + '0') : '-';
			if (GetClass(inFocus) == POPDOWNMENU)
				SendMessage(inFocus, PAINT, 0, 0);
		}
	}
}

/* Prep....Menu are called to activate drop-downs in the main menu bar */
void PrepFileMenu(void *w, struct Menu *mnu)
{
    WINDOW wnd = w;

    if (mnu != NULL) {}; /* unused parameter! */

    DeactivateCommand(&MainMenu, ID_SAVE);
    DeactivateCommand(&MainMenu, ID_SAVEAS);
    DeactivateCommand(&MainMenu, ID_CLOSE);
/*  DeactivateCommand(&MainMenu, ID_DELETEFILE); */
    DeactivateCommand(&MainMenu, ID_PRINT);

    if (cfg.ReadOnlyMode) {		/* new in 0.7b */
        DeactivateCommand(&MainMenu, ID_NEW);
        DeactivateCommand(&MainMenu, ID_DOS); /* make viewer mode "safe" */
    }

    if (wnd != NULL && GetClass(wnd) == EDITBOX)
        {
        if (isMultiLine(wnd))
            {
            if (!cfg.ReadOnlyMode) {	/* new in 0.7b */
                ActivateCommand(&MainMenu, ID_SAVE);
                ActivateCommand(&MainMenu, ID_SAVEAS);
            }
            ActivateCommand(&MainMenu, ID_CLOSE);
/*          ActivateCommand(&MainMenu, ID_DELETEFILE);  */
            ActivateCommand(&MainMenu, ID_PRINT);
            }

	}

}

void PrepSearchMenu(void *w, struct Menu *mnu)
{
    WINDOW wnd = w;

    if (mnu != NULL) {}; /* unused parameter! */

    DeactivateCommand(&MainMenu, ID_SEARCH);
    DeactivateCommand(&MainMenu, ID_REPLACE);
    DeactivateCommand(&MainMenu, ID_SEARCHNEXT);

    if (wnd != NULL && GetClass(wnd) == EDITBOX) {
	if (isMultiLine(wnd))   {
	    ActivateCommand(&MainMenu, ID_SEARCH);
            if (!cfg.ReadOnlyMode) {	/* new in 0.7b */
	        ActivateCommand(&MainMenu, ID_REPLACE);
	    }
	    ActivateCommand(&MainMenu, ID_SEARCHNEXT);
	}
    }
}

void PrepEditMenu(void *w, struct Menu *mnu)
{
    WINDOW wnd = w;

    if (mnu != NULL) {}; /* unused parameter! */

    DeactivateCommand(&MainMenu, ID_CUT);
    DeactivateCommand(&MainMenu, ID_COPY);
    DeactivateCommand(&MainMenu, ID_CLEAR);
    DeactivateCommand(&MainMenu, ID_DELETETEXT);
    DeactivateCommand(&MainMenu, ID_PARAGRAPH);
    DeactivateCommand(&MainMenu, ID_PASTE);
    DeactivateCommand(&MainMenu, ID_UNDO);
    DeactivateCommand(&MainMenu, ID_UPCASE);	/* new in 0.7d */
    DeactivateCommand(&MainMenu, ID_DOWNCASE);	/* new in 0.7d */
    ActivateCommand(&MainMenu, ID_WORDCOUNT);	/* new in 0.7d */
    if (wnd != NULL && GetClass(wnd) == EDITBOX) {
	if (isMultiLine(wnd) &&
	    (!cfg.ReadOnlyMode)) {	/* new mode in 0.7b */

	    if (TextBlockMarked(wnd))       {
		ActivateCommand(&MainMenu, ID_CUT);
		ActivateCommand(&MainMenu, ID_COPY);
		ActivateCommand(&MainMenu, ID_CLEAR);
		ActivateCommand(&MainMenu, ID_DELETETEXT);
                ActivateCommand(&MainMenu, ID_UPCASE); /* new in 0.7d */
                ActivateCommand(&MainMenu, ID_DOWNCASE); /* new in 0.7d */
	    }
	    ActivateCommand(&MainMenu, ID_PARAGRAPH);
	    if (!TestAttribute(wnd, READONLY) &&
		Clipboard != NULL)
		ActivateCommand(&MainMenu, ID_PASTE);
	    if (wnd->DeletedText != NULL)
		ActivateCommand(&MainMenu, ID_UNDO);
	} /* editable non-empty EDITBOX */
    }
}


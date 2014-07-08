/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWtools!src:cparse.c(EBDSDK_P.1) $ */

/*
* Log stripped */

/*
 * CPARSE.C
 *
 * Author:  Derek Knight
 * Date:        10 March 1993
 *
 *  Purpose: Parse a C-file - Either producing a list of the files it includes
 *           or producing a listing of the full file and its children
 */

/* --------------------------- Includes ------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MACOSX
#include <ctype.h>
#endif

#if defined(WIN32) || defined(IBMPC)
#include <dos.h>
#include <ctype.h>
#include <io.h>
#endif

#ifdef linux
#include <ctype.h>
#endif

#include "cparse.h"


/* --------------------------- Macros -------------------------------------- */

/* #define DEBUG */

/* define find_t for lower version compilers*/

#ifndef _MSC_VER
#define _find_t find_t
#endif

#ifndef MAX_IFDEF_DEPTH
#define MAX_IFDEF_DEPTH 64
#endif  /* !MAX_IFDEF_DEPTH */

#ifndef MAX_LINE
#define MAX_LINE 256
#endif  /* !MAX_LINE */

#ifndef MAX_IDENTIFIER
#define MAX_IDENTIFIER 64
#endif  /* !MAX_IDENTIFIER */

#ifndef MAX_PATHNAME
#define MAX_PATHNAME 128
#endif  /* !MAX_PATHNAME */

#define EOL_CHAR( c ) ( (c) == '\n' || (c) == '\r' )

#if !defined(WIN32) && !defined(IBMPC)
int iscsymf(char c)
{
	if (((c >= 'a') && (c <= 'z')) ||
	    ((c >= 'A') && (c <= 'Z')) ||
	     (c == '_'))
	  return((int) c);
    else
      return 0;
 }
 
int iscsym(char c)
{
 	if (iscsymf(c))
 		return c;
 	else if ((c >= '0') && (c <= '9'))
 		return c;
 	else
 		return 0;
}
#endif
 

/* --------------------------- Types --------------------------------------- */

typedef struct Include_s
{
	char Path [MAX_PATHNAME];
	Bool_t Global;
	struct Include_s *next;
} Include_t;

typedef struct Define_s
{
	char *Name;
	char *Value;
	int Length;
	Bool_t State;
	Bool_t Global;
	struct Define_s *next;
} Define_t;

typedef struct Macro_s
{
	char *Name;
	char *Params;
	char *Expand;
	int Length;
	Bool_t State;
	Bool_t Global;
	struct Macro_s *next;
} Macro_t;

typedef struct Parse_s
{
	char *Atom;
	int Length;
	Bool_t PackWhite;
	int Short;
	int Long;
	Bool_t Translate;
	int (*Action)(char *Keyword, char *ROL, HowParse_t how);
} Parse_t;

typedef struct Reduction_s
{
	char *Atom;
	int Length;
	char Reduce;
	int Priority;
} Reduction_t;

int ParseLine (char *Keyword, HowParse_t how);
int ParseInclude (char *Keyword, char *ROL, HowParse_t how);
int ParseDefine (char *Keyword, char *ROL, HowParse_t how);
int ParseUndef (char *Keyword, char *ROL, HowParse_t how);
int ParseIfdef (char *Keyword, char *ROL, HowParse_t how);
int ParseIfndef (char *Keyword, char *ROL, HowParse_t how);
int ParseIf (char *Keyword, char *ROL, HowParse_t how);
int ParseElse (char *Keyword, char *ROL, HowParse_t how);
int ParseEndif (char *Keyword, char *ROL, HowParse_t how);
char *ParseIfStatement (char *Line);
char *ReduceCondition (char *Line);
char *ReduceOneZero (char *Line);
char *ExpandDefines (char *Line);
char *ExpandMacros (char *Line);
void StringReplace (char *Line, char *Where, char *To, char *From);
char *GetNextChar (char *Line, Bool_t WhiteSpace);
char *FindIncludeFile (char *Name, Bool_t Global);
Bool_t GetDefine (char *Name);
char *ValueDefine (char *Name);
void FreeDefines (Bool_t Global);
void SetMacro (char *Name, char *Params, char *Expand, Bool_t State, Bool_t Global, HowParse_t how);
Bool_t GetMacro (char *Name);
void UnsetMacro (char *Name);
void FreeMacros (Bool_t Global);
void WriteOut (char *Text);
void WriteAppend (char *Text);
void WriteNewLine (void);
void WriteTabs (void);
Bool_t stringCopy(char* target, char* source, int targetBufferSize);
Bool_t stringAppend(char* target, char* source, int targetBufferSize);

Include_t *Includes = NULL;
Define_t *Defines = NULL;
Macro_t *Macros = NULL;
Bool_t SystemIncludes = FALSE;
char SaveLine [MAX_LINE];
int OutLength = 0;
FILE *OutChannel = NULL;
Bool_t SkipIfdef[MAX_IFDEF_DEPTH];
Bool_t InComment = FALSE;
int IfdefDepth = 0;
int DefineCount = 0;
int MacroCount = 0;
Bool_t Skipping = FALSE;
Bool_t AllocFailed = FALSE;
Bool_t DoubleSlashComments = FALSE;
Bool_t UseDefinesValues = FALSE;
Parse_t ParseList [] =
{
  { "include", 7, TRUE, 3, 22, FALSE, ParseInclude },
  { "define", 6, TRUE, 1, MAX_IDENTIFIER, FALSE, ParseDefine },
  { "undef", 5, TRUE, 1, MAX_IDENTIFIER, FALSE, ParseUndef },
  { "ifdef", 5, TRUE, 1, MAX_IDENTIFIER, FALSE, ParseIfdef },
  { "ifndef", 6, TRUE, 1, MAX_IDENTIFIER, FALSE, ParseIfndef },
  { "if", 2, FALSE, 1, MAX_LINE, FALSE, ParseIf },
  { "else", 4, TRUE, 0, MAX_IDENTIFIER, FALSE, ParseElse },
  { "endif", 5, TRUE, 0, MAX_IDENTIFIER, FALSE, ParseEndif }
};
#define NParseList (sizeof (ParseList) / sizeof (ParseList [0]))
Reduction_t ReductionList [] =
{
	{"!0", 2, '1', 0},
	{"!1", 2, '0', 0},
	{"0||0", 4, '0', 1},
	{"0||1", 4, '1', 1},
	{"1||0", 4, '1', 1},
	{"1||1", 4, '1', 1},
	{"0&&0", 4, '0', 1},
	{"0&&1", 4, '0', 1},
	{"1&&0", 4, '0', 1},
	{"1&&1", 4, '1', 1},
	{"(0)", 3, '0', 2},
	{"(1)", 3, '1', 2}
};
#define NReductionList (sizeof (ReductionList) / sizeof (ReductionList [0]))
#define NPriority 3

#if defined(WIN32) || defined(IBMPC)
/* return number of files parsed or -1 if error */
int ParseFiles (char *name, char *objects, char *inputs, HowParse_t how)
{
#ifdef  WIN32
	struct _finddata_t FindStruct;
	long fHandle;
#else   /* IBMPC */
	struct _find_t FindStruct;
#endif
	int Found;
	int Count = 0, i;
	char Path [MAX_PATHNAME];
	char *RPos, *OPos, *DPos, *IPos;
	char Object [MAX_PATHNAME], Input [MAX_PATHNAME];

	stringCopy(Path, name, MAX_PATHNAME);
	RPos = strrchr (Path, '\\');
	if (!RPos) RPos = Path; else RPos++;
	if (objects)
	{
		stringCopy(Object, objects, MAX_PATHNAME);
		if (Object[strlen (Object)-1] != '\\') stringAppend(Object, "\\", MAX_PATHNAME);
		OPos = &Object[strlen (Object)];
	}
	else
	{
		stringCopy(Object, Path, MAX_PATHNAME);
		OPos = strrchr (Object, '\\');
		if (!OPos) OPos = Object; else OPos++;
	}
	if (inputs)
	{
		stringCopy(Input, inputs, MAX_PATHNAME);
		if (Input[strlen (Input)-1] != '\\') stringAppend(Input, "\\", MAX_PATHNAME);
		IPos = &Input[strlen (Input)];
	}
	else
	{
		stringCopy(Input, Path, MAX_PATHNAME);
		IPos = strrchr (Input, '\\');
		if (!IPos) IPos = Input; else IPos++;
	}
#ifdef  WIN32
	fHandle = _findfirst (name, &FindStruct);
	Found = fHandle == -1 ? -1 : 0;
#else   /* IBMPC */
	Found = _dos_findfirst (name, 0, &FindStruct);
#endif

	while (Found == 0)
	{
		char *c = strlwr (FindStruct.name);

		strcpy (IPos, c);
		if (how == E_Depends)
		{
			strcpy (RPos, c);
			strcpy (OPos, c);
			DPos = strchr (OPos, '.');
			if (DPos) *DPos = '\0';
			stringAppend(Object, ".obj: ", MAX_PATHNAME);
			WriteAppend(Object);
			WriteAppend (Input);
		}
		else if (how == E_ExpandPS)
		{
			strcpy (OPos, c);
			strcpy (RPos, c);
			DPos = strchr (OPos, '.');
			if (DPos) *DPos = '\0';
			stringAppend(Object, ".psb", MAX_PATHNAME);
			SetOutput (Object, "%", "");
		}

		IfdefDepth = 0;
		Skipping = FALSE;
		InComment = FALSE;
		AllocFailed = FALSE;

		i = ParseFile (Path, how);

		if ( i >=0 )
			fprintf( stderr, "%s: Parsed %d lines\n", Path, i );
		else
			fprintf( stderr, "%s: Parse error", Path );
#ifdef  DEBUG
		fprintf (stderr, "Define Count %d\n", DefineCount);
#endif  /* DEBUG */
		FreeDefines (FALSE);
		FreeMacros (FALSE);

		if (how == E_Depends)
		{
			WriteNewLine ();
			WriteNewLine ();
		}
		else if (how == E_ExpandPS)
		{
			EndOutput ();
		}

		if ( i < 0 ) return -1;
		Count++;

#ifdef  WIN32
		Found = _findnext (fHandle, &FindStruct);
#else   /* IBMPC */
		Found = _dos_findnext (&FindStruct);
#endif
	}
	return Count;
}
#endif

/* return number of lines parsed or -1 if error */
int ParseFile( char * name, HowParse_t how )
{
  FILE *        ParseChannel;
  int           count = 0;
  char          line[ MAX_LINE ];
  char *        pEnd = line;
  char *        p;

  if (name) {
    ParseChannel = fopen(name, "r");
    if (!ParseChannel) {
      sprintf(line, "Open error (%s)", name);
      perror(line);
      return -1;
    }
  }
  else {
    ParseChannel = stdin;
  }

  while
  (
   ( p = fgets( pEnd, MAX_LINE - ( pEnd - line ), ParseChannel ) ) != NULL
   || pEnd != line
  )
  {
    char *      pStart;

    if ( p == NULL ) *pEnd = '\0'; else count++;
    pStart = pEnd;
    pEnd += strlen( line );

    if ( pEnd - line >= MAX_LINE - 1 )
    {
      sprintf( line, "Error: file %s line %d too long", name, count );
      perror( line );
      return -1;
    }

    /* Concatenate next line if this line ends in backslash */
    p = pEnd;
    while ( p > pStart && EOL_CHAR( *(p-1) ) ) p--;
    if ( p > pStart && ( *(p-1) == '\\' ) ) continue;

    pEnd = line;
    if ( ParseLine( line, how ) < 0 ) return -1;
  }
  fclose (ParseChannel);
  return count;
}

/*  return 0 for success -1 for error */
int ParseLine (char *Line, HowParse_t how)
{
	char *c;
	int i, j, Len;
	Parse_t *Parse;

	stringCopy(SaveLine, Line, MAX_LINE);
	Line = GetNextChar (Line, FALSE);
	if (InComment) return 0;
	if (!Line) return 0;
	if (*Line != '#')
	{
		if (how == E_ExpandPS && !Skipping)
		{
			char *ExpLine = ExpandMacros (SaveLine);
			ExpLine = ExpandDefines (ExpLine);
			WriteOut (ExpLine);
		}
                return 0;
	}
	Line [strlen (Line)+1] = '\0';  /* Add an extra NULL (for ROL)*/

        /* skip # and any following whitespace */
        Line++;
        while ( *Line && isspace( *Line ) ) Line++;

	c = Line;
	while ( *c && !isspace (*c) ) c++;
	Len = c - Line;
	*c++ = '\0';
	for (i = 0; (unsigned)i < NParseList; i++)
	{
		Parse = &ParseList [i];
		if (Parse->Length == Len && strcmp (Parse->Atom, Line) == 0)
		{
			char Keyword [MAX_LINE], *r = Keyword, *k = r;
			char Value [MAX_LINE], *v = Value;
			while (*c && isspace (*c))c++;
			for (j = 0; j < Parse->Long; j++)
			{
				c = GetNextChar (c, Parse->PackWhite);
				if (!c) break;
				if (!Parse->PackWhite) *(r++) = *(c++);
				else if (*c != ' ') *(r++) = *(c++);
				else break;
			}
			if (j < Parse->Short) continue;
			*r = '\0';
			if (c)
			{
				while (*c && isspace (*c)) c++;
				for (j = 0; j < MAX_LINE; j++)
				{
					c = GetNextChar( c, TRUE );
					if (!c) break;
					*(v++) = *(c++);
				}
				*v = '\0';
				v = Value;
			}
			return Parse->Action (Keyword, Value, how);
		}
	}
	return 0;
}

char * GetNextChar( char * Line, Bool_t WhiteSpace )
{
  char *        p = Line;
  char          c;

  while ( ( c = *p++ ) != 0 )
  {
    if ( InComment && c == '*' && *p == '/' )
    {
      InComment = FALSE;
      p++;
      continue;
    }
    if ( InComment ) continue;

    if ( EOL_CHAR( c ) ) continue;
    if ( c == '\\' && EOL_CHAR( *p ) ) continue;

    if ( isspace( c ) )
    {
      if ( !WhiteSpace ) continue;
      else *(p-1) = ' ';
    }

    if ( c == '/' )
    {
      if ( *p == '*' )
      {
        InComment = TRUE;
        p++;
        continue;
      }
      if ( *p == '/' && DoubleSlashComments ) return NULL;
    }

    return p-1;
  }
  return NULL;
}



/* -------------------- Pre-processor command parsers -------------------- */
/* return 0 if success -1 if error */
int ParseInclude (char *Keyword, char *ROL, HowParse_t how)
{
	char *File = NULL, *c;
	Bool_t Global;

	if (Skipping) return 0;
	Global = Keyword [0] == '<';
	for (c = Keyword+2; *c; c++)
		if (*c == '"' || *c == '>')
		{
			*c = '\0';
			break;
		}
	if (!Global || SystemIncludes)
	{
		char SavePath [MAX_IDENTIFIER];

#ifdef  DEBUG
		fprintf (stderr, "Include -%s- %s\n", &Keyword[1], Global ? "GLOBAL" : "LOCAL");
#endif  /* DEBUG */
		File = FindIncludeFile (&Keyword[1], Global);
		if (File == NULL)
		{
			fprintf( stderr, "Missing include file %s\n", &Keyword[1] );
			return -1;
		}

		stringCopy(SavePath, File, MAX_IDENTIFIER);
		if (how == E_Depends) WriteAppend (SavePath);
		return( ParseFile( SavePath, how ) < 0 ) ? -1 : 0;
	}
	return 0;
}

/* return 0 if success -1 if error */
int ParseDefine( char * Keyword, char * ROL, HowParse_t how )
{
  char *        b;

  if ( Skipping ) return 0;

  b = strchr( Keyword, '(' );  
  if ( b )
  {
    char        Macro[ MAX_LINE ];
    char        Expand[ MAX_LINE ];
    char *      m = Macro;
    char *      e = Expand;

    stringCopy(Macro, b, MAX_LINE);
    stringAppend(Macro, ROL, MAX_LINE);
    *b = '\0';
    b = strchr( Macro, ')' );
    stringCopy(Expand, ++b, MAX_LINE);
    *b = '\0';
#ifdef  DEBUG
    fprintf( stderr, "Macro -%s-%s-%s\n", Keyword, Macro, Expand );
#endif  /* DEBUG */
    SetMacro( Keyword, Macro, Expand, TRUE, FALSE, how );
  }
  else
  {
#ifdef  DEBUG
    fprintf( stderr, "Define -%s-%s-\n", Keyword, ROL );
#endif  /* DEBUG */
    SetDefine( Keyword, ROL, TRUE, FALSE, how );
  }
  return 0;
}

/* return 0 if success -1 if error */
int ParseUndef (char *Keyword, char *ROL, HowParse_t how)
{
	if (Skipping) return 0;
#ifdef  DEBUG
	fprintf (stderr, "Undefine -%s-\n", Keyword);
#endif  /* DEBUG */
	UnsetDefine (Keyword);
	return 0;
}

/* return 0 if success -1 if error */
int ParseIfdef (char *Keyword, char *ROL, HowParse_t how)
{
	IfdefDepth++;
	SkipIfdef [IfdefDepth] = Skipping || !GetDefine (Keyword);
	Skipping = SkipIfdef [IfdefDepth];
#ifdef  DEBUG
	fprintf (stderr, "Ifdef -%s- %d %s\n", Keyword, IfdefDepth, Skipping ? "SKIP" : "USE");
#endif  /* DEBUG */
	return 0;
}

/* return 0 if success -1 if error */
int ParseIfndef (char *Keyword, char *ROL, HowParse_t how)
{
	IfdefDepth++;
	SkipIfdef [IfdefDepth] = Skipping || GetDefine (Keyword);
	Skipping = SkipIfdef [IfdefDepth];
#ifdef  DEBUG
	fprintf (stderr, "Ifndef -%s- %d %s\n", Keyword, IfdefDepth, Skipping ? "SKIP" : "USE");
#endif  /* DEBUG */
	return 0;
}

/* return 0 if success -1 if error */
int ParseIf (char *Keyword, char *ROL, HowParse_t how)
{
	while (ParseIfStatement (Keyword)) {}
	Keyword = ReduceCondition (Keyword);
	Keyword = ReduceOneZero (Keyword);
	if (Keyword [1])
	{
		fprintf (stderr, "Line ignored, too complex to reduce to 1 or 0:\n%s-- Reduces to \"%s\" --\n", SaveLine, Keyword);
	}
	IfdefDepth++;
	SkipIfdef [IfdefDepth] = Skipping || Keyword [0] == '0';
	Skipping = SkipIfdef [IfdefDepth];
#ifdef  DEBUG
	fprintf (stderr, "If -%s- %d %s\n", SaveLine, IfdefDepth, Skipping ? "SKIP" : "USE");
#endif  /* DEBUG */
	return 0;
}

/* return 0 if success -1 if error */
int ParseElse (char *Keyword, char *ROL, HowParse_t how)
{
	SkipIfdef [IfdefDepth] = !SkipIfdef [IfdefDepth];
	Skipping = SkipIfdef [IfdefDepth] || SkipIfdef [IfdefDepth-1];
#ifdef  DEBUG
	fprintf (stderr, "Else -%s- %d %s\n", Keyword, IfdefDepth, Skipping ? "SKIP" : "USE");
#endif  /* DEBUG */
	return 0;
}

/* return 0 if success -1 if error */
int ParseEndif (char *Keyword, char *ROL, HowParse_t how)
{
	IfdefDepth--;
	if (IfdefDepth < 0)
	{
		fprintf (stderr, "Mismatched #endif\nResults :may be incorrect\n");
		IfdefDepth = 0;
	}
	Skipping = SkipIfdef [IfdefDepth];
#ifdef  DEBUG
	fprintf (stderr, "Endif -%s- %d %s\n", Keyword, IfdefDepth+1, Skipping ? "SKIP" : "USE");
#endif  /* DEBUG */
	return 0;
}

char *ParseIfStatement (char *Line)
{
	char *Start, *DS, *DE;

	Start = strstr (Line, "defined");
	if (!Start)
	{
		char *Some = NULL;
		DE = DS = Line;
		while (*DS)
		{
			char DefineName [MAX_IDENTIFIER], *DN;
			for (DS = DE; *DS && !isalnum (*DS); DS++) {}
			if (!*DS) return Some;
			for (DE = DS, DN=DefineName; *DE && isalnum (*DE); *DN++ = *DE++) {}
			*DN = '\0';
			if (!GetDefine (DefineName)) return NULL;
			StringReplace (Line, DS, ValueDefine (DefineName), DefineName);
			Some = Line;
		}
		return Some;
	}
	else
	{
		DS = strchr (Start, '(');
		if (!DS) return Line;
		DE = strchr (Start, ')');
		if (!DE) return NULL;
		*DE++ = '\0';
		*Start++ = (char)(GetDefine (DS+1) ? '1' : '0');
		while (*DE) *Start++ = *DE++;
		*Start = '\0';
	}
	return Line;
}

/* -------------------- Line expansion and reduction --------------------- */
char *ReduceCondition (char *Line)
{
	char *Rel, *From, *Next, *End, *c;

	if (!Line [1]) return Line;
	for (c = Line; *c; c++) if (!strchr ("0123456789<>=!&|.", *c)) return Line;
	From = Line;
	while (*From && (Rel = strpbrk (From, "<>=!")) != NULL)
	{
		double First, Second;
		char Result;
		if (*Rel == '!' && Rel [1] != '=')
		{
			From = Rel+1;
			continue;
		}

		First = strtod (From, &Next);
		Next++;
		if (*Next == '=') Next++;
		Second = strtod (Next, &End);
		switch (*Rel)
		{

			case '>':
				Result = (char)((Next [-1] == '=') ? ((First >= Second) ? '1' : '0') : ((First > Second) ? '1' : '0'));
				break;
			case '<':
				Result = (char)((Next [-1] == '=') ? ((First <= Second) ? '1' : '0') : ((First < Second) ? '1' : '0'));
				break;
			case '=':
				Result = (char)((First == Second) ? '1' : '0');
				break;
			case '!':
				Result = (char)((First != Second) ? '1' : '0');
				break;
		}
		*From++ = Result;
		for (Next = From-1; *Next; *++Next = *End++) {}
		while (*From && !isdigit (*From)) From++;
	}
	return Line;
}

char *ReduceOneZero (char *Line)
{
	char *Sub;
	int i, j;
	Bool_t Reduced;
   Bool_t DidSomething;

	do
	{
		DidSomething = FALSE;
		for (i = 0; i < NPriority; i++)
		{
			Sub = Line;
			while (*Sub)
			{
				do
				{
					Reduced = FALSE;
					for (j = 0; (unsigned)j < NReductionList; j++)
					{
						Reduction_t *Red = &ReductionList [j];
						if (Red->Priority == i &&
							strncmp (Sub, Red->Atom, Red->Length) == 0)
						{
							char *RS = Sub, *RE = Sub + Red->Length;
							Reduced = TRUE;
							*RS++ = Red->Reduce;
							while (*RE) *RS++ = *RE++;
							*RS = '\0';
							DidSomething = TRUE;
							break;
						}
					}
				} while (Reduced);
				Sub++;
			}
		}
	} while (DidSomething);
	return Line;
}

char * ExpandDefines( char * Line )
{
  Define_t *    Def;
  int           more;

  do
  {
    Def = Defines;
    more = FALSE;

    while ( Def )
    {
      char * Pos = strstr( Line, Def->Name );

      if ( Pos )
      {
        char *  p = Pos;

        /* Check macro name not embedded in a longer identifier */
        /* The next line effectively skips backwards over digits */
        while ( p > Line && ! iscsymf( p[ -1 ] ) && iscsym( p[ -1 ] ) ) p--;
        if
        (
         ( p == Line || ! iscsymf( p[ -1 ] ) )
         && ! iscsym( p[ Def->Length ] )  
        )
        {
          more = TRUE;
          StringReplace( Line, Pos, Def->Value, Def->Name );
        }
      }
      Def = Def->next;
    }
  }
  while ( more );

  return Line;
}

char * ExpandMacros( char * Line )
{
  Macro_t *     Mac = Macros;
  int           more;

  do
  {
    Mac = Macros;
    more = FALSE;

    while ( Mac )
    {
      char *    Pos = strstr( Line, Mac->Name );

      if ( Pos )
      {
        char *  p = Pos;

        /* Check macro name not embedded in a longer identifier */
        /* The next line effectively skips backwards over digits */
        while ( p > Line && ! iscsymf( p[ -1 ] ) && iscsym( p[ -1 ] ) ) p--;
        if
        (
         ( p == Line || ! iscsymf( p[ -1 ] ) )
         && ! iscsym( p[ Mac->Length ] )  
        )
        {
          char *        ePos = strchr( &Pos[Mac->Length], '(' );
          int           bCount = 1;
          char          FromB[ MAX_LINE ];
          char          ToB[ MAX_LINE ];
          char          ParamB[ MAX_LINE ];
          char *        From = FromB;
          char *        To = ToB;
          char *        Param = ParamB;

          more = TRUE;
          if ( *ePos ) ePos++;
          while ( *ePos && bCount != 0 )
          {
            if ( *ePos == '(' ) bCount++; else if (*ePos == ')') bCount--;
            ePos++;
          }
          if ( !*ePos )
          {
            fprintf (stderr, "Mismatched brackets in line (macro ingored):\n%s", Line );
            return Line;
          }
          strncpy( FromB, Pos, ePos-Pos );
          FromB[ ePos-Pos ] = '\0';
          StringReplace( Line, Pos, "", From );
          * To = '\0';
          if ( *Mac->Expand )
          {
            char *      ThisParam = Param+1;
            char *      ThisFrom;

            stringCopy(ToB, Mac->Expand, MAX_LINE);
            stringCopy(ParamB, Mac->Params, MAX_LINE);
            From = strchr( From, '(' );
            if ( !From )
            {
              fprintf (stderr, 
               "Insufficient arguments for macro in line: (!result unpredicted!)\n%s",
               Line
              );
              return Line;
            }
            ThisFrom = From + 1;
            /* Convert <From> to <To> using parameters in <Param> */
            while ( *Param++)
            {
              if ( *Param == ')' || *Param == ',' )
              {
                int     nParam;
                char *  ThisMatch = To;

                *Param = '\0';
                nParam = strlen (ThisParam);
                if ( !*To )
                {
                  fprintf (stderr, 
                   "Insufficient arguments for macro in line: (expansion ignored)\n%s",
                   Line
                  );
                  return Line;
                }
                while ( * From++ )
                {
                  if ( *From == ')' || *From == ',' ) *From = '\0';
                }
                while ( NULL != ( ThisMatch = strstr( ThisMatch, ThisParam )))
                {
                  if ( !isalnum( ThisMatch[ nParam ]) )
                  {
                    StringReplace( To, ThisMatch, ThisFrom, ThisParam );
                  }
                  ThisMatch+= nParam;
                }
                *Param = ',';
                ThisParam = Param+1;
                ThisFrom = From;
              }
            }
          }
          StringReplace (Line, Pos, To, "");
        }
      }
      Mac = Mac->next;
    }
  }
  while ( more );

  return Line;
}

void StringReplace (char *Line, char *Where, char *To, char *From)
{
	int NFrom = strlen (From);
	int NTo = strlen (To);

#ifdef DEBUG
	fprintf (stderr, "StringReplace ->%s<- ->%s<- ->%s<-", From, To, Line );
#endif
	if (NFrom > NTo)
	{
		char *c = Where, *r = Where+NFrom-NTo;
		while (c[-1])*c++ = *r++;
	}
	else if (NFrom < NTo)
	{
		int nLine = strlen (Line);
		char *c = Line+nLine+NTo-NFrom, *r = Line+nLine;
		while (c > Where) *c-- = *r--;
	}
	strncpy (Where, To, NTo);
#ifdef DEBUG
	fprintf (stderr, "->%s<-\n", Line );
#endif
}

/* -------------------------- Sundry functions --------------------------- */
void ResetIfdefs (void)
{
	SkipIfdef [0] = FALSE;
}

void AddSystemIncludes (void)
{
	SystemIncludes = TRUE;
}

void DoubleSlashIsComment (void)
{
	DoubleSlashComments = TRUE;
}

void DefineExpansion (void)
{
	UseDefinesValues = TRUE;
}

/* ------------------ Include directory list processing ------------------ */
void AddIncludeDirs (char *DirList, Bool_t Global)
{
	char *Dir = DirList, *Sep;
	Include_t *Inc, *Next;

	while (Dir && *Dir)
	{
		Sep = strchr (Dir, ';');
		if (Sep) *Sep = '\0';
		Inc = malloc (sizeof (*Inc));
		if (!Inc)
		{
			fprintf (stderr, "Out of memory\nSome include directories may not be effective\nNeed to rebuild with a larger memory model\n");
			return;
		}
		if (!Includes)
		{
			Includes = Inc;
		}
		else
		{
			Next = Includes;
			while (Next->next) Next = Next->next;
			Next->next = Inc;
		}
		stringCopy(Inc->Path, Dir, MAX_PATHNAME);
		Dir = Sep ? Sep+1 : NULL;
		Inc->next = NULL;
		Inc->Global = Global;
	}
}

char *FindIncludeFile (char *Name, Bool_t Global)
{
	static char Path [MAX_PATHNAME];
	Include_t *Inc = Includes;
	FILE *handle;

	while (Inc)
	{
		if (!Global || Inc->Global)
		{
#ifdef MACOSX
			sprintf (Path, "%s/%s", Inc->Path, Name);
#endif
#ifdef UNIX
			sprintf (Path, "%s/%s", Inc->Path, Name);
#endif
#if defined(WIN32) || defined(IBMPC)
			sprintf (Path, "%s\\%s", Inc->Path, Name);
#endif
			/*
			 * It would be nice if we could use
			 *
			 * if (access (Path, 0) == 0) return Path;
			 *
			 * but unfortunately the macintosh returns 0
			 * when the parent directory of Path doesn't exist!
			 */
			if ( (handle = fopen(Path, "r")) )
			{
				fclose(handle);
				return Path;
			}
		}
		Inc = Inc->next;
	}
	return NULL;
}

void FreeIncludeDirs (void)
{
	while (Includes)
	{
		Include_t *Next = Includes->next;
#ifdef  DEBUG
		fprintf (stderr, "Path %s, %s\n", Includes->Path, Includes->Global ? "GLOBAL" : "LOCAL");
#endif  /* DEBUG */
		free (Includes);
		Includes = Next;
	}
}

/* ------------------------- Define processing --------------------------- */
void SetDefine (char *Name, char *Value, Bool_t State, Bool_t Global, HowParse_t how)
{
	Define_t *Def, *Next = Defines, *Prev = NULL;
	char *Nam = NULL, *Val = NULL;
	int Len = strlen (Name);
	int LVal = strlen (Value);

	if (AllocFailed) return;
	while (Next)
	{
		if (Next->Length == Len && strcmp (Next->Name, Name) == 0)
		{
			Next->State = State;
			return;
		}
		Prev = Next;
		Next = Next->next;
	}
	Def = malloc (sizeof (*Def));
	if (!Def)
	{
		if (!AllocFailed)
			fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
		AllocFailed = TRUE;
		return;
	}
	Nam = malloc (Len+1);
	if (!Nam)
	{
		if (!AllocFailed)
			fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
		AllocFailed = TRUE;
		free (Def);
		return;
	}
	if (UseDefinesValues)
	{
		Val = malloc (LVal+1);
		if (!Val)
		{
			if (!AllocFailed)
				fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
			AllocFailed = TRUE;
			free (Def);
			free (Nam);
			return;
		}
	}
	DefineCount++;
	if (Nam) strcpy (Nam, Name);
	if (Val) strcpy (Val, Value);
	Def->Name = Nam;
	Def->Value = Val;
	Def->Length = Len;
	Def->State = State;
	Def->Global = Global;
	Def->next = Defines;
	Defines = Def;
}

Bool_t GetDefine (char *Name)
{
	Define_t *Def = Defines;
	int Len = strlen (Name);

	while (Def)
	{
		if (Def->Length == Len && strcmp (Def->Name, Name) == 0)
		{
			return Def->State;
		}
		Def = Def->next;
	}
	return FALSE;
}

char *ValueDefine (char *Name)
{
	Define_t *Def = Defines;
	int Len = strlen (Name);

	while (Def)
	{
		if (Def->Length == Len && strcmp (Def->Name, Name) == 0)
		{
			return Def->Value;
		}
		Def = Def->next;
	}
	return "";
}

void UnsetDefine (char *Name)
{
	Define_t *Def = Defines, *Prev = NULL;
	int Len = strlen (Name);

	while (Def)
	{
		Define_t *Next = Def->next;
		if (Def->Length == Len && strcmp (Def->Name, Name) == 0)
		{
			if (Prev) Prev->next = Next; else Defines = Next;
			if (Def->Name) free (Def->Name);
			if (Def->Value) free (Def->Value);
			free (Def);
			DefineCount--;
			break;
		}
		else
		{
			Prev = Def;
		}
		Def = Next;
	}
}

void FreeDefines (Bool_t Global)
{
	Define_t *Def = Defines, *Prev = NULL;

	while (Def)
	{
		Define_t *Next = Def->next;
		if (Global || !Def->Global)
		{
			if (Prev) Prev->next = Next; else Defines = Next;
#ifdef  DEBUG
			fprintf (stderr, "Def %s ->%s<-\n", Def->Name ? Def->Name : "(NULL)", Def->Value ? Def->Value : "(NULL)");
#endif  /* DEBUG */
			if (Def->Name) free (Def->Name);
			if (Def->Value) free (Def->Value);
			free (Def);
			DefineCount--;
		}
		else
		{
			Prev = Def;
		}
		Def = Next;
	}
#ifdef  DEBUG
	fprintf (sterr, "Def count now %d\n", DefineCount);
#endif  /* DEBUG */
}

/* --------------------------- Macro processing -------------------------- */
void SetMacro (char *Name, char *Params, char *Expand, Bool_t State, Bool_t Global, HowParse_t how)
{
	Macro_t *Mac, *Next = Macros, *Prev = NULL;
	char *Nam = NULL, *Par = NULL, *Exp = NULL;
	int Len = strlen (Name);
	int LPar = strlen (Params);
	int LExp = strlen (Expand);

	if (AllocFailed) return;
	while (Next)
	{
		if (Next->Length == Len && strcmp (Next->Name, Name) == 0)
		{
			Next->State = State;
			return;
		}
		Prev = Next;
		Next = Next->next;
	}
	Mac = malloc (sizeof (*Mac));
	if (!Mac)
	{
		if (!AllocFailed)
			fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
		AllocFailed = TRUE;
		return;
	}
	Nam = malloc (Len+1);
	if (!Nam)
	{
		if (!AllocFailed)
			fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
		AllocFailed = TRUE;
		free (Mac);
		return;
	}
	if (how == E_ExpandPS)
	{
		Par = malloc (LPar+1);
		if (!Par)
		{
			if (!AllocFailed)
				fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
			AllocFailed = TRUE;
			free (Mac);
			free (Nam);
			return;
		}
		Exp = malloc (LExp+1);
		if (!Exp)
		{
			if (!AllocFailed)
				fprintf (stderr, "Out of memory\nSome defines may not be effective\nNeed to rebuild with a larger memory model\n");
			AllocFailed = TRUE;
			free (Par);
			free (Mac);
			free (Nam);
			return;
		}
}
	MacroCount++;
	if (Nam) strcpy (Nam, Name);
	if (Par) strcpy (Par, Params);
	if (Exp) strcpy (Exp, Expand);
	Mac->Name = Nam;
	Mac->Params = Par;
	Mac->Expand = Exp;
	Mac->Length = Len;
	Mac->State = State;
	Mac->Global = Global;
	Mac->next = Macros;
	Macros = Mac;
}

Bool_t GetMacro (char *Name)
{
	Macro_t *Mac = Macros;
	int Len = strlen (Name);

	while (Mac)
	{
		if (Mac->Length == Len && strcmp (Mac->Name, Name) == 0)
		{
			return Mac->State;
		}
		Mac = Mac->next;
	}
	return FALSE;
}

void UnsetMacro (char *Name)
{
	Macro_t *Mac = Macros, *Prev = NULL;
	int Len = strlen (Name);

	while (Mac)
	{
		Macro_t *Next = Mac->next;
		if (Mac->Length == Len && strcmp (Mac->Name, Name) == 0)
		{
			if (Prev) Prev->next = Next; else Macros = Next;
			if (Mac->Name) free (Mac->Name);
			if (Mac->Params) free (Mac->Params);
			if (Mac->Expand) free (Mac->Expand);
			free (Mac);
			MacroCount--;
			break;
		}
		else
		{
			Prev = Mac;
		}
		Mac = Next;
	}
}

void FreeMacros (Bool_t Global)
{
	Macro_t *Mac = Macros, *Prev = NULL;

	while (Mac)
	{
		Macro_t *Next = Mac->next;
		if (Global || !Mac->Global)
		{
			if (Prev) Prev->next = Next; else Macros = Next;
#ifdef  DEBUG
			fprintf (stderr, "Mac %s <-%s-> ->%s<-\n", Mac->Name ? Mac->Name : "(NULL)", Mac->Params ? Mac->Params : "(NULL)", Mac->Expand ? Mac->Expand : "(NULL)");
#endif  /* DEBUG */
			if (Mac->Name) free (Mac->Name);
			if (Mac->Params) free (Mac->Params);
			if (Mac->Expand) free (Mac->Expand);
			free (Mac);
			MacroCount--;
		}
		else
		{
			Prev = Mac;
		}
		Mac = Next;
	}
#ifdef  DEBUG
	fprintf (stderr, "Mac count now %d\n", MacroCount);
#endif  /* DEBUG */
}

/* --------------------------- Output production ------------------------- */
int SetOutput (char *OutFile, char *StartComment, char *EndComment)
{
	if (OutFile && *OutFile)
	{
		OutChannel = fopen (OutFile, "w");
		if (!OutChannel)
		{
			fprintf (stderr, "file %s cannot be opened for writing\n", OutFile);
			return 1;
		}
	}
	else
	{
		OutChannel = stderr;
	}
	fprintf (OutChannel, "%s Do not edit this auto-generated file %s\n", StartComment, EndComment);
	return 0;
}

void WriteAppend (char *Text)
{
	int Len = strlen (Text);
        if (!OutChannel) OutChannel = stderr;

	OutLength += Len;
	if (OutLength > 76)
	{
		fprintf (OutChannel, "\\");
		WriteNewLine ();
		WriteTabs ();
	}
	fprintf (OutChannel, "%s ",Text);
	fflush (OutChannel);
	OutLength += Len;
}

void WriteOut (char *Text)
{
  if (!OutChannel) OutChannel = stderr;
	fprintf (OutChannel, "%s",Text);
	fflush (OutChannel);
}

void WriteNewLine (void)
{
	OutLength = 0;
        if (!OutChannel) OutChannel = stderr;
	fprintf (OutChannel, "\n");
	fflush (OutChannel);
}

void WriteTabs (void)
{
	OutLength = 8;
        if (!OutChannel) OutChannel = stderr;
	fprintf (OutChannel, "\t");
	fflush (OutChannel);
}

void EndOutput (void)
{
	if (OutChannel && OutChannel != stderr) fclose (OutChannel);
}

/* Copy the source string into the target string if the target buffer is big enough.
 * Print an alert to stderr when the target buffer is too small.
 */
Bool_t stringCopy(char* target, char* source, int targetBufferSize)
{
  if ((int)(strlen(source) + 1) <= targetBufferSize) {
    strcpy(target, source);
    return TRUE;
  }
  else {
    fprintf(stderr, "ERROR: String does not fit in buffer!\n String:\"%s\"\n Buffer length: %d\n",
            source, targetBufferSize);
    return FALSE;
  }
}

/* Append the source string to the target string if the target buffer is big enough.
 * Print an alert to stderr when the target buffer is too small.
 */
Bool_t stringAppend(char* target, char* source, int targetBufferSize)
{
  if ((int)(strlen(target) + strlen(source) + 1) <= targetBufferSize) {
    strcat(target, source);
    return TRUE;
  }
  else {
    fprintf(stderr, "ERROR: Composite string does not fit in buffer!\n"
                    " Base:\"%s\"\n Addition:\"%s\"\n Buffer length: %d\n", 
            target, source, targetBufferSize);
    return FALSE;
  }
}


/* eof cparse.c */

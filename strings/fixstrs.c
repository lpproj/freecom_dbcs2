/*
This program will read in the language file and create three files:

strings.dat - contains all the strings. will be appended on the end
              of the exe file.

strings.h - include file that contains all the defines.

strings.log - contains all warnings of the local LNG file, the
              which resources (strings) are missing from it and
              which ones are not specified in the DEFAULT.lng, thus,
              are most likely unknown to FreeCOM
              This file is present only, if there are such warnings,
              but no message is printed onto screen!

Then, if the "/lib" option is present, it creates the
STRINGS library source files into the subdirectory STRINGS.

There are two input files:
DEFAULT.lng and the language file passed as argument to FIXSTRS.
DEFAULT.lng has two meanings making it a fundamental file, which
ensures the integrity of the multi-language support of FreeCOM:

1) The order of strings noted in DEFAULT.lng will be kept the same in
	all *.LNG files.
2) If an individual *.LNG file does not define a certain string, its
	contents is taken from the DEFAULT file.

Because each string is assigned a certain number and only this number
is known to FreeCOM internally, especially meaning 1) will ensure that
each STRINGS.DAT assigns the same semantic to those numbers.

The STRINGS.DAT file generated in the following steps:
1) DEFAULT.lng is read; all strings are copied into memory,
	the string_index_t array is generated with these strings.
	At the end, one could generate STRINGS.DAT with all the default
	strings, which are usually in English.
2) the specified *.LNG file is read; if a string is defined in this file,
	too, its contents overwrites the default string already present.
	New strings are assigned a new number above the already allocated
	string numbers.
3) Now all strings are known, there sizes and offsets within the file,
	the STRINGS.DAT and STRINGS.H files are generated.

2000/07/09 ska
chg: to read STRINGS.H to keep the same order of strings
chg: to let STRINGS.TXT read only once (temporary binary file)
chg: The format of STRINGS.DAT has been changed in order to support
	different languages _without_ recompiling.

2000/07/11 ska
chg: To use STRINGS.H to keep up the order becomes problematic, as this
	file is regenerated each time FIXSTRS is run. On failure, this
	file is destroyed. Therefore STRINGS.TXT will be renamed into
	DEFAULT.lng and is used to a) specify the order and, if missing,
	the default string text.

2001/03/15 ska
add: version number of strings and logfile entries
*/


#include <ctype.h>
#if defined(__TURBOC__)
#include <dir.h>
#elif defined(__GNUC__) && !defined(__MINGW32__)
#include <unistd.h>
#include <sys/stat.h>
#define stricmp strcasecmp
#define mkdir(x) mkdir(x, 0777)
static char *strupr(char *s)
{
  char *p;

  for (p = s; *p; p++)
    *p = toupper(*p);
  return s;
}
#elif defined(__WATCOMC__) && defined(__UNIX__)
#include <unistd.h>
#include <sys/stat.h>
#define mkdir(x) mkdir(x, 0777)
#else
#include <direct.h>
#include <io.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#undef DEBUG
#include "../config.h"
#include "../include/strings.typ"
#include "../include/resource.h"
#include "../include/keys.h"

#define logfile "strings.log"
#define fDAT "strings.dat"
#define fTXT "DEFAULT.lng"
#define fH "strings.h"
#define fEXT ".lng"
#define fDMAKEFILE "makefile"
#define fTCMAKEFILE "strings.rsp"
#define fTCMAKFILE "strings.mak"

typedef enum STATE {
	 LOOKING_FOR_START
	,GETTING_PROMPT_LINE_1
	,GETTING_PROMPT_LINE_2
	,GETTING_STRING
} read_state;

#define MAXSTRINGS       384

#define VERSION_MISMATCH 128
#define VALIDATION_MISMATCH 64
#define PERFORM_VALIDATION 32

const char id[]="FreeDOS STRINGS v";
	/* all prompts within *.LNG files start with */
const char promptID[] = "PROMPT_";
#define promptIDlen (sizeof(promptID) - 1)

#define STRINGLIB_DIR "strings"
#define stringdir cfile
#define cfilename (&cfile[sizeof(STRINGLIB_DIR)])
#define cfmt "str%04x.c"
#define objfmt "str%04x.obj"
char cfile[] = STRINGLIB_DIR "\0str45678.obj";

/*
	Implementation details about to cache the strings within memory:

	+ Because the strings are currently entitled to fit into 64KB,
		they should fit into memory during runtime of this program.
		Therefore FIXSTRS will be compiled in Compact memory model.

*/

typedef struct {
	char *text;
	int length;
} dynstring;
typedef struct {
	const int keycode;
	const char * const keyname;
} symKey;

FILE *lgf = 0;

int in_file = 0;
string_index_t string[MAXSTRINGS];
struct {
	int flags;				/* bitfield: #0 -> DEFAULT, #1 -> special LNG file
								meaning: present in particular file
								#5: perform printf() validation
								#6: printf() validation failed
								#7: Version mismatch */
	char *name;				/* name of string */
	char *text;				/* text of this string */
	int version;
	char *vstring;			/* validation string */
} strg[MAXSTRINGS];
string_count_t cnt = 0;		/* current string number */
string_count_t maxCnt = 0;	/* number of strings within array */

#if defined(__TINY__) || defined(__SMALL__) || defined(__MEDIUM__)
#error "This program must be compiled with far data pointers"
#endif

char temp[1024];

static const char besFromChar[] =
 "abcdefghijklmnopqrstuvwxyz,.[{}]\\?0";
static const char besToChar[] =
 "\a\bcd\e\fghijklm\nopq\rs\t\x0\vw\x0yz,.[{}]\\?";

symKey symkeys[] = {		/* symbolic keynames, uppercased! */
	 { KEY_CTL_C,	"BREAK" }		/* Pseudo-^Break */
	,{ KEY_CTL_C,	"CBREAK" }
	,{ KEY_NL,	"LF" }
	,{ KEY_NL,	"NL" }
	,{ KEY_NL,	"LINEFEED" }
	,{ KEY_NL,	"NEWLINE" }
	,{ KEY_CR,	"CR" }
	,{ KEY_CR,	"ENTER" }
	,{ KEY_ESC,	"ESC" }
	,{ KEY_ESC,	"ESCAPE" }
	,{ ASCIICODE('\f'),	"FF" }
	,{ ASCIICODE('\f'),	"FORMFEED" }
	,{ ASCIICODE('\a'),	"ALARM" }
	,{ ASCIICODE('\a'),	"BELL" }
	,{ ASCIICODE('\a'),	"BEEP" }
	,{ KEY_BS,	"BS" }
	,{ KEY_BS,	"BACKSPACE" }
	,{ KEY_TAB,	"HT" }
	,{ KEY_TAB,	"TAB" }
	,{ ASCIICODE('\v'),	"VT" }
	,{ ASCIICODE('\0'),	"NUL" }
	,{ ASCIICODE('\x1'),	"SOH" }
	,{ ASCIICODE('\x2'),	"STX" }
	,{ ASCIICODE('\x3'),	"ETX" }
	,{ ASCIICODE('\x4'),	"EOT" }
	,{ ASCIICODE('\x5'),	"ENQ" }
	,{ ASCIICODE('\x6'),	"ACK" }
	,{ ASCIICODE('\x7'),	"BEL" }
		/* 8 -> BS, 9 -> HT, A -> LF, B ->VT, C -> FF, D -> CR */
	,{ ASCIICODE('\xe'),	"SO" }
	,{ ASCIICODE('\xf'),	"SI" }
	,{ ASCIICODE('\x10'),	"DLE" }
	,{ ASCIICODE('\x11'),	"DC1" }
	,{ ASCIICODE('\x12'),	"DC2" }
	,{ ASCIICODE('\x13'),	"DC3" }
	,{ ASCIICODE('\x14'),	"DC4" }
	,{ ASCIICODE('\x15'),	"NAK" }
	,{ ASCIICODE('\x16'),	"SYN" }
	,{ ASCIICODE('\x17'),	"ETB" }
	,{ ASCIICODE('\x18'),	"CAN" }
	,{ ASCIICODE('\x19'),	"EM" }
	,{ ASCIICODE('\x1a'),	"SUB" }
		/* 1b -> ESC */
	,{ ASCIICODE('\x1c'),	"FS" }
	,{ ASCIICODE('\x1d'),	"GS" }
	,{ ASCIICODE('\x1e'),	"RS" }
	,{ ASCIICODE('\x1f'),	"US" }

	,{ KEY_F1, "F1" }
	,{ KEY_F2, "F2" }
	,{ KEY_F3, "F3" }
	,{ KEY_F4, "F4" }
	,{ KEY_F5, "F5" }
	,{ KEY_F6, "F6" }
	,{ KEY_F7, "F7" }
	,{ KEY_F8, "F8" }
	,{ KEY_F9, "F9" }
	,{ KEY_F10, "F10" }
	,{ KEY_F11, "F11" }
	,{ KEY_F12, "F12" }

	,{ KEY_LEFT, "LEFT" }
	,{ KEY_RIGHT, "RIGHT" }
	,{ KEY_UP, "UP" }
	,{ KEY_DOWN, "DOWN" }
	,{ KEY_INS, "INS" }
	,{ KEY_DEL, "DEL" }
	,{ KEY_HOME, "HOME" }
	,{ KEY_END, "END" }
	,{ KEY_PUP, "PUP" }
	,{ KEY_PDOWN, "PDOWN" }

	,{ 0, ""}
};

#if defined(DBCS)

static int is_dbcs_lead_default(unsigned char c)
{	/* single-byted character strings */
	(void)c;
	return 0;
}
static int is_dbcs_lead_cp932(unsigned char c)
{	/* Japanese (CP932 shift_jis) */
	return (0x81 <= c && c <= 0x9f) || (0xe0 <= c && c <= 0xfc);
}

int (*is_dbcs_lead)(unsigned char c) = is_dbcs_lead_default;

#endif

	/* keep it a single-file project */
#include "../lib/res_w.c"

/*
 * Append the passed in string onto strg[cnt].text
 */
#define app(s)	appStr(text, (s))
#define appStr(vs,s) appMem((vs), (s), strlen((s)))
#define appMem(vs,s,l) appMem_(&(vs), (s), (l))
int appMem_(dynstring *vs, char *s, int length)
{
	if((vs->text = realloc(vs->text, 1 + vs->length + length)) != 0) {
		char *p;

		p = vs->text + vs->length;
		if(length) memcpy(p, s, length);
		p[length] = 0;
		vs->length += length;
		return 1;
	}

	fputs("Out of memory\n", stderr);
	return 0;
}

unsigned fromxdigit(int ch)
{	if(isdigit(ch))
		return ch - '0';
	return toupper(ch) - 'A';
}

#ifdef __TURBOC__
#define join(s1,s2)	strcpy(stpcpy(temp, s1), s2);
#else
#define join(s1,s2)	strcat(strcpy(temp, s1), s2);
#endif
void pxerror(const char * const msg1, const char * const msg2)
{	join(msg1, msg2);
	perror(temp);
}

void dumpCh(FILE * const f, const unsigned char ch)
{	static const char from[] = "'\\\n\t\a\b\f";
	static const char to[]   = "'\\ntabf0";
	const char *p;

	if((p = strchr(from, ch)) != 0)
		fprintf(f, "'\\%c'", to[(int)(p - from)]);
	else if(ch > 0 && ch < 0x7f && isprint(ch))
		fprintf(f, " '%c'", ch);
	else fprintf(f, "0x%02x", (unsigned char)ch);
}

void dumpString(const int stringId)
{	FILE *f;
	int len, cnt;
	unsigned char *p;

	sprintf(cfilename, cfmt, stringId);
	if((f = fopen(cfile, "wt")) == NULL) {
		pxerror("creating ", cfile);
		exit(102);
	}
	fprintf(f, "const char %s[] = {\n\t", strg[stringId].name);
	p = (unsigned char*)strg[stringId].text;
	if(string[stringId].size > 1) {
		for(len = string[stringId].size, cnt = 9; --len;) {
			char ch;

			dumpCh(f, ch = *p++);
			putc(',', f);
			putc(' ', f);
			if(--cnt == 0 || ch == '\n') {
				putc('\n', f);
				putc('\t', f);
				cnt = 9;
			}
		} 
	}
	if(string[stringId].size)
		dumpCh(f, *p);
	fputs("\n};\n", f);
	fclose(f);
}


/* map a backslash sequence */
int mapBSEscape(char ** const s)
{	char *p;
    const char *q;
	int ch;

	p = *s;
	if((ch = *p++) == 0)	/* Don't advance pointer */
		return 0;

	if(ch == 'x') {	/* Hexadecimal */
		if(isxdigit(*p)) {
			ch = fromxdigit(*p++);
			if(isxdigit(*p))
				ch = (ch << 4) | fromxdigit(*p++);
		} else
			ch = 0;
	} else if((q = strchr(besFromChar, ch)) != 0) {
		ch = besToChar[(unsigned)(q - besFromChar)];
	} /* else  ch remains the character behind the backslash */

	*s = p;			/* Advance pointer */
	return ch;
}

/* map a symbolic key */
int mapSymKey(char * const p)
{	symKey *q;

	strupr(p);		/* Uppercase here to speed up process later */
	q = symkeys;
	do if(strcmp(q->keyname, p) == 0)	/* found */
		break;
	while((q++)->keycode);

	return q->keycode;
}

int loadFile(const char * const fnam)
{	unsigned long linenr;
	char *p;
	read_state state = LOOKING_FOR_START;
	FILE *fin;
	dynstring text;			/* Current text */
	dynstring vstring;		/* Validation string */
	int version;
#if defined(DBCS)
	int in_dbcs = 0;
	char *upfnam;
#endif

	text.text = vstring.text = 0;
	version = 0;

	printf("FIXSTRS: loading file %s\n", fnam);

	join(fnam, fEXT);
	if((fin = fopen(fnam, "rt")) == NULL
	 && (fin = fopen(temp, "rt")) == NULL) {
		pxerror("opening ", fnam);
		return 33;
	}

#if defined(DBCS)
	is_dbcs_lead = is_dbcs_lead_default;
	if ((upfnam = strdup(fnam)) != NULL)
	{
		strupr(upfnam);
		if (strstr(upfnam, "JAPAN") || strstr(upfnam, "CP932"))
			is_dbcs_lead = is_dbcs_lead_cp932;
		free(upfnam);
		upfnam = NULL;
	}
#endif

	linenr = 0;
	while (fgets(temp, sizeof(temp), fin)) {
		++linenr;
		p = strchr(temp, '\0');
		if(p[-1] != '\n') {
			fprintf(stderr, "Line %lu too long\n", linenr);
			return 41;
		}
			/* Cut trailing control characters */
		while (--p >= temp && iscntrl(*p));
		p[1] = '\0';

		switch (state) {
		case LOOKING_FOR_START:
			switch(*temp) {
			case ':': {
				char *vers;

				if((vers = strchr(temp + 1, '#')) != 0) {
					*vers = '\0';
				}
				/* Locate the string name */
				for(cnt = 0; cnt < maxCnt; ++cnt)
					if(strcmp(strg[cnt].name, temp + 1) == 0)
						goto strnameFound;
				/* string name was not found --> create a new one */
				++maxCnt;
				if (maxCnt >= MAXSTRINGS) {
					fprintf(stderr, "Out of string buffer; should increase MAXSTRINGS to more than %u\n", (unsigned)(MAXSTRINGS));
					return 80;
				}
			strnameFound:
				if(!strg[cnt].name) {
					if((strg[cnt].name = strdup(temp + 1)) == 0) {
						fputs("Out of memory\n", stderr);
						return 80;
					}
				}
				vstring.length = text.length = 0;
				version = (vers && *++vers)? atoi(vers): 0;
				if(vers && strchr(vers, '%'))
					strg[cnt].flags |= PERFORM_VALIDATION;
				if(memcmp(strg[cnt].name, promptID, promptIDlen) == 0)
					state = GETTING_PROMPT_LINE_1;
				else
					state = GETTING_STRING;
			}
			break;

			default:
				while(p >= temp && isspace(*p)) --p;
				if(p >= temp) {
					fprintf(stderr, "Syntax error in line #%lu\n", linenr);
					return 44;
				}
				/** fall through **/
			case '\0': case '#':
				break;
			}
		break;

		case GETTING_PROMPT_LINE_1:
			{
				char *p, *q, len;
				int ch;

				if((*temp == '.' || *temp == ',') && (temp[1] == '\0')) {
					fprintf(stderr, "%s: %s: prompt syntax error\n"
					 , fnam, strg[cnt].name);
					return 41;
				}
				q = p = temp;
#if defined(DBCS)
				in_dbcs = 0;
#endif
				while((ch = *p++) != 0)
#if defined(DBCS)
				{ /* DBCS: beginning of while */
					if (in_dbcs) {
						*q++ = ch;
						in_dbcs = 0;
						continue;
					} else {
						in_dbcs = is_dbcs_lead((unsigned char)ch);
					}
#endif
					switch(ch) {
					case '\\':
						if(*p && (*q++ = mapBSEscape(&p)) == 0) {
							fprintf(stderr
							 , "%s: %s: ASCII(0) is no valid key\n"
							 , fnam, strg[cnt].name);
							return 49;
						}
						break;
					case '{':
						{	char *h;
							int thisCh;

							if((p = strchr(h = p, '}')) == 0) {
								fprintf(stderr
								 , "%s: %s: invalid symbolic key\n"
								 , fnam, strg[cnt].name);
								return 46;
							}
							*p++ = 0;
							if((thisCh = mapSymKey(h)) == 0) {
								fprintf(stderr
								 , "%s: %s: unknown symbolic key\n"
								 , fnam, strg[cnt].name);
								return 47;
							}
							if(thisCh >= 256) {
								fprintf(stderr
								 , "%s: %s: non-ASCII keys not supported, yet\n"
								 , fnam, strg[cnt].name);
								return 55;
							}
							*q++ = thisCh;
						}
						break;
					case '[':
						fprintf(stderr
						 , "%s: %s: brackets are not supported, yet\n"
						 , fnam, strg[cnt].name);
						return 48;

					default:
						*q++ = ch;
						break;
					}
#if defined(DBCS)
				} /* DBCS: end of while */
#endif
				*q = 0;
				if(q == temp) {
					fprintf(stderr
					 , "%s: %s: empty key sequence\n"
					 , fnam, strg[cnt].name);
					 return 52;
				}
				if((unsigned)(q - temp) > 255) {
					fprintf(stderr
					 , "%s: %s: too many keys\n"
					 , fnam, strg[cnt].name);
					 return 55;
				}
				len = (unsigned char)(q - temp);
					/* Prompts are PStrings in this form:
						LKKKKMMMM
						where number of K's == number of M's == L
						K -> key (1..255); M -> metakey (range 1..26);
						0 < L < 256
					*/
				if(!appMem(text, &len, 1) || !app(temp))
					return 42;
				state = GETTING_PROMPT_LINE_2;
			}
			break;


		case GETTING_PROMPT_LINE_2:
		{
			char *p, *q;

			if ((*temp == '.' || *temp == ',') && (temp[1] == '\0')) {
				fprintf(stderr, "%s: %s: prompt syntax error\n"
				 , fnam, strg[cnt].name);
				return 43;
			}

			p = q = temp;
			while((*q = *p++) != 0)
				if(*q >= 'a' && *q <= 'z') {
					*q++ -= 'a' - 1;	/* valid metakey */
				} else if(!isspace(*q)) {
					fprintf(stderr, "%s: %s: invalid target metakey\n"
					 , fnam, strg[cnt].name);
					return 44;
				}
			if((unsigned)(q - temp) + 1 != text.length) {
				fprintf(stderr
				 , "%s: %s: number of metakeys does not match input keys\n"
				 , fnam, strg[cnt].name);
				return 53;
			}
			if(!app(temp))
				return 54;
			state = GETTING_STRING;
			break;
		}


		case GETTING_STRING:
			if ((*temp == '.' || *temp == ',') && (temp[1] == '\0')) {
				if (*temp == ',' &&	text.length
				 && text.text[text.length - 1] == '\n') {
				 	/* Cut the text as there is to always be a '\0' at the
						end of the string */
				 	text.text[--text.length] = '\0';
				}
				state = LOOKING_FOR_START;
				appMem(vstring, "", 0);		/* ensure vstring.text is != NULL */
				assert(vstring.text);
				assert((strg[cnt].flags & 3) == 0		/* New string */
				 || strg[cnt].vstring);
				/* Apply the cached text */
				if((strg[cnt].flags & 3) == 0		/* New string */
				 || (strg[cnt].version == version
				     && ((strg[cnt].flags & PERFORM_VALIDATION) == 0
				      || strcmp(strg[cnt].vstring, vstring.text) == 0))) {
					/* OK -> replace it */
					strg[cnt].version = version;
					free(strg[cnt].text);
					strg[cnt].text = text.text;
					string[cnt].size = text.length + 1;
					free(strg[cnt].vstring);
					strg[cnt].vstring = vstring.text;
				} else {
					if(strg[cnt].version != version)
						strg[cnt].flags |= VERSION_MISMATCH;
					if(strcmp(strg[cnt].vstring, vstring.text) != 0)
						strg[cnt].flags |= VALIDATION_MISMATCH;
					/* Failed -> ignore the read text */
					free(text.text);
					free(vstring.text);
				}
				text.text = vstring.text = 0;
				strg[cnt].flags |= in_file;
			} else {
				char *p, *q, ch;
				/* Fetch the '%' format sequences */
				q = temp - 1;
				while((p = strchr(q + 1, '%')) != 0) {
					if((q = strpbrk(p, "%diouxXfegEGcsnp")) == 0)
						q = strchr(p, '\0') - 1;
					if(!appMem(vstring, p, (unsigned)(q - p) + 2))
						return 51;
				}
				/* Replace backslash escape sequences */
				p = q = temp;
#if defined(DBCS)
				in_dbcs = 0;
#endif
				while((ch = *p++) != 0) {
#if defined(DBCS)
					if (in_dbcs) {
						*q++ = ch;
						in_dbcs = 0;
						continue;
					}
					else
						in_dbcs = is_dbcs_lead((unsigned char)ch);
#endif
					if(ch != '\\')
						*q++ = ch;
					else if(!*p) goto noAppendNL;
					else *q++ = mapBSEscape(&p);
				}
				*q++ = '\n';
noAppendNL:
				if(!appMem(text, temp, (unsigned)(q - temp)))
					return 82;
			}
		break;
		}
	}
	if(ferror(fin)) {
		pxerror("reading ", fnam);
		return 34;
	}
	fclose(fin);
	if(state != LOOKING_FOR_START) {
		fprintf(stderr, "%s: Last string not terminated\n", fnam);
		return 40;
	}

	return 0;
}

static int create_make_dependency(void)
{
	string_count_t cnt1;
	string_count_t maxCnt1;
	string_count_t cnt2;

	strcpy(cfilename, fTCMAKFILE);
	if((lgf = fopen(cfile, "wt")) == NULL) {
		pxerror("creating ", cfile);
		return 101;
	}
	cnt1 = maxCnt1 = cnt2 = 0;
	for(cnt = 0; cnt < maxCnt; ++cnt) {
		if( cnt1 == 0 ) {
			if( cnt > 0 )
				fprintf(lgf, "\n");
			fprintf(lgf, "strings_deps%d : \\\n", maxCnt1++);
			cnt2 = 0;
		}
		fprintf(lgf, " " objfmt, cnt);
		if(++cnt1 > 127) {
			cnt1 = 0;
		}
		if(++cnt2 > 7 && cnt1 > 0) {
			fprintf(lgf, " \\\n");
			cnt2 = 0;
		}
	}
	for(cnt = 0; cnt < maxCnt1; ++cnt) {
		if( cnt == 0 )
			fprintf(lgf, "\nSTRINGS_DEPS =");
		fprintf(lgf, " strings_deps%d", cnt);
	}
	fflush(lgf);
	if(ferror(lgf)) {
		puts("Unspecific error writing to " fTCMAKFILE);
		return 104;
	}
	fclose(lgf);
	return 0;
}

int main(int argc, char **argv)
{
	FILE *dat, *inc;
	int rc;
	unsigned long size;
	string_count_t cnt;		/* current string number */
	string_size_t lsize;
	int makeLib = 0;
	const char *fmt;

	unlink(logfile);

	while(argv[1] != NULL ) {
		if(stricmp(argv[1], "/lib") == 0 || stricmp(argv[1], "--lib") == 0) {
			--argc;
			++argv;
			makeLib = 1;
		} else if(stricmp(argv[1], "/lib1") == 0 || stricmp(argv[1], "--lib1") == 0) {
			--argc;
			++argv;
			makeLib = 2;
		} else if(stricmp(argv[1], "/lib2") == 0 || stricmp(argv[1], "--lib2") == 0) {
			--argc;
			++argv;
			makeLib = 3;
		} else {
			break;
		}
	}

	/*
	 * Hidden options lib and lib1 and lib2
	 *
	 * if one of these option is used then program generate strings library
	 * source files and generate make files and response file for make utility
	 *
	 * lib.. options control response file format
	 * lib2  format is '+<file name> &'
	 * lib1  format is '+<file name>'
	 * lib   format is '<file name>' only
	 */
	if(argc > 2 ) {
		puts("FIXSTRS - Generate STRINGS.DAT and STRINGS.H for a language\n"
			"Useage: FIXSTRS [/lib|/lib1|/lib2] [language]\n"
			"\tIf no language is specified, only the default strings are read.\n"
			"\tThe <language>.LNG file must reside in the current directory.\n"
			"Note: DEFAULT.lng must be present in the current directory, too.");
		return 127;
	}


	in_file = 1;
	if((rc = loadFile(fTXT)) != 0)
		return rc;
	in_file = 2;
	if(argc > 1 && (rc = loadFile(argv[1])) != 0)
		return rc;

/* Now all the strings are cached into memory */

	if(maxCnt < 2) {
		fputs("No string definition found.\n", stderr);
		return 43;
	}

	/* Create the LOG file */
	if(argc > 1) {		/* Only if a local LNG file was specified */
		lgf = NULL;			/* No LOG entry til this time */
		for(cnt = 0; cnt < maxCnt; ++cnt) {
			switch(strg[cnt].flags & 3) {
			case 0:		/* Er?? */
				fputs("Internal error assigned string has no origin?!\n"
				 , stderr);
				return 99;
			case 1:		/* DEFAULT.lng only */
				if(!lgf && (lgf = fopen(logfile, "wt")) == NULL) {
					fprintf(stderr, "Cannot create logfile: '%s'\n"
					 , logfile);
					goto breakLogFile;
				}
				fprintf(lgf, "%s: Missing from local LNG file\n"
				 , strg[cnt].name);
				break;
			case 2:		/* local.LNG only */
				if(!lgf && (lgf = fopen(logfile, "wt")) == NULL) {
					fprintf(stderr, "Cannot create logfile: '%s'\n"
					 , logfile);
					goto breakLogFile;
				}
				fprintf(lgf, "%s: No such string resource\n"
				 , strg[cnt].name);
				break;
			case 3:		/* OK */
				break;
			}
			if(strg[cnt].flags & VERSION_MISMATCH) {
				if(!lgf && (lgf = fopen(logfile, "wt")) == NULL) {
					fprintf(stderr, "Cannot create logfile: '%s'\n"
					 , logfile);
					goto breakLogFile;
				}
				fprintf(lgf, "%s: Version mismatch, current is: %u\n"
				 , strg[cnt].name, strg[cnt].version);
			}
			if(strg[cnt].flags & VALIDATION_MISMATCH) {
				if(!lgf && (lgf = fopen(logfile, "wt")) == NULL) {
					fprintf(stderr, "Cannot create logfile: '%s'\n"
					 , logfile);
					goto breakLogFile;
				}
				fprintf(lgf, "%s: printf() format string mismatch, should be: %s\n"
				 , strg[cnt].name, strg[cnt].vstring);
			}
		}

		if(lgf)
			fclose(lgf);
	}
breakLogFile:

	/* 1. Adjust the offset and generate the overall size */
	for(size = string[0].size, cnt = 1; cnt < maxCnt; ++cnt) {
		string[cnt].index = string[cnt-1].index + string[cnt-1].size;
		size += string[cnt].size;
	}

	if(size >= 0x10000ul - sizeof(string_index_t) * maxCnt) {
		fputs("Overall size of strings exceeds 64KB limit\n", stderr);
		return 44;
	}

	/* 2. Open STRINGS.DAT and STRINGS.H and dump control information */
	if ((dat = fopen(fDAT,"wb")) == NULL) {
		perror("creating " fDAT);
		return 36;
	}
	if ((inc = fopen(fH,"wt")) == NULL) {
		perror("creating " fH);
		return 37;
	}

puts("FIXSTRS: building STRINGS resource");

	fputs("/*\n"
		" * This file was automatically generated by FIXSTRS.\n"
		" * Any modifications will be lost next time this tool\n"
		" * is invoked.\n"
		" */\n\n", inc);
	fprintf(inc,"#define  STRINGS_ID         \"%s%u\"\n"
	 , id, STRING_RESOURCE_MINOR_ID);

	startResource(dat, RES_ID_STRINGS, STRING_RESOURCE_MINOR_ID);
		/* Preamble of STRINGS.DAT file */
	fprintf(dat, "%s%u", id, STRING_RESOURCE_MINOR_ID);
/*	fwrite(id, sizeof(id) - 1, 1, dat);		*//* file contents ID */
	fwrite("\r\n\x1a", 4, 1, dat);			/* text file full stop */
	fputs("#define  STRINGS_ID_TRAILER 4\n", inc);	/* 4 additional bytes */
	fputs("\n\n", inc);						/* delimiter */

		/* parameters of strings */
	fwrite(&maxCnt, sizeof(maxCnt), 1, dat);	/* number of strings */
	lsize = (string_size_t)size;
	fwrite(&lsize, sizeof(lsize), 1, dat);		/* total size of string text */

		/* string control area */
	fwrite(string, sizeof(string[0]), maxCnt, dat);
	/* append the strings */
	for(cnt = 0; cnt < maxCnt; ++cnt) {
		fwrite(strg[cnt].text, string[cnt].size, 1, dat);
		if(makeLib)
			fprintf(inc, "extern const char %s[];\n", strg[cnt].name);
		fprintf(inc, "#define  %-34s 0x%02x  /* @ 0x%04x */\n"
		 , strg[cnt].name, cnt, string[cnt].index);
	}
	fputs("\n/* END OF FILE */\n", inc);
	endResource(dat);

	fflush(dat);
	if(ferror(dat)) {
		fputs("Unspecific write error into " fDAT "\n", stderr);
		return 38;
	}
	fflush(inc);
	if(ferror(inc)) {
		fputs("Unspecific write error into " fH "\n", stderr);
		return 39;
	}

	fclose(dat);
	fclose(inc);

	if(makeLib) {
		mkdir(stringdir);
#define fdmake inc
#define ftc101 dat
		cfilename[-1] = '/';
		strcpy(cfilename, fDMAKEFILE);
		if((fdmake = fopen(cfile, "wt")) == NULL) {
			pxerror("creating ", cfile);
			return 100;
		}
		if((rc = create_make_dependency()) != 0)
			return rc;
		strcpy(cfilename, fTCMAKEFILE);
		if((ftc101 = fopen(cfile, "wt")) == NULL) {
			pxerror("creating ", cfile);
			return 101;
		}

puts("FIXSTRS: building STRINGS library source files");
		/********************** prologue */
		fputs("\
MAXLINELENGTH := 8192\n\
# Project specific C compiler flags\n\
MYCFLAGS_DBG = -UNDEBUG $(null,$(DEBUG) $(NULL) -DDEBUG=1)\n\
MYCFLAGS_NDBG = -DNDEBUG=1 -UDEBUG\n\
MYCFLAGS = $(null,$(NDEBUG) $(MYCFLAGS_DBG) $(MYCFLAGS_NDBG))\n\
\n\
#	Default target\n\
all: $(CFG) strings.lib\n\
\n\
strings.lib .LIBRARY : ", fdmake);

		/********************* individual files */
		for(cnt = 0; cnt < maxCnt; ++cnt) {
			dumpString(cnt);
			fprintf(fdmake, "\\\n\t" objfmt, cnt);
		}
		if(makeLib == 3) {
			/* Turbo C */
			fmt = "+" objfmt " &\n";
		} else if(makeLib == 2) {
			/* Borland C */
			fmt = "+" objfmt "\n";
		} else {
			/* GCC, Open Watcom */
			fmt = objfmt "\n";
		}
		for(cnt = 0; cnt < maxCnt - 1; ++cnt) {
			fprintf(ftc101, fmt, cnt);
		}
		if(makeLib > 1) {
			/* Borland C, Turbo C */
			fmt = "+" objfmt "\n";
		} else {
			/* GCC, Open Watcom */
			fmt = objfmt "\n";
		}
		fprintf(ftc101, fmt, cnt);
		/********************** epilogue */

		fputs("\n\
\n\
.IF $(CFG) != $(NULL)\n\
\n\
CONFIGURATION = $(CONF_BASE)\n\
\n\
.IF $(_COMPTYPE) == BC\n\
CONF_BASE =	\\\n\
-f- \\\n\
-I$(INCDIR:s/;/ /:t\";\")	\\\n\
-L$(LIBDIR:s/;/ /:t\";\")	\\\n\
-w\n\
\n\
.IF $(_COMPILER)==BC5\n\
CONFIGURATION += -RT- -x-\n\
.ENDIF\n\
\n\
CONF_DBG =	$(MYCFLAGS_DBG)\n\
CONF_NDBG =	$(MYCFLAGS_NDBG)\n\
\n\
.ENDIF\n\
\n\
.IF $(_COMPTYPE) == TC\n\
CONF_BASE =	\\\n\
-I$(INCDIR:s/;/ /:t\";\")	\\\n\
-L$(LIBDIR:s/;/ /:t\";\")	\\\n\
-w\n\
\n\
CONF_DBG =	$(MYCFLAGS_DBG)\n\
CONF_NDBG =	$(MYCFLAGS_NDBG)\n\
\n\
.ENDIF\n\
.ENDIF\n", fdmake);

		fflush(ftc101);
		if(ferror(ftc101)) {
			puts("Unspecific error writing to " fTCMAKEFILE);
			return 104;
		}
		fclose(ftc101);
		fflush(fdmake);
		if(ferror(fdmake)) {
			puts("Unspecific error writing to " fDMAKEFILE);
			return 105;
		}
		fclose(fdmake);
	}

	return 0;
}

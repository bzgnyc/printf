// There should be a 1:1 match with specifiers handled by printf1arg()
#define PRINTF_SPECIFIERS_STD	"diufFeEgGxXosScCaA"
// This adds specifiers valid for printf(1) but not valid for printf(3)
#define PRINTF_SPECIFIERS	PRINTF_SPECIFIERS_STD "bQ"

// Include all specifiers valid for printf(3) but not in STD_PRINTF_SPECIFIERS
#define PRINTF_SPECIFIERS_INVALID "npDOUv" //"bkmrwyBHIJKLMNOPQRTUVWYZ"

// Include all length modifiers recognized by printf(3)
#define PRINTF_LENGTHS "hlLjtzq"


#include "cstandards.h"

#if defined(__has_include) && __has_include(<iconv.h>) && !defined(NO_ICONV)
#define HAVE_ICONV
#if __has_include(<langinfo.h>)
#define HAVE_LANGINFO
#if defined(__APPLE__) && !defined(NEED_LANGINFO)
#define NEED_LANGINFO
#endif // NEED_LANGINFO
#endif // HAVE_LANGINFO
#endif // HAVE_ICONV

#ifdef HAVE_ICONV
#define ICONV_UCS_4_INTERNAL	"UCS-4-INTERNAL"
#if defined(HAVE_LANGINFO) && defined(NEED_LANGINFO)
// macOS's iconv requires this to convert to a non-UTF-8 locale
#define ICONV_CURRENT_CODESET	nl_langinfo(CODESET)
#else
#define ICONV_CURRENT_CODESET	""
#endif // NEED_LANGINFO
#endif // HAVE_ICONV

#ifdef __OpenBSD__
#define HAVE_PLEDGE
#endif


#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>
#include <locale.h>
#if defined(__has_include) && __has_include(<unistd.h>) && HAVE_PLEDGE
#include <unistd.h>
#endif // HAVE_PLEDGE
#if defined(__has_include) && __has_include(<sysexits.h>)
#include <sysexits.h>
#endif // has_sysexits

#ifndef ARG_MAX
#define ARG_MAX	4096
#endif // ARG_MAX
#ifndef EFAULT
#define EFAULT	EILSEQ+100
#endif // EFAULT
#ifndef EINVAL
#define EINVAL	EFAULT+1
#endif // EINVAL


static int anyerrno;
static char *progname;


/*
This is explicitly and intentionally a macro that generates a codeblock
rather than a function as the former seems much simpler in this situation.
Creating a polymorphic function that takes a variable number of arugments
would be more complicated than helpful.  Nor would a single inline function
work as simply.  Then instead of a while/do or similar idiom to hide that it
is a macro rather than a function, leaving as a straight macro means the
compiler will error if treated as a function called with a terminating ;.
*/
#define strtonum(num,convfunc,str,endptr)	errno = 0;\
						num = convfunc;\
						if ( errno > 0 || endptr[0] != '\0' ) {\
							if ( errno > 0 )\
								anyerrno = errno;\
							else\
								anyerrno = EINVAL;\
							if ( errno > 0 && errno != EINVAL )\
								fprintf(stderr,"%s: \"%s\": %s\n",progname,str,strerror(errno));\
							else\
								if ( endptr == str )\
									fprintf(stderr,"%s: \"%s\": expected numeric value\n",progname,str);\
								else\
									fprintf(stderr,"%s: \"%s\": not completely converted\n",progname,str);\
						}


#if ULONG_MAX >= 0x10ffff
#define strtocodepoint strtoul
#elif defined(ULLONG_MAX) && ULLONG_MAX >= 0x10ffff
#define strtocodepoint strtoull
#endif // strtocodepoint
// Otherwise assume system too limited to handle Unicode

#if defined(strtocodepoint) && defined(__STDC_UTF_32__) && !defined(__APPLE__)
#define HAVE_FROMUNICODE
#include <uchar.h>

char *
fromunicode(char32_t codepoint) {

	char *returnstr = malloc(MB_LEN_MAX + 1);
	size_t e;
	mbstate_t ps;

	/*
	Initialize state -- unclear if this is needed or even desirable
	to do each iteration.  Or should state be maintained as a global
	variable initialized once when first needed?  Needs testing on a
	platform that supports complex, stateful multibyte encodings like
	ISO-2022-JP.
	*/
	if ( ( e = c32rtomb(NULL, U'\0', ps) ) == (size_t) -1 ) {
		anyerrno = errno;
		perror(progname);
		strcpy(returnstr,"");
	} else
		if ( ( e = c32rtomb(returnstr,codepoint,&state) ) == (size_t) -1 ) {
			anyerrno = errno;
			perror(progname);
			strcpy(returnstr,"");
		} else
			returnstr[e]='\0';

	return returnstr;
}
#elif defined(strtocodepoint) && defined(__STDC_ISO_10646__)
#define HAVE_FROMUNICODE

char *
fromunicode(wchar_t codepoint) {

	char *returnstr = malloc(MB_LEN_MAX + 1);
	int e;

	if ( codepoint > 0x10FFFF ) {
		errno = EILSEQ;
		anyerrno = errno;
		perror(progname);
		strcpy(returnstr,"");

		return returnstr;
	}

	/*
	Assuming global conversion state used by wctomb shouldn't be reset
	with each call.  This needs testing on a platform that is
	__STDC_ISO_10646__ and also happens to support complex, stateful
	multibyte character encodings like ISO-2022-J.

	If needed it would be something like e = wctomb(NULL, U'\0');
	*/
	if ( ( e = wctomb(returnstr,codepoint) ) == (size_t) -1 ) {
		anyerrno = errno;
		perror(progname);
		strcpy(returnstr,"");
	} else
		returnstr[e]='\0';

	return returnstr;
}
#elif defined(strtocodepoint) && defined(HAVE_ICONV)
#define HAVE_FROMUNICODE
#ifdef NEED_LANGINFO
#include <langinfo.h>
#endif // NEED_LANGINFO
#include <iconv.h>
#include <inttypes.h>

char *
fromunicode(uint32_t codepoint) {

	iconv_t cd;
	size_t e;
	size_t inbytesleft = sizeof(uint32_t);
	char *inbuf = (char *) &codepoint;
	size_t outbytesleft = MB_LEN_MAX;
	char *returnstr = malloc(MB_LEN_MAX + 1);
	char *outbuf = returnstr;

	if ( codepoint > 0x10FFFF ) {
		errno = EILSEQ;
		anyerrno = errno;
		perror(progname);
		strcpy(returnstr,"");

		return returnstr;
	}

	if ( ( cd = iconv_open(ICONV_CURRENT_CODESET,ICONV_UCS_4_INTERNAL) ) == (iconv_t) -1 ) {
		anyerrno = errno;
		perror(progname);
		strcpy(returnstr,"");

		return returnstr;
	} else
		if ( ( e = iconv(cd,NULL,NULL,NULL,NULL) ) == (size_t) -1 ) {
			anyerrno = errno;
			perror(progname);
			strcpy(returnstr,"");
		} else
			if ( ( e = iconv(cd,&inbuf,&inbytesleft,&outbuf,&outbytesleft) ) == (size_t) -1 ) {
				anyerrno = errno;
				perror(progname);
				strcpy(returnstr,"");
			} else
				outbuf[0] = '\0';

	iconv_close(cd);

	return returnstr;
}
#endif // fromunicode


char *
unescape(size_t *returnstrlen, char *srcstr, size_t srcstrlen, int *abortext ) {

	// srcstrlen == -1 -> strlen(srcstr) is unknown
	int maxstrlen = (((srcstrlen) == (-1)) ? (ARG_MAX) : (srcstrlen + 1));
	/*
	This assumes the resulting unescaped string won't be longer than
	the source string.  This should always be true if
	MB_LEN_MAX <= strlen("\uXXXX") [i.e. 6] and no single character
	escape sequence expands to more than 2 chars.
	*/
	char *returnstr = malloc(maxstrlen * sizeof(char));
	char c[8+1];
	char *endptr;
	char *u;
	int d = 0;
	size_t i = 0;
	size_t j = 0;
	size_t seglen;

	while ( d >= 0 && srcstr[i] != '\0' ) {
		if ( (maxstrlen-j) > 0 ) {
			seglen = 0;
        	        d = sscanf(&srcstr[i],"%[^\\]%zn",&returnstr[j],&seglen);
			i += seglen; j += seglen;
			if ( srcstr[i] == '\\' ) {
				i++;
				if ( srcstr[i] == 'c' && abortext != NULL ) {
					*abortext = 1;
					break;
				} else {
					switch (srcstr[i]) {
						case '\\': returnstr[j] = '\\'; break;
						case '\'': returnstr[j] = '\''; break;
						case 'a': returnstr[j] = '\a'; break;
						case 'b': returnstr[j] = '\b'; break;
						case 'f': returnstr[j] = '\f'; break;
						case 'n': returnstr[j] = '\n'; break;
						case 'r': returnstr[j] = '\r'; break;
						case 't': returnstr[j] = '\t'; break;
						case 'v': returnstr[j] = '\v'; break;
						case 'u':
#ifdef HAVE_FROMUNICODE
							strncpy(c,&srcstr[i+1],4); c[4]='\0';										u = fromunicode(strtocodepoint(c,&endptr,16));
							i += (endptr-c);
							if ( (j + strlen(u)) < maxstrlen )
								j += stpcpy(&returnstr[j],u) - &returnstr[j] - 1; // -1 to offset j++ below
							else
								j = maxstrlen;

							free(u);
#else
							anyerrno = EINVAL;
							fprintf(stderr,"%s: Unicode escape sequence not supported\n",progname);
#endif // HAVE_FROMUNICODE
							break;
						case 'U':
#ifdef HAVE_FROMUNICODE
							strncpy(c,&srcstr[i+1],8); c[8]='\0';
							u = fromunicode(strtocodepoint(c,&endptr,16));
							i += (endptr-c);
							if ( (j + strlen(u)) < maxstrlen )
								j += stpcpy(&returnstr[j],u) - &returnstr[j] - 1; // -1 to offset j++ below
							else
								j = maxstrlen;

							free(u);
#else
							anyerrno = EINVAL;
							fprintf(stderr,"%s: Unicode escape sequence not supported\n",progname);
#endif // HAVE_FROMUNICODE
							break;
						case '0': case '1': case '2': case '3':
						case '4': case '5': case '6': case '7':
							if ( srcstr[i] == '0' ) {
								strncpy(c,&srcstr[i],4); c[4]='\0';
							} else {
								strncpy(c,&srcstr[i],3); c[3]='\0';
							}
							returnstr[j] = strtoul(c,&endptr,8);
							i += (endptr-1-c);
							break;
						case '\0':
							i--; // -1 to offset i++ below since we're already End Of String
							// Fall through to unrecognized (i.e. default) since trailing Backslash is also an error
						default:
                               				anyerrno = EINVAL;
                                			fprintf(stderr,"%s: Unrecognized escape sequence \"\\%.*s\" truncated\n",progname,1,&srcstr[i]);
							break;
					}
					j++; i++;
				}
			}
		} else {
			d = -1;
			anyerrno = EFAULT;
			fprintf(stderr,"%s: Internal error processing \"%s\"\n",progname,srcstr);
		}
	}

	returnstr[j] = '\0';

	*returnstrlen = j;
	return returnstr;
}


// This sanitizes a printf format string of any unexpected (and therefore unsupported) specifier (e.g. "%n") and/or read an extra argument (e.g. unprocessed "*") and/or user supplied length specifiers (which may mismatch with actual parameters)
int
sanitize1fmt(char *fmt, size_t fmtlen, char *specifier, size_t specifierlen) {

	char *c;

	// Could also use fmt[strcspn(fmt,ETC)] = '\0' here if we wanted to avoid string pointers
	if ( ( c = strpbrk(fmt,"*$%\\" PRINTF_SPECIFIERS_INVALID PRINTF_SPECIFIERS) ) != NULL ) {
		anyerrno = EINVAL;
		fprintf(stderr,"%s: Illegal format \"%%%s%s\" truncated to \"%%%.*s%s\"\n",progname,fmt,specifier,(int) (c-fmt),fmt,specifier);
		c[0] = '\0';
		fmtlen = (c-fmt);
	}

	if ( ( c = strpbrk(fmt,PRINTF_LENGTHS) ) != NULL ) {
		anyerrno= EINVAL;
		fprintf(stderr,"%s: Formats may not include [%s] and \"%%%s%s\" truncated to \"%%%.*s%s\"\n",progname,PRINTF_LENGTHS,fmt,specifier,(int) (c-fmt),fmt,specifier);
		c[0] = '\0';
		fmtlen = (c-fmt);
	}

	return fmtlen;
}


// This prepares a format string incorporating fmt but also making room for prologue and epilogue text parameters
char *
prep1fmt(size_t *returnlen,
	char *fmt, size_t fmtlen,
	char *length_modifier, size_t length_modifierlen,
	char *specifier, size_t specifierlen) {

	char *ufmt;
	size_t ufmtlen;

	ufmtlen = strlen("%1$s%2$") + fmtlen + length_modifierlen + specifierlen + strlen("%3$s");
	ufmt = malloc((ufmtlen+1) * sizeof(char));
	strcpy(ufmt,"%1$s%2$"); // It is assumed fmt does not include % prefix of the format specification
	strcat(ufmt,fmt);
	strcat(ufmt,length_modifier);
	strcat(ufmt,specifier);
	strcat(ufmt,"%3$s");

	*returnlen = ufmtlen;
	return ufmt;
}


int
printf1arg(char *prologue, size_t prologuelen,
	char *fmt, size_t fmtlen,
	char *specifier, size_t specifierlen,
	char *epilogue, size_t epiloguelen, char *arg) {

	int abort = 0;

	char *uprologue; size_t uprologuelen;
	char *ufmt; size_t ufmtlen;
	char *uepilogue; size_t uepiloguelen;
	char *uarg; size_t uarglen;

#if C_Year >= 1999 
#define	strtosint	strtoll
#define strtouint	strtoull
#define INT_LM	"ll"
	signed long long slli;
	unsigned long long ulli;
#else
#define	strtosint	strtol
#define strtouint	strtoul
#define INT_LM		"l"
	signed long slli;
	unsigned long ulli;
#endif // strtoXint
	double d;

	char *endptr;

	if ( prologuelen > 0 )
		uprologue = unescape(&uprologuelen,prologue,prologuelen,NULL);
	else
		uprologue = "";
	if ( epiloguelen > 0 )
		uepilogue = unescape(&uepiloguelen,epilogue,epiloguelen,NULL);
	else
		uepilogue = "";

	// Just print the text if no formats in this batch
	if ( specifierlen == 0 && fmtlen == 0 )
		// printf("%s",X) is multiple times faster than printf(X)
		printf("%s%s",uprologue,uepilogue);
	else if ( specifierlen == 0 && strcmp(fmt,"%" ) == 0 ) // "Format" of this batch was a "%%"
		printf("%s%%%s",uprologue,uepilogue);
	else {
		// Sanitize fmt for anything that would throw off call printf(3) such as causing it to read an extra argument
		fmtlen = sanitize1fmt(fmt,fmtlen,specifier,specifierlen);
		
		ufmtlen = 0;

		switch(specifier[0]) {
		case 'd':
		case 'i':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,INT_LM,strlen(INT_LM),specifier,specifierlen);

			if ( arg != NULL )
				if ( arg[0] == '\'' || arg[0] == '"' ) {
					wchar_t *warg = malloc(3 * sizeof(wchar_t)); // arg[0] + arg[1] + '\0'

					if ( ( mbstowcs(warg,arg,2) ) == (size_t) -1 ) {
						anyerrno = errno;
						perror("printf format conversion");
						slli = 0;
					} else
						slli = warg[1];

					free(warg);
				} else { // This is intentionally a macro-generated codeblock not a function
					strtonum(slli,strtosint(arg,&endptr,0),arg,endptr)
				}
			else
				slli = 0;

			printf(ufmt,uprologue,slli,uepilogue);

			break;
		case 'x':
		case 'X':
		case 'o':
		case 'u':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,INT_LM,strlen(INT_LM),specifier,specifierlen);

			if ( arg != NULL )
				if ( arg[0] == '\'' || arg[0] == '"' ) {
					wchar_t *warg = malloc(3 * sizeof(wchar_t)); // arg[0] + arg[1] + '\0'

					if ( ( mbstowcs(warg,arg,2) ) == (size_t) -1 ) {
						anyerrno = errno;
						perror("printf format conversion");
						ulli = 0;
					} else
						ulli = warg[1];

					free(warg);
				} else { // This is intentionally a macro-generated codeblock not a function
					strtonum(ulli,strtouint(arg,&endptr,0),arg,endptr)
				}
			else
				ulli = 0;

			printf(ufmt,uprologue,ulli,uepilogue);

			break;
		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),specifier,specifierlen);

			if ( arg != NULL ) { // This is intentionally a macro-generated codeblock not a function
				strtonum(d,strtod(arg,&endptr),arg,endptr)
			} else
				d = 0.0;

			printf(ufmt,uprologue,d,uepilogue);

			break;
		case 's':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),specifier,specifierlen);

			if ( arg != NULL )
				printf(ufmt,uprologue,arg,uepilogue);
			else
				printf(ufmt,uprologue,"",uepilogue);

			break;
		case 'S':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),specifier,specifierlen);

			if ( arg != NULL ) {
				wchar_t *wfmt;
				size_t nwfmt;
				wchar_t *warg;
				size_t nwarg;

				wfmt = malloc((ufmtlen+1) * sizeof(wchar_t)); // Assume wcslen(wfmt) <= strlen(ufmt) && strlen(ufmt) <= ufmtlen
				if ( ( nwfmt = mbstowcs(wfmt,ufmt,ufmtlen+1) ) == (size_t) -1 ) {
					anyerrno = errno;
					perror("printf format conversion");
				} else {
					warg = malloc(ARG_MAX * sizeof(wchar_t));
					if ( ( nwarg = mbstowcs(warg,arg,ARG_MAX) ) == (size_t) -1 ) {
						anyerrno = errno;
						perror("printf argument conversion");
					} else
						wprintf(wfmt,uprologue,warg,uepilogue);

					free(warg);
				}

				free(wfmt);
			} else
				printf(ufmt,uprologue,L"",uepilogue);

			break;
		case 'b':
			// %b -> %s for call to printf(3)
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),"s",strlen("s"));

			if ( arg != NULL ) {
				uarg = unescape(&uarglen,arg,-1,&abort);

				if ( abort == 0 )
					printf(ufmt,uprologue,uarg,uepilogue);
				else
					printf(ufmt,uprologue,uarg,"");

				free(uarg);
			} else
				printf(ufmt,uprologue,"",uepilogue);

			break;
		case 'Q':
			// %Q -> %S for call to printf(3)
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),"S",strlen("S"));

			if ( arg != NULL ) {
				wchar_t *wfmt;
				size_t nwfmt;
				wchar_t *warg;
				size_t nwarg;

				uarg = unescape(&uarglen,arg,-1,&abort);
				wfmt = malloc((ufmtlen+1) * sizeof(wchar_t)); // Assume wcslen(wfmt) <= strlen(ufmt) && strlen(ufmt) <= ufmtlen
				if ( ( nwfmt = mbstowcs(wfmt,ufmt,ufmtlen+1) ) == (size_t) -1 ) {
					anyerrno = errno;
					perror("printf format conversion");
				} else {
					warg = malloc(ARG_MAX * sizeof(wchar_t));
					if ( ( nwarg = mbstowcs(warg,uarg,ARG_MAX) ) == (size_t) -1 ) {
						anyerrno = errno;
						perror("printf argument conversion");
					} else
						if ( abort == 0 )
							wprintf(wfmt,uprologue,warg,uepilogue);
						else
							wprintf(wfmt,uprologue,warg,L"");

					free(warg);
				}

				free(wfmt);
				free(uarg);
			} else
				printf(ufmt,uprologue,L"",uepilogue);

			break;
		case 'c':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),specifier,specifierlen);

			if ( arg != NULL )
				printf(ufmt,uprologue,(int) arg[0],uepilogue);
			else
				printf(ufmt,uprologue,"",uepilogue);

			break;
		case 'C':
			ufmt = prep1fmt(&ufmtlen,fmt,fmtlen,"",strlen(""),specifier,specifierlen);

			if ( arg != NULL ) {
				wchar_t *wfmt;
				size_t nwfmt;
				wchar_t warg;
				size_t nwarg;

				wfmt = malloc((ufmtlen+1) * sizeof(wchar_t)); // Assume wcslen(wfmt) <= strlen(ufmt) && strlen(ufmt) <= ufmtlen
				if ( ( nwfmt = mbstowcs(wfmt,ufmt,ufmtlen+1) ) == (size_t) -1 ) {
					anyerrno = errno;
					perror("printf format conversion");
				} else
					if ( ( nwarg = mbtowc(&warg,arg,MB_LEN_MAX) ) == (size_t) -1 ) {
						anyerrno = errno;
						perror("printf argument conversion");
					} else
						wprintf(wfmt,uprologue,(wint_t) warg,uepilogue);

				free(wfmt);
			} else
				printf(ufmt,uprologue,L"",uepilogue);

			break;
		default: // Should not be reached unless PRINTF_SPECIFIERS contains characters not listed as one of the cases
			anyerrno = EFAULT;
			fprintf(stderr,"%s: Internal error with format %s%s\n",progname,fmt,specifier);

			break;
		}

		if ( ufmtlen > 0 ) free(ufmt);
	}

	if ( epiloguelen > 0 ) free(uepilogue);
	if ( prologuelen > 0 ) free(uprologue);

	return abort;
}


int
parse1fmt(char *s1, size_t *returnn1,
	char *s2, size_t *returnn2,
	char *s3, size_t *returnn3,
	char *s4, size_t *returnn4,
	char *s5, size_t *returnn5, char *fmt, size_t fmtlen) {

	size_t n;
	size_t n1 = 0;
	size_t n2 = 0;
	size_t n3 = 0;
	size_t n4 = 0;
	size_t n5 = 0;
	int d;
	
	if ( (d = sscanf(fmt,"%[^%]%zn%1[%]%zn%[^%" PRINTF_SPECIFIERS "]%zn%1[" PRINTF_SPECIFIERS "]%zn%[^%]%zn",s1,&n1,s2,&n2,s3,&n3,s4,&n4,s5,&n5)) == 5 ) {
			n = n5;
			n5 = n - n4;
			n4 = n4 - n3;
			n3 = n3 - n2;
			n2 = n2 - n1;
		} else {
			switch(d) {
				case 4: // This is ^[^%]+%[^%PRINTF_SPECIFIERS]+[PRINTF_SPECIFIERS](%.*|$)
					n = n4;
					n4 = n - n3;
					n3 = n3 - n2;
					n2 = n2 - n1;
					s5[0] = '\0'; n5 = 0;
					break;
				case 3: // This is ^[^%]+%[^%PRINTF_SPECIFIERS]+$ -> no final printf specifier -> likely invalid printf format -> similar to case 1 except still need to advance past the format and process any remaining text
					anyerrno = EINVAL;
					fprintf(stderr,"%s: Illegal format \"%.*s\" truncated\n",progname,(int) (n3-n1),&fmt[n1]);
					n = n3;
					s2[0] = '\0'; n2 = 0;
					s3[0] = '\0'; n3 = 0;
					s4[0] = '\0'; n4 = 0;
					s5[0] = '\0'; n5 = 0;
					break;
				case 0: // This is ^%.* -> Either a single remaining %, a %% escape, or a printf format (adjudicated by case 2)
					s1[0] = '\0'; n1 = 0;
					strcpy(s2,"%"); n2 = strlen("%");
					if ( (d = sscanf(&fmt[n2],"%[^%" PRINTF_SPECIFIERS "]%zn%1[" PRINTF_SPECIFIERS "]%zn%[^%]%zn",s3,&n3,s4,&n4,s5,&n5)) > 1 ) {
						if ( d == 2 ) {
							n = n2 + n4;
							s5[0] = '\0'; n5 = 0;
						} else { // d == 3
							n = n2 + n5;
							n5 = n5 - n4;
						}
						n4 = n4 - n3;
						break;
					} // else d == 0 -> no match
					// If no match from 2nd sscanf then it's either %% or %[PRINTF_SPECIFIER] or %[INVALID] same as case=2 with no text -> no break and fall through
				case 2: // ^[^%]+%[% or PRINTF_SPECIFIER] -> text followed by simple [no flags/etc] printf format
					if ( fmt[n2] == '%' ) {
						strcpy(s3,"%"); n3 = n2 + strlen("%");
						n = n3;
						n3 = n3 - n2;
						n2 = n2 - n1;
						s4[0] = '\0'; n4 = 0;
						s5[0] = '\0'; n5 = 0;
					} else {
						s3[0] = '\0'; n3 = 0;
						if ( (d = sscanf(&fmt[n2],"%1[" PRINTF_SPECIFIERS "]%zn%[^%]%zn",s4,&n4,s5,&n5)) == 2 ) {
							n = n2 + n5;
							n5 = n5 - n4;
						} else {
							if ( d == 0 ) {
								anyerrno = EINVAL;
								fprintf(stderr,"%s: Illegal format \"%.*s\" truncated to \"%.*s\"\n",progname,(int) (n2 + strnlen(&fmt[n2],1)),&fmt[n1],(int) strnlen(&fmt[n2],1),&fmt[n2]);
//								strncpy(s4,&fmt[n2],1); s4[1]='\0'; n4 = strlen(s4);
								s4[0]='\0'; n4 = 0;
//								n = n2 + strnlen(&fmt[n2],1);
							}
							n = n2 + n4;
							s5[0] = '\0'; n5 = 0;
						}
						n2 = n2 - n1;
					}
					break;
				case 1: // This is ^[^%]+ -> all text no printf format
					n = n1;
					s2[0] = '\0'; n2 = 0;
					s3[0] = '\0'; n3 = 0;
					s4[0] = '\0'; n4 = 0;
					s5[0] = '\0'; n5 = 0;
					break;
				default: // Should never happen -> internal error
					anyerrno = EFAULT;
					fprintf(stderr,"%s: Internal error with format %s\n",progname,fmt);
					n = strlen(fmt);
					break;
			}
		}

	*returnn1 = n1; *returnn2 = n2; *returnn3 = n3; *returnn4 = n4; *returnn5 = n5;

	return n;
}


int
fmtpullparams(char *s3, size_t *n3, int nextarg, char *args[], int numargs) {

	char	*c;
	size_t	fmt_width_i;
	int	fmt_width_arg;
	size_t	fmt_width_strlen;
	size_t	fmt_prec_i;
	int	fmt_prec_arg;
	size_t	fmt_prec_strlen;

	c = strchr(s3,'*');
	if ( c != NULL ) {
		fmt_width_i = (c-s3);
		fmt_width_arg = nextarg;
		if ( args[fmt_width_arg] == NULL )
			fmt_width_strlen=0;
		else
			fmt_width_strlen=strlen(args[nextarg]);
		if ( nextarg < numargs )
			nextarg++;

		c = strstr(&c[1],".*");
		if ( c != NULL ) {
			fmt_prec_i = (c-s3);
			fmt_prec_arg = nextarg;
			if ( args[fmt_prec_arg] == NULL)
				fmt_prec_strlen = 0;
			else
				fmt_prec_strlen = strlen(args[fmt_prec_arg]);
			if ( nextarg < numargs )
				nextarg++;

			if ( fmt_prec_strlen+fmt_width_strlen < 2 )
				memmove(&s3[fmt_prec_i+strlen(".*")+fmt_prec_strlen-1+fmt_width_strlen-1],&s3[fmt_prec_i+strlen(".*")],2-fmt_prec_strlen-fmt_width_strlen);
			else
				memmove(&s3[fmt_prec_i+strlen(".*")+fmt_prec_strlen-1+fmt_width_strlen-1],&s3[fmt_prec_i+strlen(".*")],fmt_prec_strlen-1+fmt_width_strlen-1);
			memmove(&s3[fmt_width_i],args[fmt_width_arg],fmt_width_strlen);
			s3[fmt_width_i+fmt_width_strlen]='.';
			memmove(&s3[fmt_width_i+fmt_width_strlen+1],args[fmt_prec_arg],fmt_prec_strlen);
		} else {
			if ( fmt_width_strlen < 1 )
				memmove(&s3[fmt_width_i+1+fmt_width_strlen-1],&s3[fmt_width_i+1],1-fmt_width_strlen);
			else
				memmove(&s3[fmt_width_i+1+fmt_width_strlen-1],&s3[fmt_width_i+1],fmt_width_strlen-1);
			memmove(&s3[fmt_width_i],args[fmt_width_arg],fmt_width_strlen);
		}
	}

	return nextarg;
}


int
parsefmt(char *fmt, size_t fmtlen, char *args[], int numargs) {

	int nextarg = 0;
	char *s1 = malloc((fmtlen+1) * sizeof(char));
	char s2[2];
	char *s3 = malloc((fmtlen+1) * sizeof(char));
	char s4[2];
	char *s5 = malloc((fmtlen+1) * sizeof(char));
	size_t n1,n2,n3,n4,n5;
	size_t n;

	while ( fmt[0] != '\0' ) {
		n = parse1fmt(s1,&n1,s2,&n2,s3,&n3,s4,&n4,s5,&n5,fmt,fmtlen);

		if ( n3 > 0 )
			nextarg = fmtpullparams(s3,&n3,nextarg,args,numargs);

		if ( printf1arg(s1,n1,s3,n3,s4,n4,s5,n5,args[nextarg]) != 0 ) {
			free(s5); free(s3); free(s1);
			return numargs;
		}
		if ( n4 > 0 && nextarg < numargs )
			nextarg++;
		fmt += n;
	}

	free(s5); free(s3); free(s1);

	return nextarg;
}


void
usage(void)
{
	fprintf(stderr, "usage: printf format [arguments ...]\n");
}


int
main (int argc, char *argv[]) {

	char *fmt;
	int nextarg;

// Use hardcoded strings until call to setlocale(3)
#ifdef HAVE_PLEDGE
	if (pledge("stdio", NULL) == -1)
#ifdef __OpenBSD__
		err(1, "pledge");
#else
	{
		perror("printf: pledge");
		exit(EXIT_FAILURE);
	}
#endif //OpenBSD
#endif //HAVE_PLEDGE

	anyerrno = 0;

	nextarg=0;
	if ( argc > nextarg ) {
		progname = argv[nextarg]; nextarg++;

		if ( setlocale(LC_ALL, "") == NULL )
			fprintf(stderr,"%s: Warning: current locale not valid\n",progname); // Assume that if argv[0] exists it is a valid string in the default C locale but without a successful setlocale(3) just fallback to a hardcoded string for the rest

		if ( argc > nextarg ) {
			fmt = argv[nextarg]; nextarg++;

			do
				nextarg += parsefmt(fmt,ARG_MAX-1,&argv[nextarg],argc-nextarg);
			while ( nextarg>2 && nextarg < argc ); // If nextarg==2 then exit after one pass since that means no arguments were consumed by fmt
		} else {
			usage();
			anyerrno = EINVAL;
		}
	} else
		anyerrno = EFAULT;

	if ( anyerrno > 0 )
#ifdef EX_SOFTWARE
		if ( anyerrno == EFAULT )
			return EX_SOFTWARE;
		else
#endif // sysexits code for internal error
			return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}

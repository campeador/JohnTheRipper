/*
 * This is Base64 conversion code. It will convert between raw and base64
 * all flavors, hex and base64 (all flavors), and between 2 flavors of
 * base64.  Conversion happens either direction (to or from).
 *
 * Coded Fall 2014 by Jim Fougeron.  Code placed in public domain.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted, as long an unmodified copy of this
 * license/disclaimer accompanies the source.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 *  currently handles these conversions (to and from any to any)
 *     raw      (binary)
 *     hex
 *     mime     (A..Za..z0..1+/   The == for null trails may be optional, removed for now)
 *     crypt    (./0..9A..Za..Z   Similar to encoding used by crypt)
 *     cryptBS  like crypt, but bit swapped encoding order
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include "missing_getopt.h"
#endif
#include "memory.h"
#include "common.h"
#include "jumbo.h"
#include "base64.h"
#include "base64_convert.h"
#include "memdbg.h"

#define ERR_base64_unk_from_type	-1
#define ERR_base64_unk_to_type		-2
#define ERR_base64_to_buffer_sz		-3
#define ERR_base64_unhandled		-4

/* mime variant of base64, like crypt version in common.c */
static const char *itoa64m = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char atoi64m[0x100];
static int mime_setup=0;

// Length macros which convert from one system to the other.
// RAW_TO_B64_LEN(x) returns 'exact' base64 string length needed. NOTE, some base64 will padd to
//     an even 4 characters.  The length from this macro DOES NOT include padding values.
// B64_TO_RAW_LEN(x) returns raw string length needed for the base-64 string that is NOT padded
//     in any way (i.e.  needs to be same value as returned from RAW_TO_B64_LEN(x)  )
#define RAW_TO_B64_LEN(a) (((a)*4+2)/3)
#define B64_TO_RAW_LEN(a) (((a)*3+1)/4)

static void base64_unmap_i(char *in_block) {
	int i;
	char *c;

	for(i=0; i<4; i++) {
		c = in_block + i;
		if(*c == '.') { *c = 0; continue; }
		if(*c == '/') { *c = 1; continue; }
		if(*c>='0' && *c<='9') { *c -= '0'; *c += 2; continue; }
		if(*c>='A' && *c<='Z') { *c -= 'A'; *c += 12; continue; }
		if(*c>='a' && *c<='z') { *c -= 'a'; *c += 38; continue; }
		*c = 0;
	}
}
static void base64_decode_i(const char *in, int inlen, unsigned char *out) {
	int i, done=0;
	unsigned char temp[4];

	for(i=0; i<inlen; i+=4) {
		memcpy(temp, in, 4);
		memset(out, 0, 3);
		base64_unmap_i((char*)temp);
		out[0] = ((temp[0]<<2) & 0xfc) | ((temp[1]>>4) & 3);
		done += 2;
		if (done >= inlen) return;
		out[1] = ((temp[1]<<4) & 0xf0) | ((temp[2]>>2) & 0xf);
		if (++done >= inlen) return;
		out[2] = ((temp[2]<<6) & 0xc0) | ((temp[3]   ) & 0x3f);
		++done;
		out += 3;
		in += 4;
	}
}
static void base64_decode_iBS(const char *in, int inlen, unsigned char *out) {
	int i, done=0;
	unsigned char temp[4];

	for(i=0; i<inlen; i+=4) {
		memcpy(temp, in, 4);
		memset(out, 0, 3);
		base64_unmap_i((char*)temp);
		out[0] = ((temp[0]   ) & 0x3f) | ((temp[1]<<6) & 0xc0);
		done += 2;
		if (done >= inlen) return;
		out[1] = ((temp[1]>>2) & 0x0f) | ((temp[2]<<4) & 0xf0);
		if (++done >= inlen) return;
		out[2] = ((temp[2]>>4) & 0x03) | ((temp[3]<<2) & 0xfc);

		++done;
		out += 3;
		in += 4;
	}
}
static void enc_base64_1_iBS(char *out, unsigned val, unsigned cnt) {
	while (cnt--) {
		unsigned v = val & 0x3f;
		val >>= 6;
		*out++ = itoa64[v];
	}
}
static void base64_encode_iBS(const unsigned char *in, int len, char *outy) {
	int mod = len%3, i;
	unsigned u;
	for (i = 0; i*3 < len; ++i) {
		u = (in[i*3] | (((unsigned)in[i*3+1])<<8)  | (((unsigned)in[i*3+2])<<16));
		if ((i+1)*3 >= len) {
			switch (mod) {
				case 0:
					enc_base64_1_iBS(outy, u, 4); outy[4] = 0; break;
				case 1:
					enc_base64_1_iBS(outy, u, 2); outy[2] = 0; break;
				case 2:
					enc_base64_1_iBS(outy, u, 3); outy[3] = 0; break;
			}
		}
		else
			enc_base64_1_iBS(outy, u, 4);
		outy += 4;
	}
}
static void enc_base64_1_i(char *out, unsigned val, unsigned cnt) {
	while (cnt--) {
		unsigned v = (val & 0xFC0000)>>18;
		val <<= 6;
		*out++ = itoa64[v];
	}
}
static void base64_encode_i(const unsigned char *in, int len, char *outy) {
	int mod = len%3, i;
	unsigned u;
	for (i = 0; i*3 < len; ++i) {
		u = ((((unsigned)in[i*3])<<16) | (((unsigned)in[i*3+1])<<8)  | (((unsigned)in[i*3+2])));
		if ((i+1)*3 >= len) {
			switch (mod) {
				case 0:
					enc_base64_1_i(outy, u, 4); outy[4] = 0; break;
				case 1:
					enc_base64_1_i(outy, u, 2); outy[2] = 0; break;
				case 2:
					enc_base64_1_i(outy, u, 3); outy[3] = 0; break;
			}
		}
		else
			enc_base64_1_i(outy, u, 4);
		outy += 4;
	}
}
static void enc_base64_1(char *out, unsigned val, unsigned cnt) {
	while (cnt--) {
		unsigned v = (val & 0xFC0000)>>18;
		val <<= 6;
		*out++ = itoa64m[v];
	}
}
/* mime char set */
static void base64_encode(const unsigned char *in, int len, char *outy) {
	int mod = len%3, i;
	unsigned u;
	for (i = 0; i*3 < len; ++i) {
		u = ((((unsigned)in[i*3])<<16) | (((unsigned)in[i*3+1])<<8)  | (((unsigned)in[i*3+2])));
		if ((i+1)*3 >= len) {
			switch (mod) {
				case 0:
					enc_base64_1(outy, u, 4); outy[4] = 0; break;
				case 1:
					enc_base64_1(outy, u, 2); outy[2] = 0; break;
				case 2:
					enc_base64_1(outy, u, 3); outy[3] = 0; break;
			}
		}
		else
			enc_base64_1(outy, u, 4);
		outy += 4;
	}
}
static void raw_to_hex(const unsigned char *from, int len, char *to) {
	int i;
	for (i = 0; i < len; ++i) {
		*to++ = itoa16[(*from)>>4];
		*to++ = itoa16[(*from)&0xF];
		++from;
	}
	*to = 0;
}
static void hex_to_raw(const char *from, int len, unsigned char *to) {
	int i;
	for (i = 0; i < len; i += 2)
		*to++ = (atoi16[(ARCH_INDEX(from[i]))]<<4)|atoi16[(ARCH_INDEX(from[i+1]))];
	*to = 0;
}

/* these functions should allow us to convert 4 base64 bytes at a time, and not */
/* have to allocate a large buffer, decrypt to one, and re-encrypt just to do a */
/* conversion.  With these functions we should be able to walk through a buffer */
static int mime_to_cryptBS(const char *cpi, char *cpo) {
	char Tmp[3], *cpo_o = cpo;
	int len, len_left = strlen(cpi);
	len = len_left;
	while (len_left > 0) {
		base64_decode((char*)cpi, len_left < 4 ? len_left : 4, Tmp);
		base64_encode_iBS((const unsigned char*)Tmp, 3, cpo);
		cpi += 4;
		cpo += 4;
		len_left -= 4;
	}
	cpo_o[len] = 0;
	return strlen(cpo_o);
}
static int mime_to_crypt(const char *cpi, char *cpo) {
	char Tmp[3], *cpo_o = cpo;
	int len, len_left = strlen(cpi);
	len = len_left;
	while (len_left > 0) {
		base64_decode((char*)cpi, len_left < 4 ? len_left : 4, Tmp);
		base64_encode_i((const unsigned char*)Tmp, 3, cpo);
		cpi += 4;
		cpo += 4;
		len_left -= 4;
	}
	cpo_o[len] = 0;
	return strlen(cpo_o);
}
static int crypt_to_cryptBS(const char *cpi, char *cpo) {
	char Tmp[3], *cpo_o = cpo;
	int len, len_left = strlen(cpi);
	len = len_left;
	while (len_left > 0) {
		base64_decode_i((char*)cpi, len_left < 4 ? len_left : 4, (unsigned char*)Tmp);
		base64_encode_iBS((const unsigned char*)Tmp, 3, cpo);
		cpi += 4;
		cpo += 4;
		len_left -= 4;
	}
	cpo_o[len] = 0;
	return strlen(cpo_o);
}
static int crypt_to_mime(const char *cpi, char *cpo) {
	char Tmp[3], *cpo_o = cpo;
	int len, len_left = strlen(cpi);
	len = len_left;
	while (len_left > 0) {
		base64_decode_i((char*)cpi, len_left < 4 ? len_left : 4, (unsigned char*)Tmp);
		base64_encode((const unsigned char*)Tmp, 3, cpo);
		cpi += 4;
		cpo += 4;
		len_left -= 4;
	}
	cpo_o[len] = 0;
	return strlen(cpo_o);
}
static int cryptBS_to_mime(const char *cpi, char *cpo) {
	char Tmp[3], *cpo_o = cpo;
	int len, len_left = strlen(cpi);
	len = len_left;
	while (len_left > 0) {
		base64_decode_iBS((char*)cpi, len_left < 4 ? len_left : 4, (unsigned char*)Tmp);
		base64_encode((const unsigned char*)Tmp, 3, cpo);
		cpi += 4;
		cpo += 4;
		len_left -= 4;
	}
	cpo_o[len] = 0;
	return strlen(cpo_o);
}
static int cryptBS_to_crypt(const char *cpi, char *cpo) {
	char Tmp[3], *cpo_o = cpo;
	int len, len_left = strlen(cpi);
	len = len_left;
	while (len_left > 0) {
		base64_decode_iBS((char*)cpi, len_left < 4 ? len_left : 4, (unsigned char*)Tmp);
		base64_encode_i((const unsigned char*)Tmp, 3, cpo);
		cpi += 4;
		cpo += 4;
		len_left -= 4;
	}
	cpo_o[len] = 0;
	return strlen(cpo_o);
}

static void setup_mime() {
	const char *pos;
	mime_setup=1;
	memset(atoi64m, 0x7F, sizeof(atoi64m));
	for (pos = itoa64m; pos <= &itoa64m[63]; pos++)
		atoi64m[ARCH_INDEX(*pos)] = pos - itoa64m;
	/* base64conv tool does not have common_init called by JtR. We have to do it ourselves */
	common_init();
}

char *base64_convert_cp(const void *from, b64_convert_type from_t, int from_len, void *to, b64_convert_type to_t, int to_len, unsigned flags)
{
	int err = base64_convert(from, from_t, from_len, to, to_t, to_len, flags);
	if (err < 0) {
		base64_convert_error_exit(err);
	}
	return (char*)to;
}
int base64_convert(const void *from, b64_convert_type from_t, int from_len, void *to, b64_convert_type to_t, int to_len, unsigned flags)
{
	unsigned char *tmp;
	if (!mime_setup)
		setup_mime();

	switch (from_t) {
		case e_b64_raw:		/* raw memory */
		{
			switch(to_t) {
				case e_b64_raw:		/* raw memory */
				{
					if (from_t > to_t)
						return ERR_base64_to_buffer_sz;
					memcpy(to, from, from_len);
					return from_len;
				}
				case e_b64_hex:		/* hex */
				{
					if ((from_t*2+1) > to_t)
						return ERR_base64_to_buffer_sz;
					raw_to_hex((unsigned char*)from, from_len, (char*)to);
					if (flags&flg_Base64_HEX_UPCASE)
						strupr((char*)to);
					return from_len<<1;
				}
				case e_b64_mime:	/* mime */
				{
					base64_encode((unsigned char*)from, from_len, (char*)to);
					return strlen((char*)to);
				}
				case e_b64_crypt:	/* crypt encoding */
				{
					base64_encode_i((unsigned char*)from, from_len, (char*)to);
					return strlen((char*)to);
				}
				case e_b64_cryptBS:	/* crypt encoding, network order (used by WPA, cisco9, etc) */
				{
					base64_encode_iBS((unsigned char*)from, from_len, (char*)to);
					return strlen((char*)to);
				}
				default:
					return ERR_base64_unk_to_type;
			}
		}
		case e_b64_hex:		/* hex */
		{
			from_len = strlen((char*)from);
			switch(to_t) {
				case e_b64_raw:		/* raw memory */
				{
					if (to_len * 2 < from_len)
						return ERR_base64_to_buffer_sz;
					hex_to_raw((const char*)from, from_len, (unsigned char*)to);
					return from_len / 2;
				}
				case e_b64_hex:		/* hex */
				{
					from_len = strlen((char*)from);
					if (to_len < strlen((char*)from)+1)
						return ERR_base64_to_buffer_sz;
					strcpy((char*)to, (const char*)from);
					if (to_t != from_t) {
						if (to_t == e_b64_hex)
							strlwr((char*)to);
						else
							strlwr((char*)to);
					}
					return from_len;
				}
				case e_b64_mime:	/* mime */
				{
					tmp = (unsigned char*)mem_alloc(from_len/2);
					hex_to_raw((const char*)from, from_len, tmp);
					base64_encode((unsigned char*)tmp, from_len/2, (char*)to);
					MEM_FREE(tmp);
					return strlen((char*)to);
				}
				case e_b64_crypt:	/* crypt encoding */
				{
					tmp = (unsigned char*)mem_alloc(from_len/2);
					hex_to_raw((const char*)from, from_len, tmp);
					base64_encode_i((unsigned char*)tmp, from_len/2, (char*)to);
					MEM_FREE(tmp);
					return strlen((char*)to);
				}
				case e_b64_cryptBS:	/* crypt encoding, network order (used by WPA, cisco9, etc) */
				{
					tmp = (unsigned char*)mem_alloc(from_len/2);
					hex_to_raw((const char*)from, from_len, tmp);
					base64_encode_iBS((unsigned char*)tmp, from_len/2, (char*)to);
					MEM_FREE(tmp);
					return strlen((char*)to);
				}
				default:
					return ERR_base64_unk_to_type;
			}
		}
		case e_b64_mime:	/* mime */
		{
			const char *cp = (const char*)from;
			from_len = strlen(cp);
			while (cp[from_len-1]=='=' || cp[from_len-1]=='.')
				from_len--;

			switch(to_t) {
				case e_b64_raw:		/* raw memory */
				{
					// TODO, validate to_len
					base64_decode((char*)from, from_len, (char*)to);
					return B64_TO_RAW_LEN(from_len);
				}
				case e_b64_hex:		/* hex */
				{
					// TODO, validate to_len
					tmp = (unsigned char*)mem_alloc(from_len);
					base64_decode((char*)from, from_len, (char*)tmp);
					raw_to_hex(tmp, B64_TO_RAW_LEN(from_len), (char*)to);
					MEM_FREE(tmp);
					if (flags&flg_Base64_HEX_UPCASE)
						strupr((char*)to);
					return strlen((char*)to);
				}
				case e_b64_mime:	/* mime */
				{
					if (to_len < from_len+1)
						return ERR_base64_to_buffer_sz;
					memcpy(to, from, from_len);
					((char*)to)[from_len] = 0;
					return from_len;
				}
				case e_b64_crypt:	/* crypt encoding */
				{
					int len = mime_to_crypt((const char *)from, (char *)to);
					return len;
				}
				case e_b64_cryptBS:	/* crypt encoding, network order (used by WPA, cisco9, etc) */
				{
					int len = mime_to_cryptBS((const char *)from, (char *)to);
					return len;
				}
				default:
					return ERR_base64_unk_to_type;
			}
		}
		case e_b64_crypt:	/* crypt encoding */
		{
			const char *cp = (const char*)from;
			from_len = strlen(cp);
			while (cp[from_len-1]=='.' && cp[from_len-2]=='.')
				from_len--;
			if (cp[from_len-1]=='.' && from_len%4 != 2)
				from_len--;
			switch(to_t) {
				case e_b64_raw:		/* raw memory */
				{
					// TODO, validate to_len
					base64_decode_i((char*)from, from_len, (unsigned char*)to);
					return B64_TO_RAW_LEN(from_len);
				}
				case e_b64_hex:		/* hex */
				{
					// TODO, validate to_len
					tmp = (unsigned char*)mem_alloc(from_len);
					base64_decode_i((char*)from, from_len, (unsigned char*)tmp);
					raw_to_hex(tmp, B64_TO_RAW_LEN(from_len), (char*)to);
					MEM_FREE(tmp);
					if (flags&flg_Base64_HEX_UPCASE)
						strupr((char*)to);
					return strlen((char*)to);
				}
				case e_b64_mime:	/* mime */
				{
					int len = crypt_to_mime((const char *)from, (char *)to);
					return len;
				}
				case e_b64_crypt:	/* crypt encoding */
				{
					if (to_len < from_len+1)
						return ERR_base64_to_buffer_sz;
					memcpy(to, from, from_len);
					((char*)to)[from_len]=0;
					return from_len;
				}
				case e_b64_cryptBS:	/* crypt encoding, network order (used by WPA, cisco9, etc) */
				{
					int len = crypt_to_cryptBS((const char *)from, (char *)to);
					return len;
				}
				default:
					return ERR_base64_unk_to_type;
			}
		}
		case e_b64_cryptBS:	/* crypt encoding, network order (used by WPA, cisco9, etc) */
		{
			const char *cp = (const char*)from;
			from_len = strlen(cp);
			while (cp[from_len-1]=='.' && cp[from_len-2]=='.')
				from_len--;
			if (cp[from_len-1]=='.' && from_len%4 != 2)
				from_len--;
			switch(to_t) {
				case e_b64_raw:		/* raw memory */
				{
					 // TODO, validate to_len
					base64_decode_iBS((char*)from, from_len, (unsigned char*)to);
					return B64_TO_RAW_LEN(from_len);
				}
				case e_b64_hex:		/* hex */
				{
					// TODO, validate to_len
					unsigned char *tmp = (unsigned char*)mem_alloc(from_len);
					base64_decode_iBS((char*)from, from_len, (unsigned char*)tmp);
					raw_to_hex(tmp, B64_TO_RAW_LEN(from_len), (char*)to);
					MEM_FREE(tmp);
					if (flags&flg_Base64_HEX_UPCASE)
						strupr((char*)to);
					return strlen((char*)to);
				}
				case e_b64_mime:	/* mime */
				{
					int len = cryptBS_to_mime((const char *)from, (char *)to);
					return len;
				}
				case e_b64_crypt:	/* crypt encoding */
				{
					int len = cryptBS_to_crypt((const char *)from, (char *)to);
					return len;
				}
				case e_b64_cryptBS:	/* crypt encoding, network order (used by WPA, cisco9, etc) */
				{
					memcpy(to, from, from_len);
					((char*)to)[from_len] = 0;
					return from_len;
				}
				default:
					return ERR_base64_unk_to_type;
			}
		}
		default:
			return ERR_base64_unk_from_type;
	}
	return 0;
}
void base64_convert_error_exit(int err) {
	// TODO: add error codes when created.
	switch (err) {
		case ERR_base64_unk_from_type:	fprintf (stderr, "base64_convert error, Unknown From Type\n", err); break;
		case ERR_base64_unk_to_type:	fprintf (stderr, "base64_convert error, Unknown To Type\n", err); break;
		case ERR_base64_to_buffer_sz:	fprintf (stderr, "base64_convert error, *to buffer too small\n", err); break;
		case ERR_base64_unhandled:		fprintf (stderr, "base64_convert error, currently unhandled conversion\n", err); break;
		default:						fprintf (stderr, "base64_convert_error_exit(%d)\n", err);
	}
	exit(1);
}
char *base64_convert_error(int err) {
	char *p = (char*)mem_alloc(256);
	switch (err) {
		case ERR_base64_unk_from_type:	sprintf(p, "base64_convert error, Unknown From Type\n", err); break;
		case ERR_base64_unk_to_type:	sprintf(p, "base64_convert error, Unknown To Type\n", err); break;
		case ERR_base64_to_buffer_sz:	sprintf(p, "base64_convert error, *to buffer too small\n", err); break;
		case ERR_base64_unhandled:		sprintf(p, "base64_convert error, currently unhandled conversion\n", err); break;
		default:						sprintf(p, "base64_convert_error_exit(%d)\n", err);
	}
	return p;
}

static int usage(char *name)
{
	fprintf(stderr, "Usage: %s [-i input_type] [-o output_type] [-q] [-e] data [data ...]\n"
	        "\tdata must match input_type (if hex, then data should be in hex)\n"
			"\t-q will only output resultant string. No extra junk text\n"
			"\t-e turns on buffer overwrite error checking logic\n"
			"\tinput/output types:\n"
			"\t\traw\traw data byte\n"
			"\t\thex\thexidecimal string (for input, case does not matter)\n"
			"\t\tmime\tbase64 mime encoding\n"
			"\t\tcrypt\tbase64 crypt character set encoding\n"
			"\t\tcryptBS\tbase64 crypt encoding, byte swapped\n"
			"",
	        name);
	return EXIT_FAILURE;
}

static b64_convert_type str2convtype(const char *in) {
	if (!strcmp(in, "raw")) return e_b64_raw;
	if (!strcmp(in, "hex")) return e_b64_hex;
	if (!strcmp(in, "mime")) return e_b64_mime;
	if (!strcmp(in, "crypt")) return e_b64_crypt;
	if (!strcmp(in, "cryptBS")) return e_b64_cryptBS;
	return e_b64_unk;
}

/* simple conerter of strings or raw memory */
int base64conv(int argc, char **argv) {
	int c;
	b64_convert_type in_t=e_b64_unk, out_t=e_b64_unk;
	int quiet=0,err_chk=0;

	/* Parse command line */
	while ((c = getopt(argc, argv, "i:o:q!e!")) != -1) {
		switch (c) {
		case 'i':
			in_t = str2convtype(optarg);
			if (in_t == e_b64_unk) {
				fprintf(stderr, "%s error: invalid input type %s\n", argv[0], optarg);
				return usage(argv[0]);
			}
			break;
		case 'o':
			out_t = str2convtype(optarg);
			if (out_t == e_b64_unk) {
				fprintf(stderr, "%s error: invalid output type %s\n", argv[0], optarg);
				return usage(argv[0]);
			}
			break;
		case 'q':
			quiet=1;
			break;
		case 'e':
			err_chk=1;
			break;
		case '?':
		default:
			return usage(argv[0]);
		}
	}
	argc -= optind;
	if(argc == 0)
		return usage(argv[0]);
	argv += optind;

	while(argc--) {
		char *po = (char*)mem_calloc(strlen(*argv)*3);
		int i, len;
		if (err_chk)
			memset(po, 2, strlen(*argv)*3);
		if (!quiet)
			printf("%s  -->  ", *argv);
		len=base64_convert(*argv, in_t, strlen(*argv), po, out_t, strlen(*argv)*3, 0);
		po[len] = 0;
		printf("%s\n", po);
		fflush(stdout);
		// check for overwrite problems
		if (err_chk) {
			int tot = strlen(*argv)*3;
			i=len;
			if (po[i]) {
				fprintf(stderr, "OverwriteLogic: Null byte missing\n");
			}
			for (++i; i < tot; ++i)
			{
				if (((unsigned char)po[i]) != 2) {
					if (i-len > 2)
					fprintf(stderr, "OverwriteLogic: byte %c (%02X) located at offset %d (%+d)\n", (unsigned char)po[i], (unsigned char)po[i], i, i-len);
				}
			}
		}
		MEM_FREE(po);
		++argv;
	}
	MEMDBG_PROGRAM_EXIT_CHECKS(stderr);
	return 0;
}
/*
 * Copyright (c) 2004
 *      Christian Gut.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "externs.h"

int read_pass(char *, size_t);
int write_pass(char *);
int validate_cpass(char *, char *);
int gen_salt(char *, size_t);
void secretusage(void);

char *bcrypt_gensalt(u_int8_t);

/* read_pass reads the (blowfish crypted) password from a file */
int
read_pass(char *pass, size_t size)
{
	FILE *pwdhandle;

	pwdhandle = fopen(NSHPASSWD_TEMP, "r");
	if (pwdhandle == NULL)
		return (0);
	if (fgets(pass, size, pwdhandle) == NULL && ferror(pwdhandle)) {
		fclose(pwdhandle);
		return (0);
	}
	fclose(pwdhandle);

	return (1);
}

/* write the crypted password to the passwd-temp file */
int
write_pass(char *cpass)
{
	FILE *pwdhandle;

	umask(S_IWGRP | S_IRWXO);
	/* maybe we should flock here? */
	pwdhandle = fopen(NSHPASSWD_TEMP, "w");
	if (pwdhandle == NULL) {
		printf("%% Unable to write run-time crypt repository: %s\n",
		       strerror(errno));
		return (0);
	}
	fprintf(pwdhandle, "%s", cpass);
	fclose(pwdhandle);

	return (1);
}

/* sanity checks for crypted password input */
int
validate_cpass(char *cipher, char *cpass)
{
	const char *errstr;
	char *rounds, *pw;

	if (!isprefix(cipher, "blowfish")) {
		printf("%% Invalid cipher: %s\n", cipher);
		return 0;
	}

	if (strlen(cpass) > _PASSWORD_LEN) {
		printf("%% Encrypted password is too long\n");
		return 0;
	}

	/* version number: 2b for blowfish */
	if (cpass[0] != '$' || cpass[1] != '2' || cpass[2] != 'b') {
		printf("%% Invalid crypted password version number\n");
		return 0;
	}

	/* Parse the number of rounds. */
	if (cpass[3] != '$') {
		printf("%% Number of blowfish rounds missing from crypted password\n");
		return 0;
	}
	rounds = &cpass[4];
	while (*rounds == '0') /* skip leading zeroes */
		rounds++;

	pw = strchr(rounds, '$');
	if (pw == NULL) {
		printf("%% Encrypted password string is missing\n");
		return 0;
	}

	/* Copy rounds into NUL-terminated buffer for strtonum() */
	rounds = strndup(rounds, pw - rounds);
	if (rounds == NULL) {
		printf("%% validate_cpass: strndup: %s\n", strerror(errno));
		return 0;
		
	}
	strtonum(rounds, 4, 31, &errstr);
	free(rounds);
	if (errstr) {
		printf("%% Number of blowfish rounds is %s\n", errstr);
		return 0;
	}

	pw++; /* skip over '$' separator */
	if (pw[0] == '\0') {
		printf("%% Encrypted password string is empty\n");
		return 0;
	}

	return 1;
}

int
gen_salt(char *salt, size_t saltlen)
{
	/* 6 is a rounds value like from localcipher option of login.conf */
	strlcpy(salt, bcrypt_gensalt(6), saltlen);
	return 1;
}

void
secretusage(void)
{
	printf("%% enable secret <password>\t\tSet password"
	       "(plaintext)\n");
	printf("%% enable secret <cipher> <hash>\t\tSet"
	       " password(ciphertext)\n");
}

/*
 * enable privileged mode
 */
int
enable(int argc, char **argv)
{
	char *p, *cpass;
	char salt[_PASSWORD_LEN];
	char pass[_PASSWORD_LEN + 1];

	switch (argc) {

	case 1:
		if (priv == 1)
			return 0;

		/* try to read pass */
		if (!(read_pass(pass, sizeof(pass)))) {
			if (errno == ENOENT) {
				/* no password file, so enable */
				priv = 1;
				return 1;
			} else {
				/* cant read password file */
				printf("%% Unable to read %s: %s\n",
				       NSHPASSWD_TEMP, strerror(errno));
				return 0;
			}
		}
		p = getpass("Password:");
		if (p == NULL || *p == '\0')
			return 0;

		if (strcmp(crypt(p, pass), pass) == 0) {
			priv = 1;
			return 1;
		} else {
			printf("%% Password incorrect\n");
			return 0;
		}

	case 2:
		if (argv[1][0] == '?') {
			/* print help */
			printf("%% enable\t\t\t\tEnable privileged mode\n");
			printf("%% enable ?\t\t\t\tPrint help information\n");
			secretusage();
			return 1;
		} else {
			if (isprefix(argv[1], "secret")) {
				secretusage();
				return 1;
			}
			printf("%% Invalid argument: %s\n", argv[1]);
			return 0;
		}

	case 3:
		if (!isprefix(argv[1], "secret")) {
			printf("%% Invalid argument: %s\n", argv[1]);
			return 0;
		}

		if (config_mode != 1) {
			printf("%% Configuration mode required\n");
			return 0;
		}
		if (priv != 1) { /* redundant, but kept here just in case */
			printf("%% Privilege required\n");
			return 0;
		}

		if (strlen(argv[2]) < 8) {
			printf("%% Password too short; at least 8 characters required\n");
			return 0;
		}
		if (strlen(argv[2]) > _PASSWORD_LEN) {
			printf("%% Password too long; at most %d characters allowed\n",
			    _PASSWORD_LEN);
			return 0;
		}
			
		/* crypt plaintext and save as pass */
		strlcpy(pass, argv[2], sizeof(pass));
		gen_salt(salt, sizeof(salt));
		if ((cpass = crypt(pass, salt)) == NULL) {
			printf("%% crypt failed\n");
			return 0;
		}
		return(write_pass(cpass));

	case 4:
		if (!isprefix(argv[1], "secret")) {
			printf("%% Invalid argument: %s\n", argv[2]);
			return 0;
		}

		if (!isprefix(argv[2], "blowfish")) {
			printf("%% Invalid cipher: %s\n", argv[2]);
			return 0;
		}

		if (config_mode != 1) {
			printf("%% Configuration mode required\n");
			return 0;
		}
		if (priv != 1) { /* redundant, but kept here just in case */
			printf("%% Privilege required\n");
			return 0;
		}

		if (!validate_cpass(argv[2], argv[3]))
			return 0;

		/* set crypted pass */
		strlcpy(pass, argv[3], sizeof(pass));
		return (write_pass(pass));

	default:
		printf("%% Too many arguments\n");
		return 0;
	}

}

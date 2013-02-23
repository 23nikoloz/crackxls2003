/* crackxls2003.c - recover encryption keys for Microsoft Excel 2003 worksheets
 *
 * Copyright (C) 2013 Gavin Smith
 * 
 * This file is distributed under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. */ 

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>

#include <time.h>
#include <gmp.h>
#include <signal.h>

#ifdef USE_SOLAR
#include "solar-md5/md5.h"
#else
#include <openssl/md5.h>
#endif
#include <openssl/rc4.h>

const char *file_name;
int flag_test_speed = 0;
clock_t start_time, end_time;

/* encrypted hash_and_verifier */
uint8_t data[32];

/* we will take the md5 hash of the last 16 bytes */
/* 80 = 16 + 16 + 48 */
uint8_t hash_and_verifier[80];

uint32_t md5[4];

/* real_key is appended with 00 00 00 00 */
/* and then md5'd which uses 64 byte blocks */
uint32_t real_key[16];

/* Used to calculate the total number of keys tested */
uint32_t real_key_start[2];

void print_hex (uint8_t *array, int n);

void cracking_stats (void)
{
	mpz_t n_keys;
	mpf_t n_keys_f;
	char *n_keys_str;

	mpz_t mpz_low;

	double time_used;
	mpf_t mpf_time_used;

	double keys_per_second;

	end_time = clock();
	time_used = ((double) (end_time - start_time)) / CLOCKS_PER_SEC;

	printf("CPU time used: %f seconds\n", time_used);

	/* Value of subtraction will always be positive */
	mpz_init_set_si (n_keys, real_key[1] - real_key_start[1]);

	/* multiply by 2^32 */
	mpz_mul_si (n_keys, n_keys, 1 << 16);
	mpz_mul_si (n_keys, n_keys, 1 << 16);

	/* n_keys += real_key[0] - real_key_start[0] + 1 */
	mpz_init_set_ui(mpz_low, real_key[0]);
	mpz_sub_ui(mpz_low, mpz_low, real_key_start[0]);
	mpz_add_ui(mpz_low, mpz_low, 1);
	mpz_add (n_keys, n_keys, mpz_low);
	
	n_keys_str = mpz_get_str (NULL, 10, n_keys);
	printf("Number of keys tested: %s\n", n_keys_str);
	free (n_keys_str);

	if (time_used == 0.0) return; /* Don't / by 0 */
	mpf_init (n_keys_f);
	mpf_set_z (n_keys_f, n_keys);
	mpf_init_set_d (mpf_time_used, time_used);
	mpf_div (n_keys_f, n_keys_f, mpf_time_used); 

	keys_per_second = mpf_get_d (n_keys_f);
	printf("Number of keys tested / second: %f\n", keys_per_second);
}
	
MD5_CTX md5_ctx;

void test_pass (void)
{
	/* Compute md5 */

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, real_key, 9);
	MD5_Final((unsigned char *) md5, &md5_ctx);

	/* Decrypts bytes 32-63 then 0-31 of hash_and_verifier */

	RC4_KEY k;
	RC4_set_key (&k, 16, (unsigned char *) md5); 
	RC4 (&k, 16, data+16, hash_and_verifier+16); 
	RC4 (&k, 16, data, hash_and_verifier); 

	/* Check hash */

	MD5_Init(&md5_ctx);
	MD5_Update(&md5_ctx, hash_and_verifier + 16, 16);
	MD5_Final((unsigned char *)md5, &md5_ctx);

	if (0 == memcmp (md5, hash_and_verifier, 16)) {
		printf("Key found!\n");
		printf("Key is ");
		print_hex ((uint8_t *) real_key, 5);
		if (flag_test_speed) {
			cracking_stats ();
		}
		exit(0);
	}
}

void crack_pass (void)
{
	if (flag_test_speed) {
		start_time = clock();
		real_key_start [0] = real_key [0];
		real_key_start [1] = real_key [1];
	}

	/* Only works on a little endian-machine */
	do {
		do {
			test_pass();

			real_key[0]++;
		if (!(real_key[0] & 0x0000FFFF)) {
			printf("Testing .. .. ");
			print_hex ((uint8_t *) real_key + 2, 3);
		}
		} while ((real_key[0]) != 0);

		real_key[1]++;
	} while (1);
}

void print_hex (uint8_t *array, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		printf ("%02x ", array[i]);
	}
	printf ("\n");
}


void read_hex (uint8_t *target, char *source, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		sscanf (source + 3*i, "%hhx", &target[i]);
	}
}


extern void extract (const char *file_name, unsigned char *FilePass);

void load_data_from_file (const char *file_name)
{
	char FilePass[54];
	extract (file_name, FilePass);
	
	/* print_hex(FilePass, 55); */

	memcpy (data + 16, FilePass + 22, 16); /* EncryptedVerifier */
	// print_hex (data + 16, 16);
	memcpy (data, FilePass + 38, 16); /* EncryptedVerifierHash */
	// print_hex (data, 16);

}

/* Use getopt() to parse command line */
void parse_cmd(int argc, char **argv)
{
	int c;
	while (1) {
		struct option options[] =
		{
		 {"start", required_argument, 0, 's'},
		 {"test-speed", no_argument, 0, 't'},
		 {0, 0, 0, 0}
		};
		int option_idx = 0;

		c = getopt_long (argc, argv, "s:t", options, &option_idx);

		if (c == -1) break; /* End of options */
		
		switch (c) {
		case 's': /* '--start' */
			{
			uint8_t *real_key8  = (uint8_t *) real_key;
			int n;

			n = sscanf (optarg, "%hhx %hhx %hhx %hhx %hhx",
				&real_key8[0], 
				&real_key8[1], 
				&real_key8[2], 
				&real_key8[3], 
				&real_key8[4]);
			if (n != 5) {
				fprintf(stderr,
					"Could not parse start location\n");
				exit(1);
			}
			break;
			}
		case 't':
			printf("Speed testing enabled.\n");
			flag_test_speed = 1;
			break;
		case '?':
			exit (1);
			break;
		}

	}
	if (optind == argc) {
		fprintf(stderr, "No filename provided\n");
		exit (1);
	}
	file_name = argv[optind];
	load_data_from_file (file_name);
	printf("Data successfully loaded from %s\n", file_name);
}

void catch_signal (int sig)
{
	printf("Program interrupted - ending program...\n");
	if (flag_test_speed) cracking_stats();
	exit(0);
}

main (int argc, char **argv)
{
	signal(SIGINT, catch_signal);
	parse_cmd (argc, argv);
	crack_pass ();
}

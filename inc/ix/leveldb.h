#pragma once

#include <stdint.h>
#include <ix/log.h>
#include <leveldb/c.h>
#include <time.h>
#include <assert.h>

#define KEYSIZE 32
#define VALSIZE 32

static unsigned long long *mmap_file;
static unsigned long long *randomized_keys;

typedef char db_key[KEYSIZE];
typedef char db_value[VALSIZE];

// Type of Level-db operations
typedef enum
{
    DB_PUT,
    DB_GET,
    DB_DELETE,
    DB_ITERATOR,
    DB_SEEK,
    DB_CUSTOM, // This type added for tests, see below.
} DB_REQ_TYPE;

typedef struct db_req
{
    DB_REQ_TYPE type;
    db_key key;
    db_value val;
    uint64_t ns; // service time in ns
    uint64_t ts;
} db_req;

typedef struct kv_parameter
{
    db_key key;
    db_value value;

} kv_parameter;

typedef struct custom_payload
{
    int id;
    int ns;
    long timestamp;
} custom_payload;

static void randomized_keys_init(uint64_t num_keys)
{
    randomized_keys = (unsigned long long *)malloc(num_keys * sizeof(unsigned long long));
    if (randomized_keys == 0)
    {
        assert(0 && "malloc failed");
    }
    srand(time(NULL));
    for (int i = 0; i < num_keys; i++)
    {
        randomized_keys[i] = rand() % num_keys;
    }
}

static void check_db_sequential(leveldb_t *db, long num_keys, leveldb_readoptions_t *roptions)
{
    char *db_err = NULL;

    for (size_t i = 0; i < num_keys; i++)
    {
        char keybuf[KEYSIZE], valbuf[VALSIZE];
        int len;
        snprintf(keybuf, KEYSIZE, "key%d", i);
        snprintf(valbuf, VALSIZE, "val%d", i);
        char *r = leveldb_get(db, roptions, keybuf, KEYSIZE, &len, &db_err);

		// printf("%s\n",r);

		if (db_err != NULL)
		{
			fprintf(stderr, "read fail. %s\n", keybuf);
			return (1);
		}
    }
}

static void prepare_simple_db(leveldb_t *db, long num_keys, leveldb_writeoptions_t *woptions)
{
	char *db_err = NULL;

	randomized_keys_init(num_keys);

	for (size_t i = 0; i < num_keys; i++)
	{
		char keybuf[KEYSIZE], valbuf[VALSIZE];
		snprintf(keybuf, KEYSIZE, "key%d", i);
		snprintf(valbuf, VALSIZE, "val%d", i);
		leveldb_put(db, woptions, keybuf, KEYSIZE, valbuf, VALSIZE, &db_err);

		if (db_err != NULL)
		{
			fprintf(stderr, "write fail. %s\n", keybuf);
			return (1);
		}
	}
}


static void prepare_complex_db(leveldb_t *db, long num_keys, leveldb_writeoptions_t *woptions)
{
	char *db_err = NULL;

	randomized_keys_init(num_keys);

	for (size_t i = 0; i < num_keys; i++)
	{
		char keybuf[KEYSIZE], valbuf[VALSIZE];
		snprintf(keybuf, KEYSIZE, "key%d", i);
		snprintf(valbuf, KEYSIZE, "val%d", i);
		leveldb_put(db, woptions, keybuf, KEYSIZE, valbuf, VALSIZE, &db_err);

		if (db_err != NULL)
		{
			fprintf(stderr, "write fail. %s\n", keybuf);
			return (1);
		}
	}

	uint64_t del_counter = 0;

	for (int i = 0; i < num_keys; i = i + num_keys / 100)
	{
		for (int j = 1; j < num_keys / 100; j++)
		{
			char keybuf[20];
			snprintf(keybuf, 20, "key%d", i + j);
			leveldb_delete(db, woptions, keybuf, strlen(keybuf), &db_err);

			if (db_err != NULL)
			{
				fprintf(stderr, "Delete fail. %s\n", keybuf);
				return (1);
			}

			del_counter++;
		}
	}

	printf("delete completed %llu \n", del_counter);
}

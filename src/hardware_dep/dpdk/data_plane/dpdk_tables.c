// Copyright 2016 Eotvos Lorand University, Budapest, Hungary
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "backend.h"
#include "actions.h"
#include "dataplane.h"
#include "dpdk_tables.h"
#include <rte_cycles.h>
// ============================================================================
// LOOKUP TABLE IMPLEMENTATIONS
#include <rte_prefetch.h>
#include <rte_hash.h>       // EXACT
#include <rte_hash_crc.h>
#include <nmmintrin.h> 
#include <rte_lpm.h>        // LPM (32 bit key)
#include <rte_lpm6.h>       // LPM (128 bit key)
#include "ternary_naive.h"  // TERNARY

#include <rte_malloc.h>     // extended tables
#include <rte_version.h>    // for conditional on rte_hash parameters
#include <rte_errno.h>

//#include"key.h"
//Added all these structs so that we can prefetch the bucket in the exact_lookups() function.
#define RTE_HASH_BUCKET_ENTRIES         4
struct rte_hash_signatures {
        union {
                struct {
                        hash_sig_t current;
                        hash_sig_t alt;
                };
                uint64_t sig;
        };
};

enum cmp_jump_table_case {
        KEY_CUSTOM = 0,
        KEY_16_BYTES,
        KEY_32_BYTES,
        KEY_48_BYTES,
        KEY_64_BYTES,
        KEY_80_BYTES,
        KEY_96_BYTES,
        KEY_112_BYTES,
        KEY_128_BYTES,
        KEY_OTHER_BYTES,
        NUM_KEY_CMP_CASES,
};

enum add_key_case {
        ADD_KEY_SINGLEWRITER = 0,
        ADD_KEY_MULTIWRITER,
        ADD_KEY_MULTIWRITER_TM,
};

typedef struct {
        volatile int locked; /**< lock status 0 = unlocked, 1 = locked */
} rte_spinlock_t;

struct rte_hash_bucket {
        struct rte_hash_signatures signatures[RTE_HASH_BUCKET_ENTRIES];
        /* Includes dummy key index that always contains index 0 */
        uint32_t key_idx[RTE_HASH_BUCKET_ENTRIES + 1];
        uint8_t flag[RTE_HASH_BUCKET_ENTRIES];
} __rte_cache_aligned;

struct rte_hash {
        char name[RTE_HASH_NAMESIZE];   /**< Name of the hash. */
        uint32_t entries;               /**< Total table entries. */
        uint32_t num_buckets;           /**< Number of buckets in table. */
        uint32_t key_len;               /**< Length of hash key. */
        rte_hash_function hash_func;    /**< Function used to calculate hash. */
        uint32_t hash_func_init_val;    /**< Init value used by hash_func. */
        rte_hash_cmp_eq_t rte_hash_custom_cmp_eq;
        /**< Custom function used to compare keys. */
        enum cmp_jump_table_case cmp_jump_table_idx;
        /**< Indicates which compare function to use. */
        uint32_t bucket_bitmask;        /**< Bitmask for getting bucket index
                                                from hash signature. */
        uint32_t key_entry_size;         /**< Size of each key entry. */

        struct rte_ring *free_slots;    /**< Ring that stores all indexes
                                                of the free slots in the key table */
        void *key_store;                /**< Table storing all keys and data */
        struct rte_hash_bucket *buckets;        /**< Table with buckets storing all the
                                                        hash values and key indexes
                                                        to the key table*/
        uint8_t hw_trans_mem_support;   /**< Hardware transactional
                                                        memory support */
        struct lcore_cache *local_free_slots;
        /**< Local cache per lcore, storing some indexes of the free slots */
        enum add_key_case add_key; /**< Multi-writer hash add behavior */

        rte_spinlock_t *multiwriter_lock; /**< Multi-writer spinlock for w/o TM */
} __rte_cache_aligned;
//===================================================================================

static uint32_t crc32(const void *data, uint32_t data_len, uint32_t init_val) {
    int32_t *data32 = (void*)data;
    uint32_t result = init_val; 
    result = _mm_crc32_u32 (result, *data32++);
    return result;
}

static uint8_t*
copy_to_socket(uint8_t* src, int length, int socketid) {
    uint8_t* dst = rte_malloc_socket("uint8_t", sizeof(uint8_t)*length, 0, socketid);
    memcpy(dst, src, length);
    return dst;
}

// ============================================================================
// LOWER LEVEL TABLE MANAGEMENT

void create_error_text(int socketid, char* table_type, char* error_text)
{
    rte_exit(EXIT_FAILURE, "DPDK: Unable to create the %s on socket %d: %s\n", table_type, socketid, error_text);
}

void create_error(int socketid, char* table_type)
{
    if (rte_errno == ENOENT) {
        create_error_text(socketid, table_type, "missing entry");
    }
    if (rte_errno == EINVAL) {
        create_error_text(socketid, table_type, "invalid parameter passed to function");
    }
    if (rte_errno == ENOSPC) {
        create_error_text(socketid, table_type, "the maximum number of memzones has already been allocated");
    }
    if (rte_errno == EEXIST) {
        create_error_text(socketid, table_type, "a memzone with the same name already exists");
    }
    if (rte_errno == ENOMEM) {
        create_error_text(socketid, table_type, "no appropriate memory area found in which to create memzone");
    }
}

struct rte_hash *
hash_create(int socketid, const char* name, uint32_t keylen, rte_hash_function hashfunc)
{
    struct rte_hash_parameters hash_params = {
        .name = NULL,
        .entries = HASH_ENTRIES,
#if RTE_VER_MAJOR == 2 && RTE_VER_MINOR == 0
        .bucket_entries = 4,
#endif
        .key_len = keylen,
        .hash_func = hashfunc,
        .hash_func_init_val = 0,
    };
    hash_params.name = name;
    hash_params.socket_id = socketid;
    printf("%s\n", name);
    struct rte_hash *h = rte_hash_create(&hash_params);
    if (h == NULL)
        create_error(socketid, "hash");
    return h;
}

struct rte_lpm *
lpm4_create(int socketid, const char* name, uint8_t max_size)
{
#if RTE_VERSION >= RTE_VERSION_NUM(16,04,0,0)
    struct rte_lpm_config config = {
        .max_rules = 1000000,
        .number_tbl8s = 65000, // TODO refine this
        .flags = 0
    };
    struct rte_lpm *l = rte_lpm_create(name, socketid, &config);
#else
    struct rte_lpm *l = rte_lpm_create(name, socketid, max_size, 0/*flags*/);
#endif
    if (l == NULL)
        create_error(socketid, "LPM");
    return l;
}

struct rte_lpm6 *
lpm6_create(int socketid, const char* name, uint8_t max_size)
{
    struct rte_lpm6_config config = {
        .max_rules = 1000000,
        .number_tbl8s = (1 << 20),
        .flags = 0
    };
    //debug("lpm6_create\n");
    struct rte_lpm6 *l = rte_lpm6_create(name, socketid, &config);
    //debug("lpm6_create\n");
    if (l == NULL)
        create_error(socketid, "LPM6");
    return l;
}

int32_t
hash_add_key(struct rte_hash* h, void *key)
{
    int32_t ret;
    ret = rte_hash_add_key(h,(void *) key);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Unable to add entry to the hash.\n");
    return ret;
}
	
void
lpm4_add(struct rte_lpm* l, uint32_t key, uint8_t depth, uint8_t value)
{
    int ret = rte_lpm_add(l, key, depth, value);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Unable to add entry to the LPM table\n");
    //debug("LPM: Added 0x%08x / %d (%d)\n", (unsigned)key, depth, value);
}

void
lpm6_add(struct rte_lpm6* l, uint8_t key[16], uint8_t depth, uint8_t value)
{
    int ret = rte_lpm6_add(l, key, depth, value);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Unable to add entry to the LPM table\n");
   //debug("LPM: Adding route %s / %d (%d)\n", "IPV6 : ", depth, value);
}

// ============================================================================
// HIGHER LEVEL TABLE MANAGEMENT

// ----------------------------------------------------------------------------
// CREATE

static void
create_ext_table(lookup_table_t* t, void* rte_table, int socketid)
{
    debug("create_ext_table\n");
    extended_table_t* ext = rte_malloc_socket("extended_table_t", sizeof(extended_table_t), 0, socketid);
    ext->rte_table = rte_table;
    ext->size = 0;
    ext->content = rte_malloc_socket("uint8_t*", sizeof(uint8_t*)*TABLE_MAX, 0, socketid);
    t->table = ext;
}

void
exact_create(lookup_table_t* t, int socketid)
{
    char name[64];
    snprintf(name, sizeof(name), "%s_exact_%d_%d", t->name, socketid, t->instance);
    struct rte_hash* h = hash_create(socketid, name, t->key_size, rte_hash_crc);
    create_ext_table(t, h, socketid);
}

void
lpm_create(lookup_table_t* t, int socketid)
{
    char name[64];
    snprintf(name, sizeof(name), "%s_lpm_%d_%d", t->name, socketid, t->instance);
    if(t->key_size <= 4) 
        create_ext_table(t, lpm4_create(socketid, name, t->max_size), socketid); 
    else if(t->key_size <= 16) 
        create_ext_table(t, lpm6_create(socketid, name, t->max_size), socketid);
    else
        rte_exit(EXIT_FAILURE, "LPM: key_size not supported\n");

}

void
ternary_create(lookup_table_t* t, int socketid)
{
    t->table = naive_ternary_create(t->key_size, t->max_size);
}

// ----------------------------------------------------------------------------
// SET DEFAULT VALUE

void
table_setdefault(lookup_table_t* t, uint8_t* value)
{
    debug("Default value set for table %s (on socket %d).\n", t->name, t->socketid);
    t->default_val = copy_to_socket(value, t->val_size, t->socketid);
}

// ----------------------------------------------------------------------------
// ADD

static uint8_t* add_index(uint8_t* value, int val_size, int index)
{
    //realloc doesn't work in this case ("invalid old size")
    uint8_t* value2 = malloc(val_size+sizeof(int));
    memcpy(value2, value, val_size);
    *(value+val_size) = index;
    return value2;
}
void
exact_add(lookup_table_t* t, uint8_t* key, uint8_t* value)
{
    if(t->key_size == 0) return; // don't add lines to keyless tables
    extended_table_t* ext = (extended_table_t*)t->table;
    uint32_t index = rte_hash_add_key(ext->rte_table, (void*) key);
    if(index < 0)
        rte_exit(EXIT_FAILURE, "HASH: add failed\n");
    value = add_index(value, t->val_size, t->counter++);
    ext->content[index%256] = copy_to_socket(value, t->val_size, t->socketid);
}

void
exact_add_universal(lookup_table_t* t, struct universal_key key, uint8_t* value)
{
    if(t->key_size == 0) return; // don't add lines to keyless tables
    extended_table_t* ext = (extended_table_t*)t->table;
    uint32_t index = rte_hash_add_key(ext->rte_table, (void*) &key);
    //debug("Exact_add_universal::Hashed value: %lu\n", index);
    if(index < 0)
        debug("EXIT_FAILURE, HASH: add failed\n");
    value = add_index(value, t->val_size, t->counter++);
    ext->content[index%256] = copy_to_socket(value, t->val_size, t->socketid);
}

void
lpm_add(lookup_table_t* t, uint8_t* key, uint8_t depth, uint8_t* value)
{
    if(t->key_size == 0) return; // don't add lines to keyless tables
    extended_table_t* ext = (extended_table_t*)t->table;
    if(t->key_size <= 4)
    {
	value = add_index(value, t->val_size, t->counter++);
        ext->content[ext->size] = copy_to_socket(value, t->val_size, t->socketid);
        // the rest is zeroed in case of keys smaller then 4 bytes
        uint32_t key32 = 0;
        memcpy(&key32, key, t->key_size);
        lpm4_add(ext->rte_table, key32, depth, ext->size++);
    }
    else if(t->key_size <= 16)
    {
	struct ipv6_lpm_action *val = (struct ipv6_lpm_action *) value;
        uint8_t port = 1;//val->fib_hit_nexthop_params.port[0];
        ext->content[port % 256] = copy_to_socket(value, t->val_size, t->socketid);
        static uint8_t key128[16];
        memset(key128, 0, 16);
        memcpy(key128, key, t->key_size);
	lpm6_add(ext->rte_table, key128, depth, port);
    }
}

void
ternary_add(lookup_table_t* t, uint8_t* key, uint8_t* mask, uint8_t* value)
{
    if(t->key_size == 0) return; // don't add lines to keyless tables
    value = add_index(value, t->val_size, t->counter++);
    naive_ternary_add(t->table, key, mask, copy_to_socket(value, t->val_size, t->socketid));
}

// ----------------------------------------------------------------------------
// LOOKUP
uint8_t*        
one_exact_lookup(lookup_table_t* t, uint8_t* key)
{               
    if(t->key_size == 0) return t->default_val;
    extended_table_t* ext = (extended_table_t*)t->table;
    int ret = rte_hash_lookup(ext->rte_table, key);
    if(ret < 0) {
        debug("%s\n", key);
        debug("NDN entry not present\n");
    }
    return (ret < 0)? t->default_val : ext->content[ret % 256];
}

void
bulk_exact_lookup(lookup_table_t* t, int batch_size, int keysize, uint8_t key[][keysize], uint8_t **values)
{
    if(t->key_size == 0) {
        for(int i = 0; i<batch_size;i++) {
                values[i] = t->default_val;
        }    
        return;
    }
    extended_table_t* ext = (extended_table_t*)t->table;
    int32_t positions[batch_size];
    const void *data[batch_size];
    for(int i = 0; i < batch_size; i++) {
	data[i] = (void *)key[i];
    }

    int ret = rte_hash_lookup_bulk(ext->rte_table, data, batch_size, positions);
    if(ret == EINVAL) {
        for(int i = 0; i<batch_size;i++)
                values[i] = t->default_val;
    } else {
	for(int i = 0; i < batch_size; i++)
	    values[i] = (positions[i] < 0) ? t->default_val : ext->content[positions[i] %256];
    }
}

void
exact_lookup(lookup_table_t* t, int batch_size, int keysize, struct universal_key key[], uint8_t **values)
{
    extended_table_t* ext = (extended_table_t*) t->table;
    if(t->key_size == 0) {
        for(int i = 0; i < batch_size; i++) {
                values[i] = t->default_val;
        }
        return;
    }
	/*uint8_t eth_src[6] = {160,54,159,62,235,164};
	struct universal_key new_key= universal_key_default();
	new_key.eth.src = eth_src;
	new_key.vlan.ingress_port = 7;
*/

    for(int i = 0; i < batch_size; i++) {
    	//debug("IP protocol: %d\n", key[i].ip.proto);//, key[i].ip.src[2], key[i].ip.src[1], key[i].ip.src[0]);
	//debug("IPsrc: %d:%d:%d:%d, IPdst: %d:%d:%d:%d\n", key[i].ip.src[3], key[i].ip.src[2], key[i].ip.src[1], key[i].ip.src[0], key[i].ip.dst[3], key[i].ip.dst[2], key[i].ip.dst[1], key[i].ip.dst[0]);
	
	//debug("VLAN ingress: %d vid %d VLAN egress %d\n", key[i].vlan.ingress_port, key[i].vlan.vid, key[i].vlan.egress_port);
	//print_mac(key[i].eth.src);
	//print_mac(key[i].eth.dst);	
        int ret = rte_hash_lookup(ext->rte_table, (void*)&key[i]);
	if(ret == -EINVAL || ret == -ENOENT) {
		//debug("HASH: lookup failed\n");
		debug("ret:%d\n", ret);
		debug("Mac src: %d.%d.%d.%d.%d.%d, Mac dst: %d.%d.%d.%d.%d.%d, vid: %d\n", key[i].eth.src[0],key[i].eth.src[1],key[i].eth.src[2], key[i].eth.dst[3], key[i].eth.dst[4] ,key[i].eth.src[5],key[i].eth.dst[0],key[i].eth.dst[1],key[i].eth.dst[2],key[i].eth.dst[3], key[i].eth.dst[4], key[i].eth.dst[5], key[i].vlan.vid);
	

		//debug("IP protocol: %d\n", key[i].ip.proto);//, key[i].ip.src[2], key[i].ip.src[1], key[i].ip.src[0]);
	}
       	values[i] = (ret < 0)? t->default_val : ext->content[ret % 256];
    }
}

void
lpm_lookup(lookup_table_t* t, int batch_size, int keysize, uint8_t key[][keysize], uint8_t* value[]) {
    if (t->key_size == 0) {
        for (int i = 0; i < batch_size; i++) {
            value[i] = t->default_val;
        }
    }
    extended_table_t* ext = (extended_table_t*) t->table;
    if (t->key_size <= 4) {
	uint32_t result[batch_size];
        rte_lpm_lookup_bulk(ext->rte_table, (uint32_t *)key, result, batch_size);
        for(int i = 0; i < batch_size; i++) {
            value[i] = (result[i] && RTE_LPM_LOOKUP_SUCCESS) ? ext->content[result[i] % 256] : t->default_val;
        }
    }
    else if(t->key_size <= 16) {
        int16_t result[batch_size];
        int ret = rte_lpm6_lookup_bulk_func(ext->rte_table, key, result, batch_size);
        for(int i = 0; i < batch_size; i++) {
		value[i] = (ret ==0 ? ext->content[result[i]] : t->default_val);
        }
    }
}
/*
uint8_t*
ternary_lookup(lookup_table_t* t, uint8_t* key)
{
    if(t->key_size == 0) return t->default_val;
    uint8_t* ret = naive_ternary_lookup(t->table, key);
    return ret == NULL ? t->default_val : ret;
}
*/
uint8_t*
ternary_lookup(lookup_table_t* t, uint8_t* key)
{
    if(t->key_size == 0) return t->default_val;
    uint8_t* ret = naive_ternary_lookup(t->table, key);
    return ret == NULL ? t->default_val : ret;
}


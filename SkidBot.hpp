#ifndef	_SKID_BOT_H
#define _SKID_BOT_H

#include <pthread.h>
#include <chrono>

#define lock(x) (pthread_mutex_lock(&x))
#define trylock(x) (pthread_mutex_trylock(&x))
#define release(x) (pthread_mutex_unlock(&x))

// Time duration defines
#define hrc_now std::chrono::high_resolution_clock::now()
#define hrc_get_seconds(x) (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(x.time_since_epoch()).count()
#define hrc_get_milli(x) (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(x.time_since_epoch()).count()
#define hrc_get_micro(x) (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(x.time_since_epoch()).count()

// Defines some standard types
typedef union uint8_bits
{
    struct
    {
        uint8_t BIT0:1;
        uint8_t BIT1:1;
        uint8_t BIT2:1;
        uint8_t BIT3:1;
        uint8_t BIT4:1;
        uint8_t BIT5:1;
        uint8_t BIT6:1;
        uint8_t BIT7:1;
    };
    uint8_t value;
} uint8_bits;

typedef union uint8_hexs
{
    struct
    {
        uint8_t HEX0:4;
        uint8_t HEX1:4;
    };
    uint8_t value;
} uint8_hexs;

typedef union uint16_bits
{
    struct
    {
        uint8_t BIT0:1;
        uint8_t BIT1:1;
        uint8_t BIT2:1;
        uint8_t BIT3:1;
        uint8_t BIT4:1;
        uint8_t BIT5:1;
        uint8_t BIT6:1;
        uint8_t BIT7:1;
        uint8_t BIT8:1;
        uint8_t BIT9:1;
        uint8_t BIT10:1;
        uint8_t BIT11:1;
        uint8_t BIT12:1;
        uint8_t BIT13:1;
        uint8_t BIT14:1;
        uint8_t BIT15:1;
    };
    uint16_t value;
} uint16_bits;

typedef union uint32_bits
{
    struct
    {
        uint8_t BIT0:1;
        uint8_t BIT1:1;
        uint8_t BIT2:1;
        uint8_t BIT3:1;
        uint8_t BIT4:1;
        uint8_t BIT5:1;
        uint8_t BIT6:1;
        uint8_t BIT7:1;
        uint8_t BIT8:1;
        uint8_t BIT9:1;
        uint8_t BIT10:1;
        uint8_t BIT11:1;
        uint8_t BIT12:1;
        uint8_t BIT13:1;
        uint8_t BIT14:1;
        uint8_t BIT15:1;
        uint8_t BIT16:1;
        uint8_t BIT17:1;
        uint8_t BIT18:1;
        uint8_t BIT19:1;
        uint8_t BIT20:1;
        uint8_t BIT21:1;
        uint8_t BIT22:1;
        uint8_t BIT23:1;
        uint8_t BIT24:1;
        uint8_t BIT25:1;
        uint8_t BIT26:1;
        uint8_t BIT27:1;
        uint8_t BIT28:1;
        uint8_t BIT29:1;
        uint8_t BIT30:1;
        uint8_t BIT31:1;
    };
    uint32_t value;
} uint32_bits;

typedef union float_bytes
{
	float value;
	unsigned char bytes[4];
} float_bytes;

#endif

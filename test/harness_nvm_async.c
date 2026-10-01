/* Host harness for VLCBlib b47 LCR-004 (nvm.c async EEPROM writer).
 * The writer code is the literal slice from nvm.c; only NVMCON0bits.GO is
 * mapped onto a model (reads -> go_read(), "= 1" -> start_write()).
 * Model: a byte write keeps GO set for 3 reads of GO, then lands; a chosen
 * cell can be made to land wrong N times; the HLVD can report a low rail. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define _18FXXQ83_FAMILY_
#define VLCB_EEPROM_ASYNC 4
#define VLCB_VDD_GUARD 1
#define GRSP_OK 0
#define GRSP_INVALID_COMMAND_PARAMETER 11
#define NVMCMD_WRITE 3
#define NVMCMD_NOP 0
typedef uint16_t eeprom_address_t;
typedef uint8_t eeprom_data_t;
uint32_t fake_now = 0;
static uint8_t ee[1024];
static int go_polls = 0, bad_cell = -1, bad_times = 0, ints = 1;
static int writes_started = 0, sync_writes = 0, failures = 0;
struct { unsigned WRERR:1; unsigned NVMCMD:3; } NVMCON1bits;
struct { unsigned EN:1, RDY:1, OUT:1; } HLVDCON0bits = {1,1,0};
uint8_t NVMADRU, NVMADRH, NVMADRL, NVMDATL, NVMLOCK; uint32_t NVMADR;
static int go_read(void) {
    if (go_polls > 0) { go_polls--; if (go_polls == 0) {
        uint16_t a = ((uint16_t)NVMADRH << 8) | NVMADRL;
        if ((int)a == bad_cell && bad_times > 0) { bad_times--; ee[a] = (uint8_t)~NVMDATL; }
        else ee[a] = NVMDATL; } }
    return go_polls > 0;
}
static void start_write(void){ go_polls = 3; writes_started++; }
static uint8_t geti(void){ return (uint8_t)ints; }
static void bothDi(void){ ints = 0; }
static void bothEi(void){ ints = 1; }
eeprom_data_t EEPROM_Read(eeprom_address_t i){ while (go_read()) ; return ee[i]; }
uint8_t EEPROM_WriteNoVerify(eeprom_address_t i, eeprom_data_t v){
    if (HLVDCON0bits.EN && HLVDCON0bits.RDY && HLVDCON0bits.OUT) return GRSP_INVALID_COMMAND_PARAMETER;
    while (go_read()) ;
    NVMADRH = (uint8_t)(i>>8); NVMADRL = (uint8_t)i; NVMDATL = v; start_write(); return GRSP_OK; }
uint8_t EEPROM_Write(eeprom_address_t i, eeprom_data_t v){ sync_writes++;
    if (EEPROM_WriteNoVerify(i,v)!=GRSP_OK) return GRSP_INVALID_COMMAND_PARAMETER;
    while(go_read()); return ee[i]==v?GRSP_OK:GRSP_INVALID_COMMAND_PARAMETER; }
#include "async_slice_host.inc"
/* the public paths as nvm.c routes them */
static uint8_t writeEE(uint16_t a, uint8_t v){ return nvmAsyncQueue(a, v); }
static int16_t readEE(uint16_t a){ int16_t q = nvmAsyncLookup(a); return q >= 0 ? q : EEPROM_Read(a); }
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)
static void polls(int n){ while (n--) { nvmPoll(); fake_now += 16; } }
int main(void){
    memset(ee, 0xFF, sizeof ee);
    /* 1 queued write lands in the background, not on the call */
    writeEE(0x200, 7);
    CHECK(ee[0x200] == 0xFF, "write must not land synchronously");
    CHECK(readEE(0x200) == 7, "read-your-write before it lands");
    polls(10);
    CHECK(ee[0x200] == 7 && nvmAsyncPending() == 0 && nvmAsyncWrites == 1, "background write lands and verifies");
    /* 2 coalescing: three writes to one waiting cell = one write */
    int before = writes_started;
    writeEE(0x201, 1); writeEE(0x202, 9); writeEE(0x201, 2); writeEE(0x201, 3);
    CHECK(nvmAsyncPending() == 2, "coalesced onto the waiting entry");
    CHECK(readEE(0x201) == 3, "newest queued value is read");
    polls(20);
    CHECK(ee[0x201] == 3 && ee[0x202] == 9 && writes_started - before == 2, "two cells, two writes");
    /* 3 a write to the in-flight cell follows it, it does not replace it */
    writeEE(0x203, 4); nvmPoll();              /* head now in flight */
    writeEE(0x203, 5);
    CHECK(nvmAsyncPending() == 2 && readEE(0x203) == 5, "in-flight head not overwritten; newest wins on read");
    polls(20);
    CHECK(ee[0x203] == 5, "final value is the newest");
    /* 4 unchanged value costs no write */
    before = writes_started; writeEE(0x203, 5); polls(5);
    CHECK(writes_started == before && nvmAsyncPending() == 0, "no wear for an unchanged value");
    /* 5 verify failure retried, then success */
    bad_cell = 0x204; bad_times = 2; writeEE(0x204, 0x55); polls(40);
    CHECK(ee[0x204] == 0x55 && nvmAsyncFailures == 0, "two bad landings then good");
    /* 6 three failures: abandoned and counted */
    bad_times = 3; writeEE(0x204, 0x66); polls(60);
    CHECK(nvmAsyncFailures == 1 && nvmAsyncPending() == 0, "abandoned after 3 attempts");
    bad_cell = -1;
    /* 7 full queue: drained, then this write in line; nothing lost */
    for (int i = 0; i < 4; i++) writeEE(0x300 + i, (uint8_t)i);
    CHECK(nvmAsyncPending() == 4 && nvmAsyncHighWater == 4, "queue full");
    writeEE(0x310, 0xAA);
    CHECK(nvmAsyncFallbacks == 1 && nvmAsyncPending() == 0, "fallback drained the queue");
    for (int i = 0; i < 4; i++) CHECK(ee[0x300 + i] == i, "queued writes kept by fallback");
    CHECK(ee[0x310] == 0xAA, "fallback write made in line");
    /* 8 low rail holds the head, then drops it as refused after 1 s */
    HLVDCON0bits.OUT = 1; writeEE(0x320, 1); polls(100);
    CHECK(ee[0x320] == 0xFF && nvmAsyncPending() == 1, "low rail: held, not written");
    fake_now += 62500 + 16; nvmPoll();
    CHECK(nvmAsyncRefused == 1 && nvmAsyncPending() == 0, "dropped as refused after 1 s");
    /* 9 rail recovers inside the second: written */
    writeEE(0x321, 2); polls(10); HLVDCON0bits.OUT = 0; polls(10);
    CHECK(ee[0x321] == 2 && nvmAsyncRefused == 1, "recovered rail: written, not refused");
    /* 10 flushNVM drains everything, including an in-flight head */
    writeEE(0x330, 1); writeEE(0x331, 2); nvmPoll(); writeEE(0x332, 3);
    flushNVM();
    CHECK(nvmAsyncPending() == 0 && ee[0x330] == 1 && ee[0x331] == 2 && ee[0x332] == 3, "flush drains all");
    /* 11 flush on a low rail does not hang: refused entries dropped */
    HLVDCON0bits.OUT = 1; writeEE(0x340, 1); writeEE(0x341, 2); flushNVM();
    CHECK(nvmAsyncPending() == 0 && nvmAsyncRefused == 3, "flush never loops on a low rail");
    HLVDCON0bits.OUT = 0;
    /* 12 write timeout: GO stuck > 100 ms counts as a failed attempt, then retried */
    writeEE(0x350, 9); nvmPoll();                /* started, GO has 3 reads to go */
    go_polls = 1000000;                          /* stuck */
    fake_now += HUNDRED_MILI_SECOND + 1; nvmPoll();
    go_polls = 0; polls(20);
    CHECK(ee[0x350] == 9, "timed-out attempt retried and completed");
    /* 13 ring wraps correctly over many writes */
    for (int i = 0; i < 50; i++) { writeEE(0x360 + (i % 7), (uint8_t)i); polls(3); }
    flushNVM();
    for (int c = 0; c < 7; c++) { int last = -1; for (int i = 0; i < 50; i++) if (i % 7 == c) last = i; CHECK(ee[0x360 + c] == last, "ring wrap: last value per cell"); }
    printf("%s: %d failure(s); writes %u, failures %u, refused %u, fallbacks %u, high water %u\n",
        fails ? "FAILED" : "PASSED", fails, nvmAsyncWrites, nvmAsyncFailures, nvmAsyncRefused, nvmAsyncFallbacks, nvmAsyncHighWater);
    return fails ? 1 : 0;
}

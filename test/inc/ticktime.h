#include <stdint.h>
typedef union { uint32_t val; } TickValue;
extern uint32_t fake_now;
#define ONE_SECOND 62500u
#define HUNDRED_MILI_SECOND (ONE_SECOND/10)
#define tickNowGet() (fake_now)
#define tickTimeSinceNow(t) (tickNowGet() - (t).val)

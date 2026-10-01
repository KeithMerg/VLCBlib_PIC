#!/bin/sh
# Re-slice the writer from ../nvm.c, map NVMCON0bits.GO onto the model, build, run.
cd "$(dirname "$0")"
python3 - <<'PY'
s=open('../nvm.c','rb').read().decode('latin-1').replace('\r\n','\n')
a=s.index('#ifdef VLCB_EEPROM_ASYNC\n/*\n * KeithB b47, LCR-004: background')
b=s.index('#endif  /* VLCB_EEPROM_ASYNC */')+len('#endif  /* VLCB_EEPROM_ASYNC */')
t=s[a:b].replace('NVMCON0bits.GO = 1;','start_write();').replace('NVMCON0bits.GO','go_read()').replace('#include "ticktime.h"','#include "inc/ticktime.h"')
open('async_slice_host.inc','w').write(t+'\n')
PY
gcc -std=c99 -Wall -o harness_nvm_async harness_nvm_async.c && ./harness_nvm_async

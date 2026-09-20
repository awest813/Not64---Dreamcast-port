#ifndef DC_NATIVESAVES_H
#define DC_NATIVESAVES_H

void dc_nativesave_load(void);
void dc_nativesave_save(void);

#ifdef DC_HOST_STUB
int dc_nativesave_selftest(void);
#endif

#endif

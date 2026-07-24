/* sdkfake linenoise/linenoise.h */
#ifndef SDKFAKE_LINENOISE_H
#define SDKFAKE_LINENOISE_H

typedef void(linenoiseHintsCallback)(void);

int linenoiseIsDumbMode(void);
void linenoiseClearScreen(void);
void linenoiseSetMultiLine(int ml);
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn);

extern int fake_linenoise_dumb;
extern int fake_linenoise_clears;
extern int fake_linenoise_multiline; /* -1 until set */

#endif

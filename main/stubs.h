/*
 * stubs.h
 *
 * Switches for the two stubbed engine dependencies, set by the cases that exercise the engine's other branches.
 */

#ifndef STUBS_H_
#define STUBS_H_

#include <stdbool.h>

extern volatile bool stub_no_entropy; // entropy_source() reports ENTROPY_NONE
extern volatile bool stub_sntp_time;  // sntp_has_time() reports an SNTP clock

#endif /* STUBS_H_ */

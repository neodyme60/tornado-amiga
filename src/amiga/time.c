/*
Copyright (c) 2019, Miguel Mendez. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <clib/exec_protos.h>
#include <clib/timer_protos.h>
#include <devices/timer.h>

struct Device *TimerBase;
static struct IORequest timereq;
static uint32_t time_available = 0;

void timeInit(void) {
  LONG error;
  error = OpenDevice(TIMERNAME, 0, &timereq, 0);
  if (error) {
    fprintf(stderr, "FATAL - Could not open timer.device! Time services will "
                    "not be available.\n");
  } else {
    time_available = 1;
    TimerBase = timereq.io_Device;
  }
}

void timeEnd(void) {
  if (time_available) {
    CloseDevice(&timereq);
  }
}

void timeGet(struct timeval *t) {
  if (time_available) {
    GetSysTime(t);
  }
}

uint32_t timeDiffSec(struct timeval *t1, struct timeval *t2) {
  if (time_available) {
    SubTime(t2, t1);
    return (uint32_t)t2->tv_secs;
  } else {
    return 0;
  }
}

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

#include <devices/ahi.h>
#include <exec/exec.h>
#include <proto/ahi.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "asmparm.h"
#include "audio_ahi.h"
#include "ddpcm_lowlevel.h"
#include "memory.h"
#include "paula_output.h"
#include "tndo_assert.h"
#include "tornado_settings.h"

// Call the player function 5 times per second.
#define PLAYER_FREQ 5

#define DEFAULT_SAMPLE_FREQ 22050

struct Library *AHIBase;
static struct MsgPort *AHI_mp = 0;
static struct AHIRequest *AHI_io = 0;
static LONG AHI_device = -1;
static struct AHIAudioCtrl *actrl = 0;
static struct IORequest timereq;
static LONG mixfreq = 0;

static char drivername[256];
static int frameSize;
static int numSamples;
static int callao = 0;

struct AHISampleInfo lBuffer = {
    AHIST_M16S,
    0,
    0,
};

struct AHISampleInfo rBuffer = {
    AHIST_M16S,
    0,
    0,
};

static void PlayerFunc(__ASMPARM("a0", struct Hook *hook),
                       __ASMPARM("a1", APTR ignored),
                       __ASMPARM("a2", struct AHIAudioCtrl *h_actrl)) {
  ddpcmAHIPlayerFunc(numSamples, lBuffer.ahisi_Address, rBuffer.ahisi_Address);
}

static struct Hook PlayerHook = {
    0, 0, (ULONG(*)())PlayerFunc, NULL, NULL,
};

// Returns 0 if ok, 1 if an error occurred.
uint32_t audioAhiInit(demoParams *dp) {

  int sampleFreq = DEFAULT_SAMPLE_FREQ;

  switch (dp->audioPeriod) {
  case REPLAY_PERIOD_11025:
    sampleFreq = 11025;
    break;
  case REPLAY_PERIOD_22050:
    sampleFreq = 22050;
    break;
  }

  numSamples = sampleFreq / PLAYER_FREQ;

  AHI_mp = CreateMsgPort();
  if (!AHI_mp) {
    fprintf(stderr, "FATAL - Failed to create message port\n");
    return 1;
  }

  AHI_io =
      (struct AHIRequest *)CreateIORequest(AHI_mp, sizeof(struct AHIRequest));
  if (!AHI_io) {
    fprintf(stderr, "FATAL - Failed to create i/o request\n");
    return 1;
  }

  AHI_io->ahir_Version = 4;

  AHI_device = OpenDevice(AHINAME, AHI_NO_UNIT, (struct IORequest *)AHI_io, 0);
  if (AHI_device) {
    fprintf(stderr, "FATAL - Failed to open device %s\n", AHINAME);
    return 1;
  }

  AHIBase = (struct Library *)AHI_io->ahir_Std.io_Device;

  struct AHIAudioModeRequester *req = AHI_AllocAudioRequest(
      AHIR_PubScreenName, NULL, AHIR_TitleText, "Select a mode and rate",
      AHIR_InitialMixFreq, sampleFreq, AHIR_DoMixFreq, TRUE, TAG_DONE);

  if (!req) {
    fprintf(stderr, "FATAL - AHI_AllocAudioRequest() failed\n");
    return 1;
  }

  if (!(AHI_AudioRequest(req, TAG_DONE))) {
    fprintf(stderr, "FATAL - AHI_AudioRequest() failed\n");
    return 1;
  }

  actrl = AHI_AllocAudio(
      AHIA_AudioID, req->ahiam_AudioID, AHIA_MixFreq, req->ahiam_MixFreq,
      AHIA_Channels, 2, AHIA_Sounds, 2, AHIA_PlayerFunc, &PlayerHook,
      AHIA_PlayerFreq, PLAYER_FREQ << 16, AHIA_MinPlayerFreq, PLAYER_FREQ << 16,
      AHIA_MaxPlayerFreq, PLAYER_FREQ << 16, AHIA_UserData, FindTask(NULL),
      TAG_DONE);

  if (!actrl) {
    fprintf(stderr, "FATAL - AHI_AllocAudio() failed\n");
    return 1;
  }

  AHI_ControlAudio(actrl, AHIC_MixFreq_Query, &mixfreq, TAG_DONE);
  AHI_FreeAudioRequest(req);

  int maxSamples = 0;
  if (!AHI_GetAudioAttrs(AHI_INVALID_ID, actrl, AHIDB_Driver, drivername,
                         AHIDB_BufferLen, sizeof(drivername),
                         AHIDB_MaxPlaySamples, &maxSamples, TAG_DONE)) {
    fprintf(stderr, "FATAL - AHI_GetAudioAttrs() failed\n");
    return 1;
  }

  frameSize = AHI_SampleFrameSize(AHIST_M16S);

  if (dp->tornadoOptions & VERBOSE_DEBUGGING) {
    fprintf(stderr,
            "DEBUG - AHI driver: %s, buffer: %i samples, %i bytes per sample "
            "frame.\n",
            drivername, maxSamples, frameSize);
  }

  if (maxSamples < numSamples) {
    numSamples = maxSamples;
  }

  int bufferSize = numSamples;

  lBuffer.ahisi_Length = bufferSize;
  rBuffer.ahisi_Length = bufferSize;

  lBuffer.ahisi_Address = tndo_malloc(numSamples * frameSize, 0);
  rBuffer.ahisi_Address = tndo_malloc(numSamples * frameSize, 0);

  tndo_assert(lBuffer.ahisi_Address);
  tndo_assert(rBuffer.ahisi_Address);

  if (AHI_LoadSound(0, AHIST_DYNAMICSAMPLE, &lBuffer, actrl)) {
    fprintf(stderr, "FATAL - AHI_LoadSound() failed\n");
    return 1;
  }

  if (AHI_LoadSound(1, AHIST_DYNAMICSAMPLE, &rBuffer, actrl)) {
    fprintf(stderr, "FATAL - AHI_LoadSound() failed\n");
    return 1;
  }

  return 0;
}

void audioAHIEnd() {
  AHI_ControlAudio(actrl, AHIC_Play, FALSE, TAG_DONE);
  AHI_FreeAudio(actrl);
  CloseDevice((struct IORequest *)AHI_io);
  DeleteIORequest((struct IORequest *)AHI_io);
  DeleteMsgPort(AHI_mp);
}

void audioAHIStart() {
  AHI_Play(actrl, AHIP_BeginChannel, 0, AHIP_Freq, mixfreq, AHIP_Vol, 0x10000,
           AHIP_Pan, 0x00000, AHIP_Sound, 0, AHIP_EndChannel, 0,
           AHIP_BeginChannel, 1, AHIP_Freq, mixfreq, AHIP_Vol, 0x10000,
           AHIP_Pan, 0x10000, AHIP_Sound, 1, AHIP_EndChannel, 0, TAG_DONE);

  AHI_ControlAudio(actrl, AHIC_Play, TRUE, TAG_DONE);
}

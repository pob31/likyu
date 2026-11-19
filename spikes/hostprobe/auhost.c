/*
    This file is part of Likyu — https://github.com/pob31/likyu

    Copyright (C) 2026 Pierre-Olivier Boulant

    Likyu is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Likyu is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/*
    A minimal AU host, macOS only, built by hand (not by CMake):

        clang -O1 -o auhost auhost.c -framework AudioToolbox -framework CoreFoundation

    It instantiates aufx <subtype> PxBl, gives the main input and output <main> channels and
    the sidechain <sidechain> (0 leaves that element alone), initialises, renders ten blocks
    of 512 and prints every status. With a fourth argument it then uninitialises, sets the main
    input or output (or both, in the order the fifth argument gives: "i", "o", "io", "oi") to
    that width and initialises again: the sequence that reproduced REAPER's silent AU
    (docs/spikes/phase1-hosts.md, finding 9).

        auhost Lkyp 2 2          the probe, stereo main, stereo sidechain
        auhost Lkyu 2 2 4 i      the product, then input alone to four channels

    Run it in the logged-in session (a .command file opened with `open`): over SSH the AU
    registrar does not see a user-installed component. With LIKYU_PROBE_DUMP naming a folder,
    the probe leaves its report there when the host disposes of it.
*/

#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pulls[2];

static OSStatus feed (void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts,
                      UInt32 bus, UInt32 n, AudioBufferList* io)
{
    int which = (int) (intptr_t) ref;
    pulls[which]++;
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    {
        float* d = (float*) io->mBuffers[b].mData;
        for (UInt32 i = 0; i < n; ++i)
            d[i] = which == 0 ? 0.5f * sinf ((float) i * 0.05f) : 0.25f;
    }
    return noErr;
}

static OSType code (const char* s) { return ((OSType) s[0] << 24) | ((OSType) s[1] << 16) | ((OSType) s[2] << 8) | (OSType) s[3]; }

static AudioStreamBasicDescription fmt (UInt32 ch)
{
    AudioStreamBasicDescription f;
    memset (&f, 0, sizeof f);
    f.mSampleRate = 48000;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
    f.mChannelsPerFrame = ch; f.mBitsPerChannel = 32;
    return f;
}

int main (int argc, char** argv)
{
    const char* sub = argc > 1 ? argv[1] : "Lkyp";
    UInt32 ch = argc > 2 ? (UInt32) atoi (argv[2]) : 2;
    UInt32 sc = argc > 3 ? (UInt32) atoi (argv[3]) : 2;
    AudioComponentDescription d = { kAudioUnitType_Effect, code (sub), code ("PxBl"), 0, 0 };
    AudioComponent c = AudioComponentFindNext (NULL, &d);
    if (! c) { printf ("component %s not found\n", sub); return 1; }
    AudioUnit u;
    OSStatus e = AudioComponentInstanceNew (c, &u);
    printf ("instance: %d\n", (int) e);
    if (e) return 1;

    UInt32 nin = 0, nout = 0, sz = sizeof (UInt32);
    AudioUnitGetProperty (u, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &nin, &sz);
    sz = sizeof (UInt32);
    AudioUnitGetProperty (u, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &nout, &sz);
    printf ("elements: in %u, out %u\n", nin, nout);

    UInt32 isz = 0; Boolean w;
    if (AudioUnitGetPropertyInfo (u, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, &isz, &w) == noErr)
    {
        AUChannelInfo* info = malloc (isz);
        AudioUnitGetProperty (u, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, info, &isz);
        int n = (int) (isz / sizeof (AUChannelInfo));
        printf ("SupportedNumChannels: %d entries:", n);
        for (int i = 0; i < n && i < 12; ++i) printf (" {%d,%d}", info[i].inChannels, info[i].outChannels);
        printf ("%s\n", n > 12 ? " ..." : "");
        free (info);
    }
    else printf ("SupportedNumChannels: none\n");

    AudioStreamBasicDescription f = fmt (ch);
    printf ("set input 0 to %u ch: %d\n", ch, (int) AudioUnitSetProperty (u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f));
    printf ("set output 0 to %u ch: %d\n", ch, (int) AudioUnitSetProperty (u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f));
    if (nin > 1 && sc > 0)
    {
        AudioStreamBasicDescription s = fmt (sc);
        printf ("set input 1 to %u ch: %d\n", sc, (int) AudioUnitSetProperty (u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &s, sizeof s));
    }
    for (UInt32 i = 0; i < nin && i < 2; ++i)
    {
        if (i == 1 && sc == 0) break;
        AURenderCallbackStruct cb = { feed, (void*) (intptr_t) i };
        printf ("callback on input %u: %d\n", i, (int) AudioUnitSetProperty (u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, i, &cb, sizeof cb));
    }
    UInt32 maxf = 512;
    AudioUnitSetProperty (u, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof maxf);
    for (UInt32 i = 0; i < nin; ++i)
    {
        AudioStreamBasicDescription g; UInt32 gs = sizeof g;
        AudioUnitGetProperty (u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, i, &g, &gs);
        printf ("input %u now %u ch\n", i, (unsigned) g.mChannelsPerFrame);
    }
    printf ("initialize: %d\n", (int) AudioUnitInitialize (u));

    if (argc > 4)
    {
        /* argv[4]: a second width, set after an uninitialise, in the order argv[5] gives
           ("io": input then output, "oi": output then input), then initialise again. */
        UInt32 ch2 = (UInt32) atoi (argv[4]);
        const char* order = argc > 5 ? argv[5] : "io";
        printf ("uninitialize: %d\n", (int) AudioUnitUninitialize (u));
        AudioStreamBasicDescription f2 = fmt (ch2);
        for (const char* o = order; *o; ++o)
        {
            AudioUnitScope sc2 = *o == 'i' ? kAudioUnitScope_Input : kAudioUnitScope_Output;
            printf ("set %s 0 to %u ch: %d\n", *o == 'i' ? "input" : "output", ch2,
                    (int) AudioUnitSetProperty (u, kAudioUnitProperty_StreamFormat, sc2, 0, &f2, sizeof f2));
        }
        for (UInt32 i = 0; i < nin; ++i)
        {
            AudioStreamBasicDescription g; UInt32 gs = sizeof g;
            AudioUnitGetProperty (u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, i, &g, &gs);
            printf ("input %u now %u ch\n", i, (unsigned) g.mChannelsPerFrame);
        }
        {
            AudioStreamBasicDescription g; UInt32 gs = sizeof g;
            AudioUnitGetProperty (u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &g, &gs);
            printf ("output 0 now %u ch\n", (unsigned) g.mChannelsPerFrame);
            ch = g.mChannelsPerFrame;
        }
        printf ("initialize again: %d\n", (int) AudioUnitInitialize (u));
    }

    AudioBufferList* abl = malloc (sizeof (AudioBufferList) + ch * sizeof (AudioBuffer));
    abl->mNumberBuffers = ch;
    float* mem = calloc (ch * 512, sizeof (float));
    AudioTimeStamp ts; memset (&ts, 0, sizeof ts); ts.mFlags = kAudioTimeStampSampleTimeValid;
    for (int k = 0; k < 10; ++k)
    {
        for (UInt32 b = 0; b < ch; ++b)
        {
            abl->mBuffers[b].mNumberChannels = 1;
            abl->mBuffers[b].mDataByteSize = 512 * 4;
            abl->mBuffers[b].mData = mem + b * 512;
        }
        AudioUnitRenderActionFlags flags = 0;
        OSStatus r = AudioUnitRender (u, &flags, &ts, 0, 512, abl);
        float peak = 0;
        for (UInt32 b = 0; b < ch; ++b)
            for (int i = 0; i < 512; ++i) peak = fmaxf (peak, fabsf (((float*) abl->mBuffers[b].mData)[i]));
        if (k < 3 || r) printf ("render %d: status %d, output peak %.3f\n", k, (int) r, peak);
        ts.mSampleTime += 512;
    }
    printf ("pulls: main %d, sidechain %d\n", pulls[0], pulls[1]);
    AudioUnitUninitialize (u);
    AudioComponentInstanceDispose (u);
    return 0;
}

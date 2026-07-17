#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>

static const int AUDIO_SR = 44100;
static const int AUDIO_SAMPLES = AUDIO_SR / 60;  // ~735 per frame

struct AudioOut {
    HWAVEOUT dev;
    WAVEHDR hdr[4];
    int16_t buf[4][AUDIO_SAMPLES];
    int next;
    bool ok;

    bool open() {
        WAVEFORMATEX fmt;
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = AUDIO_SR;
        fmt.nAvgBytesPerSec = AUDIO_SR * 2;
        fmt.nBlockAlign = 2;
        fmt.wBitsPerSample = 16;
        fmt.cbSize = 0;

        if (waveOutOpen(&dev, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return false;

        for (int i = 0; i < 4; i++) {
            memset(&hdr[i], 0, sizeof(hdr[i]));
            hdr[i].lpData = (LPSTR)buf[i];
            hdr[i].dwBufferLength = sizeof(buf[i]);
            hdr[i].dwFlags = WHDR_DONE;
        }
        next = 0;
        ok = true;
        return true;
    }

    void push(int16_t* data, int samples) {
        if (!ok) return;
        for (int i = 0; i < 4; i++) {
            int idx = (next + i) % 4;
            if (hdr[idx].dwFlags & WHDR_DONE) {
                if (hdr[idx].dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(dev, &hdr[idx], sizeof(WAVEHDR));
                memcpy(buf[idx], data, samples * sizeof(int16_t));
                hdr[idx].dwBufferLength = samples * sizeof(int16_t);
                waveOutPrepareHeader(dev, &hdr[idx], sizeof(WAVEHDR));
                waveOutWrite(dev, &hdr[idx], sizeof(WAVEHDR));
                next = (idx + 1) % 4;
                return;
            }
        }
    }

    void close() {
        if (!ok) return;
        ok = false;
        waveOutReset(dev);
        for (int i = 0; i < 4; i++)
            if (hdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(dev, &hdr[i], sizeof(WAVEHDR));
        waveOutClose(dev);
    }
};

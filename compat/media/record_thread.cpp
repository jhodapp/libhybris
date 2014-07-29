/*
 * Copyright (C) 2014 Canonical Ltd
 * NOTE: Reimplemented starting from Android RecordThread class
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "RecordThread"

#include "record_thread.h"
#include "record_track.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <hybris/media/media_recorder_layer.h>

#include <utils/Log.h>

#define REPORT_FUNCTION() ALOGV("%s \n", __PRETTY_FUNCTION__)

namespace android {

// don't warn about blocked writes or record buffer overflows more often than this
static const nsecs_t kWarningThrottleNs = seconds(5);

// RecordThread loop sleep time upon application overrun or audio HAL read error
static const int kRecordThreadSleepUs = 5000;

ThreadBase::ThreadBase(audio_io_handle_t id)
    : Thread(false),
      mStandby(false),
      mId(id)
{
}

ThreadBase::~ThreadBase()
{
}

void ThreadBase::exit()
{
}

//---------- RecordThread -----------//

RecordThread::RecordThread(uint32_t sampleRate, audio_channel_mask_t channelMask, audio_io_handle_t id)
    : ThreadBase(id),
      m_fifoFd(-1),
      mRsmpOutBuffer(NULL),
      mRsmpInBuffer(NULL),
      mReqChannelCount(popcount(channelMask)),
      mReqSampleRate(sampleRate),
      mFramestoDrop(0)
{
    REPORT_FUNCTION();

    snprintf(mName, kNameLength, "AudioIn_%X", id);
    readInputParameters();

}

RecordThread::~RecordThread()
{
    REPORT_FUNCTION();

    close(m_fifoFd);
}

void RecordThread::destroyTrack_l(const sp<RecordTrack>& track)
{
    REPORT_FUNCTION();

    track->terminate();
    track->mState = RecordTrack::STOPPED;
    // active tracks are removed by threadLoop()
    if (mActiveTrack != track) {
        removeTrack_l(track);
    }
}

void RecordThread::removeTrack_l(const sp<RecordTrack>& track)
{
    REPORT_FUNCTION();

    mTracks.remove(track);
}

bool RecordThread::threadLoop()
{
    REPORT_FUNCTION();

    AudioBufferProvider::Buffer buffer;
    sp<RecordTrack> activeTrack;
    //Vector< sp<EffectChain> > effectChains;

    nsecs_t lastWarning = 0;

    //inputStandBy();
    {
        Mutex::Autolock _l(mLock);
        activeTrack = mActiveTrack;
        //acquireWakeLock_l(activeTrack != 0 ? activeTrack->uid() : -1);
    }

    // used to verify we've read at least once before evaluating how many bytes were read
    bool readOnce = false;

    // start recording
    while (!exitPending()) {

        //processConfigEvents();

        { // scope for mLock
            Mutex::Autolock _l(mLock);
            //checkForNewParameters_l();
            if (mActiveTrack != 0 && activeTrack != mActiveTrack) {
                SortedVector<int> tmp;
                tmp.add(mActiveTrack->uid());
                //updateWakeLockUids_l(tmp);
            }
            activeTrack = mActiveTrack;
#if 0
            if (mActiveTrack == 0 && mConfigEvents.isEmpty()) {
                standby();
#else
            if (mActiveTrack == 0) {
#endif

                if (exitPending()) {
                    break;
                }

                //releaseWakeLock_l();
                ALOGV("RecordThread: loop stopping");
                // go to sleep
                mWaitWorkCV.wait(mLock);
                ALOGV("RecordThread: loop starting");
                //acquireWakeLock_l(mActiveTrack != 0 ? mActiveTrack->uid() : -1);
                continue;
            }

            if (mActiveTrack != 0) {
                if (mActiveTrack->isTerminated()) {
                    removeTrack_l(mActiveTrack);
                    mActiveTrack.clear();
                } else if (mActiveTrack->mState == RecordTrack::PAUSING) {
                    //standby();
                    mActiveTrack.clear();
                    mStartStopCond.broadcast();
                } else if (mActiveTrack->mState == RecordTrack::RESUMING) {
                    if (mReqChannelCount != mActiveTrack->channelCount()) {
                        mActiveTrack.clear();
                        mStartStopCond.broadcast();
                    } else if (readOnce) {
                        // record start succeeds only if first read from audio input
                        // succeeds
                        if (mBytesRead >= 0) {
                            mActiveTrack->mState = RecordTrack::ACTIVE;
                        } else {
                            mActiveTrack.clear();
                        }
                        mStartStopCond.broadcast();
                    }
                    mStandby = false;
                }
            }

            //lockEffectChains_l(effectChains);
        }

        if (mActiveTrack != 0) {
            if (mActiveTrack->mState != RecordTrack::ACTIVE &&
                mActiveTrack->mState != RecordTrack::RESUMING) {
                //unlockEffectChains(effectChains);
                usleep(kRecordThreadSleepUs);
                continue;
            }
#if 0
            for (size_t i = 0; i < effectChains.size(); i ++) {
                effectChains[i]->process_l();
            }
#endif

            buffer.frameCount = mFrameCount;
            ALOGV("Calling mActiveTrack->getNextBuffer()");
            status_t status = mActiveTrack->getNextBuffer(&buffer);
            if (status == NO_ERROR) {
                readOnce = true;
                size_t framesOut = buffer.frameCount;
                //if (mResampler == NULL) {
                    // no resampling
                    while (framesOut) {
                        size_t framesIn = mFrameCount - mRsmpInIndex;
                        if (framesIn) {
                            int8_t *src = (int8_t *)mRsmpInBuffer + mRsmpInIndex * mFrameSize;
                            int8_t *dst = buffer.i8 + (buffer.frameCount - framesOut) *
                                    mActiveTrack->mFrameSize;
                            if (framesIn > framesOut)
                                framesIn = framesOut;
                            mRsmpInIndex += framesIn;
                            framesOut -= framesIn;
                            if (mChannelCount == mReqChannelCount) {
                                memcpy(dst, src, framesIn * mFrameSize);
                            } else {
                                if (mChannelCount == 1) {
#if 0
                                    upmix_to_stereo_i16_from_mono_i16((int16_t *)dst,
                                            (int16_t *)src, framesIn);
#endif
                                } else {
#if 0
                                    downmix_to_mono_i16_from_stereo_i16((int16_t *)dst,
                                            (int16_t *)src, framesIn);
#endif
                                }
                            }
                        }
                        if (framesOut && mFrameCount == mRsmpInIndex) {
                            void *readInto;
                            if (framesOut == mFrameCount && mChannelCount == mReqChannelCount) {
                                readInto = buffer.raw;
                                framesOut = 0;
                            } else {
                                readInto = mRsmpInBuffer;
                                mRsmpInIndex = 0;
                            }
                            // Read from the named pipe /dev/socket/micshm
                            ALOGD("Calling this->readPipe()");
                            mBytesRead = readPipe();
                            if (mBytesRead <= 0) {
                                if ((mBytesRead < 0) && (mActiveTrack->mState == RecordTrack::ACTIVE))
                                {
                                    ALOGE("Error reading audio input");
                                    // Force input into standby so that it tries to
                                    // recover at next read attempt
                                    //inputStandBy();
                                    usleep(kRecordThreadSleepUs);
                                }
                                mRsmpInIndex = mFrameCount;
                                framesOut = 0;
                                buffer.frameCount = 0;
                            }
                        }
                    }
                //}
#if 0
                else {
                    // resampling

                    // resampler accumulates, but we only have one source track
                    memset(mRsmpOutBuffer, 0, framesOut * FCC_2 * sizeof(int32_t));
                    // alter output frame count as if we were expecting stereo samples
                    if (mChannelCount == 1 && mReqChannelCount == 1) {
                        framesOut >>= 1;
                    }
                    mResampler->resample(mRsmpOutBuffer, framesOut,
                            this /* AudioBufferProvider* */);
                    // ditherAndClamp() works as long as all buffers returned by
                    // mActiveTrack->getNextBuffer() are 32 bit aligned which should be always true.
                    if (mChannelCount == 2 && mReqChannelCount == 1) {
                        // temporarily type pun mRsmpOutBuffer from Q19.12 to int16_t
                        ditherAndClamp(mRsmpOutBuffer, mRsmpOutBuffer, framesOut);
                        // the resampler always outputs stereo samples:
                        // do post stereo to mono conversion
                        downmix_to_mono_i16_from_stereo_i16(buffer.i16, (int16_t *)mRsmpOutBuffer,
                                framesOut);
                    } else {
                        ditherAndClamp((int32_t *)buffer.raw, mRsmpOutBuffer, framesOut);
                    }
                    // now done with mRsmpOutBuffer

                }
#endif
                if (mFramestoDrop == 0) {
                    ALOGV("Calling releaseBuffer(line: %d)", __LINE__);
                    mActiveTrack->releaseBuffer(&buffer);
                } else {
                    if (mFramestoDrop > 0) {
                        mFramestoDrop -= buffer.frameCount;
                        if (mFramestoDrop <= 0) {
                            clearSyncStartEvent();
                        }
                    } else {
                        mFramestoDrop += buffer.frameCount;
                        if (mFramestoDrop >= 0 || mSyncStartEvent == 0 ||
                                mSyncStartEvent->isCancelled()) {
                            ALOGW("Synced record %s, session %d, trigger session %d",
                                  (mFramestoDrop >= 0) ? "timed out" : "cancelled",
                                  mActiveTrack->sessionId(),
                                  (mSyncStartEvent != 0) ? mSyncStartEvent->triggerSession() : 0);
                            clearSyncStartEvent();
                        }
                    }
                }
                //mActiveTrack->clearOverflow();
            }
            // client isn't retrieving buffers fast enough
            else {
                ALOGW("Client isn't retrieving buffers fast enough, examine this code!");
#if 0
                if (!mActiveTrack->setOverflow()) {
                    nsecs_t now = systemTime();
                    if ((now - lastWarning) > kWarningThrottleNs) {
                        ALOGW("RecordThread: buffer overflow");
                        lastWarning = now;
                    }
                }
#endif
                // Release the processor for a while before asking for a new buffer.
                // This will give the application more chance to read from the buffer and
                // clear the overflow.
                usleep(kRecordThreadSleepUs);
            }
        }
        // enable changes in effect chain
        //unlockEffectChains(effectChains);
        //effectChains.clear();
    }

    //standby();

    {
        Mutex::Autolock _l(mLock);
        for (size_t i = 0; i < mTracks.size(); i++) {
            sp<RecordTrack> track = mTracks[i];
            track->invalidate();
        }
        mActiveTrack.clear();
        mStartStopCond.broadcast();
    }

    //releaseWakeLock();

    ALOGV("RecordThread %p exiting", this);
    return false;
}

status_t RecordThread::readyToRun()
{
    REPORT_FUNCTION();

    return NO_ERROR;
}

void RecordThread::onFirstRef()
{
    REPORT_FUNCTION();

    run(mName, PRIORITY_URGENT_AUDIO);
}

sp<RecordTrack> RecordThread::createRecordTrack_l(
        uint32_t sampleRate,
        audio_format_t format,
        audio_channel_mask_t channelMask,
        size_t frameCount,
        int sessionId,
        int uid,
        pid_t tid,
        status_t *status)
{
    REPORT_FUNCTION();

    sp<RecordTrack> track;
    status_t lStatus;

    { // scope for mLock
        Mutex::Autolock _l(mLock);

        track = new RecordTrack(this, sampleRate,
                      format, channelMask, frameCount, 0 /* sharedBuffer */, sessionId, uid);

#if 1
        if (track->getCblk() == 0) {
            ALOGE("createRecordTrack_l() no control block");
            lStatus = NO_MEMORY;
            track.clear();
            goto Exit;
        }
#else
        ALOGI("Not checking track->getCblk because it seems unnecessary");
#endif
        mTracks.add(track);

#if 0
        // disable AEC and NS if the device is a BT SCO headset supporting those pre processings
        bool suspend = audio_is_bluetooth_sco_device(mInDevice) &&
                        mAudioFlinger->btNrecIsOff();
        setEffectSuspended_l(FX_IID_AEC, suspend, sessionId);
        setEffectSuspended_l(FX_IID_NS, suspend, sessionId);
#endif
    }
    lStatus = NO_ERROR;

Exit:
    if (status) {
        *status = lStatus;
    }
    return track;
}

status_t RecordThread::start(RecordTrack* recordTrack,
        AudioSystem::sync_event_t event,
        int triggerSession)
{
    ALOGV("RecordThread::start event %d, triggerSession %d", event, triggerSession);
    sp<ThreadBase> strongMe = this;
    status_t status = NO_ERROR;

    {
        AutoMutex lock(mLock);
        if (mActiveTrack != 0) {
            if (recordTrack != mActiveTrack.get()) {
                status = -EBUSY;
            } else if (mActiveTrack->mState == RecordTrack::PAUSING) {
                mActiveTrack->mState = RecordTrack::ACTIVE;
            }
            return status;
        }

        recordTrack->mState = RecordTrack::IDLE;
        mActiveTrack = recordTrack;
        //status_t status = AudioSystem::startInput(mId);

        if (status != NO_ERROR) {
            mActiveTrack.clear();
            clearSyncStartEvent();
            return status;
        }
        mRsmpInIndex = mFrameCount;
        mBytesRead = 0;
#if 0
        if (mResampler != NULL) {
            mResampler->reset();
        }
#endif
        mActiveTrack->mState = RecordTrack::RESUMING;
        // signal thread to start
        ALOGV("Signal record thread");
        mWaitWorkCV.broadcast();
        // do not wait for mStartStopCond if exiting
        if (exitPending()) {
            mActiveTrack.clear();
            status = INVALID_OPERATION;
            goto startError;
        }
        mStartStopCond.wait(mLock);
        if (mActiveTrack == 0) {
            ALOGV("Record failed to start");
            status = BAD_VALUE;
            goto startError;
        }
        ALOGV("Record started OK");
        return status;
    }

startError:
    //AudioSystem::stopInput(mId);
    clearSyncStartEvent();
    close(m_fifoFd);
    return status;
}

void RecordThread::clearSyncStartEvent()
{
    if (mSyncStartEvent != 0) {
        mSyncStartEvent->cancel();
    }
    mSyncStartEvent.clear();
    mFramestoDrop = 0;
}

bool RecordThread::stop(RecordTrack* recordTrack)
{
    REPORT_FUNCTION();

    AutoMutex _l(mLock);
    if (recordTrack != mActiveTrack.get() || recordTrack->mState == RecordTrack::PAUSING) {
        return false;
    }
    recordTrack->mState = RecordTrack::PAUSING;
    // do not wait for mStartStopCond if exiting
    if (exitPending()) {
        return true;
    }
    mStartStopCond.wait(mLock);
    // if we have been restarted, recordTrack == mActiveTrack.get() here
    if (exitPending() || recordTrack != mActiveTrack.get()) {
        ALOGV("Record stopped OK");
        return true;
    }
    return false;
}

#if 0
AudioStreamIn* RecordThread::clearInput()
{
}

audio_stream_t* RecordThread::stream() const
{
}
#endif

status_t RecordThread::getNextBuffer(AudioBufferProvider::Buffer* buffer, int64_t pts)
{
    REPORT_FUNCTION();

    size_t framesReq = buffer->frameCount;
    size_t framesReady = mFrameCount - mRsmpInIndex;
    int channelCount;

    if (framesReady == 0) {
        // Read from the named pipe /dev/socket/micshm
        mBytesRead = readPipe();
        if (mBytesRead <= 0) {
            if ((mBytesRead < 0) && (mActiveTrack->mState == RecordTrack::ACTIVE)) {
                ALOGE("RecordThread::getNextBuffer() Error reading audio input");
                // Force input into standby so that it tries to
                // recover at next read attempt
                //inputStandBy();
                usleep(kRecordThreadSleepUs);
            }
            buffer->raw = NULL;
            buffer->frameCount = 0;
            return NOT_ENOUGH_DATA;
        }
        mRsmpInIndex = 0;
        framesReady = mFrameCount;
    }

    if (framesReq > framesReady) {
        framesReq = framesReady;
    }

    if (mChannelCount == 1 && mReqChannelCount == 2) {
        channelCount = 1;
    } else {
        channelCount = 2;
    }
    buffer->raw = mRsmpInBuffer + mRsmpInIndex * channelCount;
    buffer->frameCount = framesReq;
    return NO_ERROR;
}

void RecordThread::releaseBuffer(AudioBufferProvider::Buffer* buffer)
{
    REPORT_FUNCTION();

    mRsmpInIndex += buffer->frameCount;
    buffer->frameCount = 0;
}

void RecordThread::readInputParameters()
{
    REPORT_FUNCTION();

    // TODO: Add the rest of the input parameters
    mSampleRate = 44100;
    mChannelMask = 0x10;   // FIXME: where should this come from?
    mChannelCount = popcount(mChannelMask);
    mFormat = AUDIO_FORMAT_PCM_16_BIT;
    mFrameSize = 2;
    mBufferSize = MIC_READ_BUF_SIZE * sizeof(int16_t);
    ALOGD("mBufferSize: %d", mBufferSize);
    mFrameCount = mBufferSize / mFrameSize;
    mRsmpInBuffer = new int16_t[mBufferSize];
    mRsmpInIndex = mFrameCount;
}

bool RecordThread::openPipe()
{
    if (m_fifoFd > 0) {
        ALOGW("/dev/socket/micshm already opened, not opening twice");
        return true;
    }

    // Open read access to the named pipe that lives on the application side
    m_fifoFd = open("/dev/socket/micshm", O_RDONLY); //| O_NONBLOCK);
    if (m_fifoFd < 0) {
        ALOGE("Failed to open named pipe /dev/socket/micshm %s", strerror(errno));
        return false;
    }

    return true;
}

ssize_t RecordThread::readPipe()
{
    REPORT_FUNCTION();

    if (m_fifoFd < 0) {
        openPipe();
    }

    ssize_t size = 0;
    //memset(mRsmpInBuffer, 0, mBufferSize);
    size = read(m_fifoFd, mRsmpInBuffer, mBufferSize);
    if (size < 0)
    {
        ALOGE("Failed to read in data from named pipe /dev/socket/micshm: %s", strerror(errno));
        size = 0;
    }
    else
        ALOGD("Read in %d bytes into mRsmpInBuffer", size);

    return size;
}

} // namespace android

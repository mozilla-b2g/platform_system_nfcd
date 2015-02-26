/*
 * Copyright (C) 2013-2014  Mozilla Foundation
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

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/prctl.h>
#include <cutils/sockets.h>
#include <cutils/record_stream.h>
#include <unistd.h>
#include <queue>
#include <string>

#include "IpcSocketListener.h"
#include "NfcIpcSocket.h"
#include "MessageHandler.h"
#include "NfcDebug.h"

#define NFCD_SOCKET_NAME "nfcd"
#define MAX_COMMAND_BYTES (8 * 1024)

using android::Parcel;

static int nfcdRw;

MessageHandler* NfcIpcSocket::sMsgHandler = NULL;

/**
 * NfcIpcSocket
 */
NfcIpcSocket* NfcIpcSocket::sInstance = NULL;

NfcIpcSocket* NfcIpcSocket::Instance() {
  if (!sInstance) {
    sInstance = new NfcIpcSocket();
  }
  return sInstance;
}

NfcIpcSocket::NfcIpcSocket()
{
}

NfcIpcSocket::~NfcIpcSocket()
{
}

void NfcIpcSocket::Initialize(MessageHandler* aMsgHandler)
{
  InitSocket();
  sMsgHandler = aMsgHandler;
}

void NfcIpcSocket::InitSocket()
{
  mSleep_spec.tv_sec = 0;
  mSleep_spec.tv_nsec = 500 * 1000;
  mSleep_spec_rem.tv_sec = 0;
  mSleep_spec_rem.tv_nsec = 0;
}

int NfcIpcSocket::GetListenSocket() {
  const int nfcdConn = android_get_control_socket(NFCD_SOCKET_NAME);
  if (nfcdConn < 0) {
    ALOGE("Could not connect to %s socket: %s\n", NFCD_SOCKET_NAME, strerror(errno));
    return -1;
  }

  if (listen(nfcdConn, 4) != 0) {
    return -1;
  }
  return nfcdConn;
}

void NfcIpcSocket::SetSocketListener(IpcSocketListener* aListener) {
  mListener = aListener;
}

void NfcIpcSocket::Loop()
{
  bool connected = false;
  int nfcdConn = -1;
  int ret;

  while(1) {
    struct sockaddr_un peeraddr;
    socklen_t socklen = sizeof (peeraddr);

    if (!connected) {
      nfcdConn = GetListenSocket();
      if (nfcdConn < 0) {
        nanosleep(&mSleep_spec, &mSleep_spec_rem);
        continue;
      }
    }

    nfcdRw = accept(nfcdConn, (struct sockaddr*)&peeraddr, &socklen);

    if (nfcdRw < 0 ) {
      ALOGE("Error on accept() errno:%d", errno);
      /* start listening for new connections again */
      continue;
    }

    ret = fcntl(nfcdRw, F_SETFL, O_NONBLOCK);
    if (ret < 0) {
      ALOGE ("Error setting O_NONBLOCK errno:%d", errno);
    }

    ALOGD("Socket connected");
    connected = true;

    RecordStream *rs = record_stream_new(nfcdRw, MAX_COMMAND_BYTES);

    mListener->OnConnected();

    struct pollfd fds[1];
    fds[0].fd = nfcdRw;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    while(connected) {
      poll(fds, 1, -1);
      if(fds[0].revents > 0) {
        fds[0].revents = 0;

        void* data;
        size_t dataLen;
        int ret = record_stream_get_next(rs, &data, &dataLen);
        ALOGD(" %d of bytes to be sent... data=%p ret=%d", dataLen, data, ret);
        if (ret == 0 && data == NULL) {
          // end-of-stream
          break;
        } else if (ret < 0) {
          break;
        }
        WriteToIncomingQueue((uint8_t*)data, dataLen);
      }
    }
    record_stream_free(rs);
    close(nfcdRw);
  }

  return;
}

// Write NFC data to Gecko
// Outgoing queue contain the data should be send to gecko
// TODO check thread, this should run on the NfcService thread.
void NfcIpcSocket::WriteToOutgoingQueue(uint8_t* aData, size_t aDataLen)
{
  ALOGD("%s enter, data=%p, dataLen=%d", __func__, aData, aDataLen);

  if (aData == NULL || aDataLen == 0) {
    return;
  }

  size_t writeOffset = 0;
  int written = 0;

  ALOGD("Writing %d bytes to gecko ", aDataLen);
  while (writeOffset < aDataLen) {
    do {
      written = write (nfcdRw, aData + writeOffset, aDataLen - writeOffset);
    } while (written < 0 && errno == EINTR);

    if (written >= 0) {
      writeOffset += written;
    } else {
      ALOGE("Response: unexpected error on write errno:%d", errno);
      break;
    }
  }
}

// Write Gecko data to NFC
// Incoming queue contains
// TODO check thread, this should run on top of main thread of nfcd.
void NfcIpcSocket::WriteToIncomingQueue(uint8_t* aData, size_t aDataLen)
{
  ALOGD("%s enter, data=%p, dataLen=%d", __func__, aData, aDataLen);

  if (aData != NULL && aDataLen > 0) {
    sMsgHandler->ProcessRequest(aData, aDataLen);
  }
}

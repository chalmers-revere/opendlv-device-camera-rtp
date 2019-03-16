/*
 * Copyright (C) 2019 Ola Benderius
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"

#include <curl/curl.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

size_t parseSdpData(void *ptr, size_t size, size_t nmemb, void *userptr)
{
  size_t nbytes = size * nmemb;
  std::string *s = static_cast<std::string *>(userptr);

  std::istringstream f(static_cast<char *>(ptr));
  std::string line;
  while (std::getline(f, line)) {
    std::cout << line << std::endl;
    // Find the latest occurance.
    if (!line.rfind("a=control:")) {
      s->assign(line.substr(10));
    }
  }

  return nbytes;
}

void sendMagicNumber(uint32_t srcPort, std::string addr, uint32_t dstPort)
{
  int32_t fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  struct sockaddr_in src;
  std::memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_addr.s_addr = htonl(INADDR_ANY);
  src.sin_port = htons(srcPort); 

  bind(fd, reinterpret_cast<struct sockaddr *>(&src), sizeof(src));
  
  struct sockaddr_in dst;
  std::memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = inet_addr(addr.c_str());
  dst.sin_port = htons(dstPort);

  uint8_t data[4] = {0xce, 0xfa, 0xed, 0xfe};

  sendto(fd, data, 4, 0, reinterpret_cast<const struct sockaddr *>(&dst),
      sizeof(dst));

  shutdown(fd, SHUT_RDWR);
  close(fd);
}

int32_t main(int32_t argc, char **argv)
{
  int32_t retCode{1};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if ( (0 == commandlineArguments.count("url")) ||
      (0 == commandlineArguments.count("width")) ||
      (0 == commandlineArguments.count("height")) ||
      (0 == commandlineArguments.count("cid")) ) {
    std::cerr << argv[0] << " interfaces with the given RTSP/RTP-based camera "
      << "and sends the compressed frames as OpenDLV messages." << std::endl
      << "Usage:   " << argv[0] << " --url=<URL> --width=<width> "
      << "--height=<height> --cid=<CID> [--id<ID>] [--verbose]" 
      << std::endl
      << "         --cid:       CID of the OD4Session to receive Envelopes for "
      << "recording" << std::endl
      << "         --id:        ID to use in case of multiple instances of" 
      << argv[0] << std::endl
      << "         --rec:       name of the recording file; default: "
      << "YYYY-MM-DD_HHMMSS.rec" << std::endl
      << "         --recsuffix: additional suffix to add to the .rec file" 
      << std::endl
      << "         --remote:    listen for cluon.data.RecorderCommand to "
      << "start/stop recording" << std::endl
      << "         --url:       URL providing an MJPEG stream over http" 
      << std::endl
      << "         --name.i420: name of the shared memory for the I420 "
      << "formatted image; when omitted, video0.i420 is chosen" << std::endl
      << "         --name.argb: name of the shared memory for the I420 "
      << "formatted image; when omitted, video0.argb is chosen" << std::endl
      << "         --width:     desired width of a frame" << std::endl
      << "         --height:    desired height of a frame" << std::endl
      << "         --freq:      desired frame rate" << std::endl
      << "         --verbose:   display captured image" << std::endl
      << "Example: " << argv[0] << " --url=http://192.168.0.11?mjpeg "
      << "--width=640 --height=480 --verbose --rec=myFile.rec --cid=111 "
      << "--remote" << std::endl;
  } else {
    std::string const URL{commandlineArguments["url"]};
    uint32_t ID{(commandlineArguments.count("id") != 0) ?
        static_cast<uint32_t>(std::stoi(commandlineArguments["id"])) : 0};
    uint32_t WIDTH{
      static_cast<uint32_t>(std::stoi(commandlineArguments["width"]))};
    uint32_t HEIGHT{
      static_cast<uint32_t>(std::stoi(commandlineArguments["height"]))};
    bool VERBOSE{commandlineArguments.count("verbose") != 0};

    cluon::OD4Session od4{
      static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};

    CURL *curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_VERBOSE, (VERBOSE ? 1 : 0));
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
   // curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);

    std::string const transport("RTP/AVP;unicast;client_port=33056-33057");
    std::string const range("npt=0.000-");

    // RTSP options
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_OPTIONS);
    curl_easy_perform(curl);

    // RTSP describe
    std::string streamUri;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &streamUri);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parseSdpData);
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_DESCRIBE);
    curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);

    // RTSP setup
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, streamUri.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_TRANSPORT, transport.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_SETUP);
    curl_easy_perform(curl);

    // Send magic number
    sendMagicNumber(33056, "84.217.42.199", 50000);
    sendMagicNumber(33057, "84.217.42.199", 50001);

    // RTSP play
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_PLAY);
    curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_RANGE, nullptr);

    uint32_t h264NalBufPos = 0;
    char h264NalBuf[65000];
    bool hasNalStart = false;

    auto onStreamData =
      [&od4, &h264NalBuf, &h264NalBufPos, &hasNalStart, &WIDTH, &HEIGHT, &ID,
        &VERBOSE](
        std::string &&data, std::string &&,
        std::chrono::system_clock::time_point &&) noexcept {
      char const *buf_start = static_cast<char const *>(data.c_str());

      uint8_t b0 = *buf_start;
      uint8_t version = (b0 >> 6);
      bool hasPadding = (b0 & 0x20) >> 5;
      bool hasExtension = (b0 & 0x10) >> 4;
      uint8_t csrcCount = (b0 & 0xf);
      
      uint8_t b1 = *(buf_start + 1);
      bool profileMarker = (b1 >> 7);
      uint8_t profileType = (b1 & 0x7f);

      uint32_t b2b3;
      memcpy(&b2b3, buf_start + 2, 2);
      uint32_t sequenceNumber = ntohs(b2b3);
      
      uint32_t b4b5b6b7;
      memcpy(&b4b5b6b7, buf_start + 4, 4);
      uint32_t timestamp = ntohl(b4b5b6b7);
      
      uint32_t b8b9b10b11;
      memcpy(&b8b9b10b11, buf_start + 8, 4);
      uint32_t ssrcId = ntohl(b8b9b10b11);

      uint32_t paddingLen = 0;
      if (hasPadding) {
        paddingLen = *(buf_start + data.length() - 1);
      }

      uint8_t b12 = *(buf_start + 12);
      uint8_t h264RtpF = b12 >> 7;
      if (h264RtpF) {
        std::cerr << "Unexpected H264 RTP header, F=1." << std::endl;
        return;
      }
      uint8_t h264RtpNri = (b12 & 0x60f) >> 5;
      uint8_t h264RtpType = (b12 & 0x1f);
      bool isFragmented = false;

   //   if (h264RtpType == 1) {
        uint32_t nalLen = data.length() - 13 - paddingLen;
        opendlv::proxy::ImageReading ir;
          ir.fourcc("h264").width(WIDTH).height(HEIGHT).data(
              std::string(buf_start + 13, nalLen));
          od4.send(ir, cluon::time::now(), ID);
    /*  } else if (h264RtpType == 28) {
        isFragmented = true;

        uint8_t b13 = *(buf_start + 13);
        bool isStartFragment = b13 >> 7;
        bool isEndFragment = (b13 & 0x40) >> 6;
        h264RtpType = (b13 & 0x1f);

        if (isStartFragment) {
          h264NalBufPos = 0;
          hasNalStart = true;
        }
        
        uint32_t nalLen = data.length() - 14 - paddingLen;
        memcpy(h264NalBuf + h264NalBufPos, buf_start + 14, nalLen);
        h264NalBufPos += nalLen;

        std::cout << h264NalBufPos << std::endl;

        if (isEndFragment && hasNalStart) {
          opendlv::proxy::ImageReading ir;
          ir.fourcc("h264").width(WIDTH).height(HEIGHT).data(
              std::string(h264NalBuf, h264NalBufPos));
          od4.send(ir, cluon::time::now(), ID);
          hasNalStart = false;
        }
      }
      */

      if (VERBOSE) {
        std::cout << "Got data" << std::endl;
        std::cout << "Version: " << +version << std::endl;
        std::cout << "Has padding: " << hasPadding << std::endl;
        std::cout << "Has extension: " << hasExtension << std::endl;
        std::cout << "CSRC count: " << +csrcCount << std::endl;
        std::cout << "Profile marker: " << profileMarker << std::endl;
        std::cout << "Profile type: " << +profileType << std::endl;
        std::cout << "Sequence number: " << +sequenceNumber << std::endl;
        std::cout << "Timestamp: " << +timestamp << std::endl;
        std::cout << "SSRC id: " << +ssrcId << std::endl;

      //  std::cout << "Payload len: " << payloadLen << std::endl;

        std::cout << "H264 RTP header NRI: " << +h264RtpNri << std::endl;
        std::cout << "H264 RTP header type: " << +h264RtpType << std::endl;
        std::cout << "H264 RTP is fragmented: " << isFragmented << std::endl;
      }
    };
    
    auto onControlData = [&VERBOSE](std::string &&data, std::string &&,
        std::chrono::system_clock::time_point &&) noexcept {
      if (VERBOSE) {
        std::cout << "Got control data: " << std::endl;
      }
    };

    {
      cluon::UDPReceiver streamUdpReceiver{"10.42.170.243", 33056, onStreamData,
        50000};
    
      cluon::UDPReceiver controlUdpReceiver{"10.42.170.243", 33057,
        onControlData, 50001};

      while (od4.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    }

    // RTSP teardown
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_TEARDOWN);
    curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    retCode = 0;
  }
  return retCode;
}


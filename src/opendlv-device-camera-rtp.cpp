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

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include <curl/curl.h>

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"
#include "sps-decoder.hpp"

struct SdpData {
  std::string streamUri;
  std::string sps;
  std::string pps;
  double framerate;
};

size_t parseSdpData(void *ptr, size_t size, size_t nmemb, void *userptr)
{
  size_t nbytes = size * nmemb;
  SdpData *sdpData = static_cast<SdpData *>(userptr);

  std::istringstream f(static_cast<char *>(ptr));
  std::string line;
  while (std::getline(f, line)) {
    {
      std::string n("a=control:");
      if (line.find(n) != -1) {
        sdpData->streamUri = line.substr(n.length());
      }
    }
    {
      std::string n("sprop-parameter-sets=");
      int32_t j = line.find(n);
      if (j != -1) {
        cluon::FromJSONVisitor base64Decoder;
        std::string spsAndPps = line.substr(j + n.length());
        int32_t i = spsAndPps.find(",");
        sdpData->sps = base64Decoder.decodeBase64(spsAndPps.substr(0, i));
        sdpData->pps = base64Decoder.decodeBase64(spsAndPps.substr(i + 1));
      }
    }
    {
      std::string n("a=framerate:");
      if (line.find(n) != -1) {
        sdpData->framerate = stod(line.substr(n.length()));
      }
    }
  }

  return nbytes;
}

std::string getHostname(std::string uri) {
  std::string tmp;
  size_t m = uri.find_first_of("@");
  if (m == -1) {
    size_t k = uri.find_first_of(":");
    std::string protocol = uri.substr(0, k); 
    tmp = uri.substr(k + 3);
  } else {
    tmp = uri.substr(m + 1);
  }
  size_t l = tmp.find_first_of(":");
  if (l == -1) {
    l = tmp.find_first_of("/");
    if (l == -1) {
      l = tmp.length();
    }
  }
  return tmp.substr(0, l);
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
      (0 == commandlineArguments.count("cid")) ) {
    std::cerr << argv[0] << " interfaces with the given RTSP/RTP-based camera "
      << "and sends the compressed frames as OpenDLV messages." << std::endl
      << "Usage:   " << argv[0] << " --url=<URL> --cid=<CID> [--id<ID>] "
      << "[--verbose]" << std::endl
      << "         --cid:       CID of the OD4Session to receive Envelopes for "
      << "recording" << std::endl
      << "         --id:        ID to use in case of multiple instances of" 
      << argv[0] << std::endl
      << "         --url:       URL providing an MJPEG stream over http" 
      << std::endl
      << "         --verbose:   display captured image" << std::endl
      << "Example: " << argv[0] << " --url=http://192.168.0.11?mjpeg "
      << "--verbose --cid=111" << std::endl;
  } else {
    std::string const URL{commandlineArguments["url"]};
    uint32_t ID{(commandlineArguments.count("id") != 0) ?
        static_cast<uint32_t>(std::stoi(commandlineArguments["id"])) : 0};
    bool VERBOSE{commandlineArguments.count("verbose") != 0};

    std::string hostname = getHostname(URL);
    std::string localHostname;

    // TODO: Find automatically.
    uint32_t const clientPortA = 33056;
    uint32_t const clientPortB = 33057;
    uint32_t const serverPortA = 50000;
    uint32_t const serverPortB = 50001;

    cluon::OD4Session od4{
      static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};

    CURL *curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_VERBOSE, (VERBOSE ? 1 : 0));
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
   // curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);

    std::string const transport("RTP/AVP;unicast;client_port=" 
        + std::to_string(clientPortA) + "-" + std::to_string(clientPortB));
    std::string const range("npt=0.000-");

    // RTSP options
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_OPTIONS);
    curl_easy_perform(curl);

    {
      char *ip;
      curl_easy_getinfo(curl, CURLINFO_LOCAL_IP, &ip);
      localHostname = std::string(ip);
    }

    // RTSP describe
    SdpData sdpData;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sdpData);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parseSdpData);
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_DESCRIBE);
    curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);

    // RTSP setup
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, sdpData.streamUri.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_TRANSPORT, transport.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_SETUP);
    curl_easy_perform(curl);

    // Send magic number
    sendMagicNumber(clientPortA, hostname, serverPortA);
    sendMagicNumber(clientPortB, hostname, serverPortB);

    // RTSP play
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_PLAY);
    curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_RANGE, nullptr);


    SpsInfo spsInfo = decodeSps(
        reinterpret_cast<uint8_t const *>(sdpData.sps.data()),
        sdpData.sps.length());
    uint32_t width = spsInfo.width;
    uint32_t height = spsInfo.height;

    std::cout << "Connection to RTP camera established. Resolution " 
      << width << "x" << height << ", framerate " << sdpData.framerate 
      << std::endl;

    std::string outData;

    auto onStreamData =
      [&od4, &outData, &width, &height, &sdpData, &ID, &VERBOSE](
        std::string &&data, std::string &&,
        std::chrono::system_clock::time_point &&) noexcept {
      char const *buf_start = static_cast<char const *>(data.c_str());

      static uint8_t const nalPrefix[] = {0x00, 0x00, 0x01};

      uint8_t const b0 = *buf_start;
      uint8_t const version = (b0 >> 6);
      bool const hasPadding = (b0 & 0x20) >> 5;
      bool const hasExtension = (b0 & 0x10) >> 4;
      uint8_t const csrcCount = (b0 & 0xf);
      
      uint8_t const b1 = *(buf_start + 1);
      bool const profileMarker = (b1 >> 7);
      uint8_t const profileType = (b1 & 0x7f);

      uint32_t b2b3;
      memcpy(&b2b3, buf_start + 2, 2);
      uint32_t const sequenceNumber = ntohs(b2b3);
      
      uint32_t b4b5b6b7;
      memcpy(&b4b5b6b7, buf_start + 4, 4);
      uint32_t const timestamp = ntohl(b4b5b6b7);
      
      uint32_t b8b9b10b11;
      memcpy(&b8b9b10b11, buf_start + 8, 4);
      uint32_t const ssrcId = ntohl(b8b9b10b11);

      uint32_t paddingLen = 0;
      if (hasPadding) {
        paddingLen = *(buf_start + data.length() - 1);
      }

      uint8_t const b12 = *(buf_start + 12);
      uint8_t const h264RtpF = b12 >> 7;
      if (h264RtpF) {
        std::cerr << "Unexpected H264 RTP header, F=1." << std::endl;
        return;
      }
      uint8_t const h264RtpNri = (b12 & 0x60) >> 5;
      uint8_t const h264RtpType = (b12 & 0x1f);
      uint8_t nalType;

      if (h264RtpType >= 1 && h264RtpType <= 23) {
        std::cout << "hej" << std::endl;
        nalType = h264RtpType;
        uint32_t nalLen = data.length() - 12 - paddingLen;
        outData = std::string(&nalPrefix[0], &nalPrefix[0] + 3) 
            + std::string(buf_start + 12, nalLen);


        opendlv::proxy::ImageReading ir;
        ir.fourcc("h264").width(width).height(height).data(outData);
        od4.send(ir, cluon::time::now(), ID);

        outData = "";

      } else if (h264RtpType == 28) {
        uint8_t b13 = *(buf_start + 13);
        bool isStartFragment = b13 >> 7;
        bool isEndFragment = (b13 & 0x40) >> 6;
        nalType = (b13 & 0x1f);

        std::string extra;
        if (isStartFragment) {
          uint8_t nalHeader = (h264RtpNri << 5) | nalType;
          extra = std::string(&nalPrefix[0], &nalPrefix[0] + 3) + sdpData.sps 
            + std::string(&nalPrefix[0], &nalPrefix[0] + 3) + sdpData.pps  
            + std::string(&nalPrefix[0], &nalPrefix[0] + 3)
            + static_cast<char>(nalHeader);
        }

        uint32_t nalLen = data.length() - 14 - paddingLen;
        outData += extra + std::string(buf_start + 14, nalLen);

        if (isEndFragment) {
          opendlv::proxy::ImageReading ir;
          ir.fourcc("h264").width(width).height(height).data(outData);
          od4.send(ir, cluon::time::now(), ID);

          outData = "";
        }
      } else {
        std::cout << "WARNING: unknown RTP H264 payload type: " << h264RtpType
          << std::endl;
      }

      if (VERBOSE) {
        std::cout << "Timestamp: " << +timestamp << std::endl;
      }
    };
    
    auto onControlData = [&VERBOSE](std::string &&data, std::string &&,
        std::chrono::system_clock::time_point &&) noexcept {
      if (VERBOSE) {
        std::cout << "Got control data: " << std::endl;
      }
    };

    {
      cluon::UDPReceiver streamUdpReceiver{localHostname, clientPortA, 
        onStreamData, serverPortA};
    
      cluon::UDPReceiver controlUdpReceiver{localHostname, clientPortB,
        onControlData, serverPortB};

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


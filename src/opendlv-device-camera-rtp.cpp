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
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <random>

#include <curl/curl.h>

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"
#include "sps-decoder.hpp"

struct SdpData {
  std::map<uint32_t, std::string> encoding;
  std::map<uint32_t, std::string> sps;
  std::map<uint32_t, std::string> pps;
  std::map<uint32_t, uint32_t> clockrate;
  std::map<uint32_t, std::string> streamUri;
  double framerate;
};

size_t parseSdpData(void *ptr, size_t size, size_t nmemb, void *userptr)
{
  size_t nbytes = size * nmemb;
  SdpData *sdpData = static_cast<SdpData *>(userptr);

  std::istringstream f(static_cast<char *>(ptr));
  std::string line;
  uint32_t latestPayloadType = 0;
  while (std::getline(f, line)) {
    {
      std::string n("a=rtpmap:");
      if (line.find(n) != -1) {
        std::string s = line.substr(n.length());
        uint32_t payloadType = std::stoi(s.substr(0, s.find(' ')));
        latestPayloadType = payloadType;
        std::string encoding = s.substr(s.find(' ') + 1,
            s.find('/') - (s.find(' ') + 1));
        uint32_t clockrate = std::stoi(s.substr(s.find('/') + 1));
        sdpData->encoding[payloadType] = encoding;
        sdpData->clockrate[payloadType] = clockrate;
      }
    }
    {
      std::string n("a=fmtp:");
      if (line.find(n) != -1) {
        cluon::FromJSONVisitor base64Decoder;

        std::string s = line.substr(n.length());
        uint32_t payloadType = std::stoi(s.substr(0, s.find(' ')));

        n = "sprop-parameter-sets=";
        std::string spsAndPps = s.substr(s.find(n) + n.length());
        int32_t i = spsAndPps.find(",");
        sdpData->sps[payloadType] = 
          base64Decoder.decodeBase64(spsAndPps.substr(0, i));
        sdpData->pps[payloadType] = 
          base64Decoder.decodeBase64(spsAndPps.substr(i + 1));
      }
    }
    {
      std::string n("a=control:");
      if (line.find(n) != -1) {
        sdpData->streamUri[latestPayloadType] = line.substr(n.length());
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
      << "Usage:   " << argv[0] << " --url=<URL> --cid=<CID> "
      << "[--server-port-udp-a=<Port>] [--id=<ID>] "
      << "[--verbose]" << std::endl
      << "         --cid:       CID of the OD4Session to receive Envelopes for "
      << "recording" << std::endl
      << "         --server-port-udp-a: The first UDP port to use (the second "
      << "will always be the number after)"
      << std::endl
      << "         --id:        ID to use in case of multiple instances of "
         "running microservices." 
      << std::endl
      << "         --url:       URL providing an MJPEG stream over http" 
      << std::endl
      << "         --verbose:   show further information" << std::endl
      << "         --remote:    enable remotely activated recording" << std::endl
      << "         --rec:       name of the recording file; default: YYYY-MM-DD_HHMMSS.rec" << std::endl
      << "         --recsuffix: additional suffix to add to the .rec file" << std::endl
      << "Example: " << argv[0] << " --url=http://192.168.0.11?mjpeg "
      << "--verbose --cid=111" << std::endl;
  } else {
    std::string const url{commandlineArguments["url"]};
    uint32_t senderStamp{(commandlineArguments.count("id") != 0) ?
        static_cast<uint32_t>(std::stoi(commandlineArguments["id"])) : 0};
    bool verbose{commandlineArguments.count("verbose") != 0};

    uint32_t const serverPortA = {
      (commandlineArguments.count("server-port-udp-a") != 0) ?
        static_cast<uint32_t>(
            std::stoi(commandlineArguments["server-port-udp-a"])) : 50000};
    uint32_t const serverPortB = serverPortA + 1;
    
    uint32_t const clientPortA = {
      (commandlineArguments.count("client-port-udp-a") != 0) ?
        static_cast<uint32_t>(
            std::stoi(commandlineArguments["client-port-udp-a"])) : 33000};
    uint32_t const clientPortB = clientPortA + 1;

    auto getYYYYMMDD_HHMMSS = [](){
      cluon::data::TimeStamp now = cluon::time::now();

      const long int _seconds = now.seconds();
      struct tm *tm = localtime(&_seconds);

      uint32_t year = (1900 + tm->tm_year);
      uint32_t month = (1 + tm->tm_mon);
      uint32_t dayOfMonth = tm->tm_mday;
      uint32_t hours = tm->tm_hour;
      uint32_t minutes = tm->tm_min;
      uint32_t seconds = tm->tm_sec;

      std::stringstream sstr;
      sstr << year << "-" << ( (month < 10) ? "0" : "" ) << month << "-" << ( (dayOfMonth < 10) ? "0" : "" ) << dayOfMonth
                     << "_" << ( (hours < 10) ? "0" : "" ) << hours
                     << ( (minutes < 10) ? "0" : "" ) << minutes
                     << ( (seconds < 10) ? "0" : "" ) << seconds;

      std::string retVal{sstr.str()};
      return retVal;
    };
    const bool REMOTE{commandlineArguments.count("remote") != 0};
    const std::string REC{(commandlineArguments["rec"].size() != 0) ? commandlineArguments["rec"] : ""};
    const std::string RECSUFFIX{commandlineArguments["recsuffix"]};
    const std::string NAME_RECFILE{(REC.size() != 0) ? REC + RECSUFFIX : (getYYYYMMDD_HHMMSS() + RECSUFFIX + ".rec")};

    if (verbose) {
      std::cout << "Using client ports " << clientPortA << "-" << clientPortB 
        << std::endl;
    }

  /*  uint32_t clientPortA = 33000;
    uint32_t clientPortB = 33001;
    for (uint32_t port = 33000; port < 34000; port += 2) {
      int32_t clientSocketA = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

      struct sockaddr_in src;
      std::memset(&src, 0, sizeof(src));
      src.sin_family = AF_INET;
      src.sin_addr.s_addr = htonl(INADDR_ANY);
      src.sin_port = htons(port); 

      if (bind(clientSocketA, reinterpret_cast<struct sockaddr *>(&src), 
            sizeof(src)) != 0) {
        close(clientSocketA);
        continue;
      }
      
      int32_t clientSocketB = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
      
      std::memset(&src, 0, sizeof(src));
      src.sin_family = AF_INET;
      src.sin_addr.s_addr = htonl(INADDR_ANY);
      src.sin_port = htons(port + 1); 

      if (bind(clientSocketB, reinterpret_cast<struct sockaddr *>(&src), 
            sizeof(src)) != 0) {
        close(clientSocketA);
        close(clientSocketB);
        continue;
      }
        
      close(clientSocketA);
      close(clientSocketB);

      clientPortA = port;
      clientPortB = port + 1;
      break;
    }*/

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;

    std::string hostname = getHostname(url);
    std::string localHostname;

    uint32_t const clientSsrc = dis(gen);

    std::unique_ptr<cluon::OD4Session> od4{new cluon::OD4Session(static_cast<uint16_t>(std::stoi(commandlineArguments["cid"])))};

    std::string nameOfRecFile;
    std::mutex recFileMutex{};
    std::unique_ptr<std::fstream> recFile{nullptr};
    if (!REMOTE) {
      recFile.reset(new std::fstream(NAME_RECFILE.c_str(), std::ios::out|std::ios::binary|std::ios::trunc));
      std::cout << "[opendlv-video-camera-rtp]: Created " << NAME_RECFILE << "." << std::endl;
    }
    else {
      od4.reset(new cluon::OD4Session(static_cast<uint16_t>(std::stoi(commandlineArguments["cid"])),
          [REC, RECSUFFIX, getYYYYMMDD_HHMMSS, &recFileMutex, &recFile, &nameOfRecFile](cluon::data::Envelope &&envelope) noexcept {
        if (cluon::data::RecorderCommand::ID() == envelope.dataType()) {
          std::lock_guard<std::mutex> lck(recFileMutex);
          cluon::data::RecorderCommand rc = cluon::extractMessage<cluon::data::RecorderCommand>(std::move(envelope));
          if (1 == rc.command()) {
            if (recFile && recFile->good()) {
              recFile->flush();
              recFile->close();
              recFile = nullptr;
              std::cout << "[opendlv-video-camera-rtp]: Closed " << nameOfRecFile << "." << std::endl;
            }
            nameOfRecFile = (REC.size() != 0) ? REC + RECSUFFIX : (getYYYYMMDD_HHMMSS() + RECSUFFIX + ".rec");
            recFile.reset(new std::fstream(nameOfRecFile.c_str(), std::ios::out|std::ios::binary|std::ios::trunc));
            std::cout << "[opendlv-video-camera-rtp]: Created " << nameOfRecFile << "." << std::endl;
          }
          else if (2 == rc.command()) {
            if (recFile && recFile->good()) {
              recFile->flush();
              recFile->close();
              std::cout << "[opendlv-video-camera-rtp]: Closed " << nameOfRecFile << "." << std::endl;
            }
            recFile = nullptr;
          }
        }
      }));
    }

    CURL *curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   // curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);

    std::string const transport("RTP/AVP;unicast;client_port=" 
        + std::to_string(clientPortA) + "-" + std::to_string(clientPortB));
    std::string const range("npt=0.000-");

    // RTSP options
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, url.c_str());
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
    for (auto &streamUri : sdpData.streamUri) {
      if (streamUri.first == 96) {
        if (verbose) {
          std::cout << "Found H.264 stream at " << streamUri.second 
            << ". Setting up." << std::endl;
        }
        curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, streamUri.second.c_str());
        curl_easy_setopt(curl, CURLOPT_RTSP_TRANSPORT, transport.c_str());
        curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_SETUP);
        curl_easy_perform(curl);
      }
    }

    // Send magic number
    sendMagicNumber(clientPortA, hostname, serverPortA);
    sendMagicNumber(clientPortB, hostname, serverPortB);

    // RTSP play
    curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, url.c_str());
    curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_PLAY);
    curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_RANGE, nullptr);


    // TODO: make right (properly support different payload types)
    SpsInfo spsInfo = decodeSps(
        reinterpret_cast<uint8_t const *>(sdpData.sps[96].data()),
        sdpData.sps[96].length());
    uint32_t width = spsInfo.width;
    uint32_t height = spsInfo.height;

    std::cout << "Connection to RTP camera established. Resolution " 
      << width << "x" << height << ", framerate " << sdpData.framerate 
      << std::endl;

    std::string outData;

    std::mutex rtcpMutex;
    cluon::data::TimeStamp latestNtpTime;
    uint64_t latestRtpTime;
    double jitter = 700.0; // TODO: Calculate jitter (easy)
    uint32_t highestSeq = 0;

    auto onStreamData =
      [&od4, &recFileMutex, &recFile, &outData, &width, &height, &sdpData, &rtcpMutex,
      &latestNtpTime, &latestRtpTime, &highestSeq, &senderStamp, &verbose](
        std::string &&data, std::string &&,
        std::chrono::system_clock::time_point &&) noexcept {
      char const *buf_start = static_cast<char const *>(data.c_str());

      static uint8_t const nalPrefix[] = {0x00, 0x00, 0x00, 0x01};

      uint8_t const b0 = *buf_start;
     // uint8_t const version = (b0 >> 6);
      bool const hasPadding = (b0 & 0x20) >> 5;
     // bool const hasExtension = (b0 & 0x10) >> 4;
     // uint8_t const csrcCount = (b0 & 0xf);
      
      uint8_t const b1 = *(buf_start + 1);
     // bool const profileMarker = (b1 >> 7);
      uint8_t const payloadType = (b1 & 0x7f);

      if (payloadType != 96) {
        std::cout << "WARNING: Unknown format " << +payloadType << std::endl;
        return;
      }

      uint16_t b2b3;
      memcpy(&b2b3, buf_start + 2, 2);
      uint16_t const sequenceNumber = ntohs(b2b3);
      
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

      cluon::data::TimeStamp ts;
      {
        std::lock_guard<std::mutex> lock(rtcpMutex);
        uint64_t rtpTimeInMicroseconds = (timestamp - latestRtpTime)
          * (1000000UL / sdpData.clockrate[payloadType]);
        ts = cluon::time::fromMicroseconds(
            cluon::time::toMicroseconds(latestNtpTime)
            + rtpTimeInMicroseconds);
        highestSeq = sequenceNumber > highestSeq ? sequenceNumber : highestSeq;
      }

      if (h264RtpType >= 1 && h264RtpType <= 23) {
        nalType = h264RtpType;
        uint32_t nalLen = data.length() - 12 - paddingLen;
        outData = std::string(reinterpret_cast<const char*>(&nalPrefix[0]), 4) 
            + std::string(buf_start + 12, nalLen);

        if (verbose) {
          std::cout << "Received " << outData.size() << " bytes." << std::endl;
        }

        opendlv::proxy::ImageReading ir;
        ir.fourcc("h264").width(width).height(height).data(outData);

        std::lock_guard<std::mutex> lck(recFileMutex);
        if (recFile && recFile->good()) {
          cluon::data::Envelope envelope;
          {
            cluon::ToProtoVisitor protoEncoder;
            {
              envelope.dataType(ir.ID());
              ir.accept(protoEncoder);
              envelope.serializedData(protoEncoder.encodedData());
              envelope.sent(cluon::time::now());
              envelope.sampleTimeStamp(ts);
              envelope.senderStamp(senderStamp);
            }
          }

          std::string serializedData{cluon::serializeEnvelope(std::move(envelope))};
          recFile->write(serializedData.data(), serializedData.size());
          recFile->flush();
        }

        outData = "";

      } else if (h264RtpType == 28) {
        uint8_t b13 = *(buf_start + 13);
        bool isStartFragment = b13 >> 7;
        bool isEndFragment = (b13 & 0x40) >> 6;
        nalType = (b13 & 0x1f);

        std::string extra;
        if (isStartFragment) {
          uint8_t nalHeader = (h264RtpNri << 5) | nalType;
          extra = std::string(reinterpret_cast<const char*>(&nalPrefix[0]), 4) 
            + sdpData.sps[payloadType] 
            + std::string(reinterpret_cast<const char*>(&nalPrefix[0]), 4) 
            + sdpData.pps[payloadType]
            + std::string(reinterpret_cast<const char*>(&nalPrefix[0]), 4)
            + static_cast<char>(nalHeader);
        }

        uint32_t nalLen = data.length() - 14 - paddingLen;
        outData += extra + std::string(buf_start + 14, nalLen);

        if (isEndFragment) {
          if (verbose) {
            std::cout << "Received " << outData.size() << " bytes (defragmented)." << std::endl;
          }
          opendlv::proxy::ImageReading ir;
          ir.fourcc("h264").width(width).height(height).data(outData);
          std::lock_guard<std::mutex> lck(recFileMutex);
          if (recFile && recFile->good()) {
            cluon::data::Envelope envelope;
            {
              cluon::ToProtoVisitor protoEncoder;
              {
                envelope.dataType(ir.ID());
                ir.accept(protoEncoder);
                envelope.serializedData(protoEncoder.encodedData());
                envelope.sent(cluon::time::now());
                envelope.sampleTimeStamp(ts);
                envelope.senderStamp(senderStamp);
              }
            }

            std::string serializedData{cluon::serializeEnvelope(std::move(envelope))};
            recFile->write(serializedData.data(), serializedData.size());
            recFile->flush();
          }

          outData = "";
        }
      } else {
        std::cout << "WARNING: unknown RTP H264 payload type: " << h264RtpType
          << std::endl;
      }
    };
    
    auto onControlData = [&clientPortB, &serverPortB, &clientSsrc, &hostname, 
         &rtcpMutex, &latestNtpTime, &latestRtpTime, &jitter, &highestSeq,
         &verbose](
             std::string &&data, std::string &&, 
             std::chrono::system_clock::time_point &&dataInTs) noexcept {

      char const *buf_start = static_cast<char const *>(data.c_str());

    //  uint8_t const b0 = *buf_start;
    //  uint8_t const version = (b0 >> 6);
    //  bool const hasPadding = (b0 & 0x20) >> 5;
    //  uint8_t const receptionReportCount = (b0 & 0x1f);

      uint8_t const b1 = *(buf_start + 1);
      uint8_t const packetType = b1;
      
    //  uint16_t b2b3;
    //  memcpy(&b2b3, buf_start + 2, 2);
    //  uint8_t const length = ntohs(b2b3);

      uint32_t b4b5b6b7;
      memcpy(&b4b5b6b7, buf_start + 4, 4);
      uint32_t const ssrcId = ntohl(b4b5b6b7);

      if (packetType == 200) {

        uint32_t b8b9b10b11;
        memcpy(&b8b9b10b11, buf_start + 8, 4);
        uint32_t const ntpMsw = ntohl(b8b9b10b11);
      
        uint32_t b12b13b14b15;
        memcpy(&b12b13b14b15, buf_start + 12, 4);
        uint32_t const ntpLsw = ntohl(b12b13b14b15);
        
        uint32_t b10b11b12b13;
        memcpy(&b10b11b12b13, buf_start + 10, 4);
      
        uint64_t const sec = ntpMsw - 2208988800ULL;
        uint64_t const usec = (ntpLsw * 1000000UL) >> 32;

        cluon::data::TimeStamp ntpTime = cluon::time::fromMicroseconds(
            sec * 1000000UL + usec);
        
        uint32_t b16b17b18b19;
        memcpy(&b16b17b18b19, buf_start + 16, 4);
        uint32_t const rtpTime = ntohl(b16b17b18b19);

        uint32_t jitter_n;
        uint16_t highestSeq_n;
        {
          std::lock_guard<std::mutex> lock(rtcpMutex);
          latestNtpTime = ntpTime;
          latestRtpTime = rtpTime;
          jitter_n = htonl(static_cast<uint32_t>(jitter));
          highestSeq_n = htons(highestSeq);
        }


        // Send Receiver report.
        {
          int32_t fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

          struct sockaddr_in src;
          std::memset(&src, 0, sizeof(src));
          src.sin_family = AF_INET;
          src.sin_addr.s_addr = htonl(INADDR_ANY);
          src.sin_port = htons(clientPortB); 

          bind(fd, reinterpret_cast<struct sockaddr *>(&src), sizeof(src));
          
          struct sockaddr_in dst;
          std::memset(&dst, 0, sizeof(dst));
          dst.sin_family = AF_INET;
          dst.sin_addr.s_addr = inet_addr(hostname.c_str());
          dst.sin_port = htons(serverPortB);

          uint8_t data[48] = {};
          {
            data[0] = 0x81;
            data[1] = 0xc9;

            uint16_t length{7};
            uint16_t length_n = htons(length);
            memcpy(data + 2, &length_n, 2);

            uint32_t clientSsrc_n = htonl(clientSsrc);
            memcpy(data + 4, &clientSsrc_n, 4);
            
            memcpy(data + 8, &b4b5b6b7, 4);

            uint32_t fractionAndCumulativeLost{0};
            uint32_t fractionAndCumulativeLost_n 
              = htonl(fractionAndCumulativeLost);
            memcpy(data + 12, &fractionAndCumulativeLost_n, 4);
            
            uint16_t extendedHighestSeq{1};
            uint16_t extendedHighestSeq_n = htons(extendedHighestSeq);
            memcpy(data + 16, &extendedHighestSeq_n, 2);
            memcpy(data + 18, &highestSeq_n, 2);
            
            memcpy(data + 20, &jitter_n, 4);
            
            memcpy(data + 24, &b10b11b12b13, 4);

            double timeSinceSr = std::chrono::duration<double>(
                  std::chrono::high_resolution_clock::now() - dataInTs).count();
            uint32_t delaySinceLastSr = static_cast<uint32_t>(
                timeSinceSr / 65536);
            uint32_t delaySinceLastSr_n = htonl(delaySinceLastSr);
            memcpy(data + 28, &delaySinceLastSr_n, 4);
          }

          {
            data[32] = 0x81;
            data[33] = 0xca;

            uint16_t length{3};
            uint16_t length_n = htons(length);
            memcpy(data + 34, &length_n, 2);
            
            uint32_t clientSsrc_n = htonl(clientSsrc);
            memcpy(data + 36, &clientSsrc_n, 4);
            
            data[40] = 0x01;
            data[41] = 0x05;
            data[42] = 0x73;
            data[43] = 0x68;
            data[44] = 0x72;
            data[45] = 0x65;
            data[46] = 0x77;
            data[47] = 0x00;
          }

          sendto(fd, data, 48, 0, 
              reinterpret_cast<const struct sockaddr *>(&dst), sizeof(dst));

          shutdown(fd, SHUT_RDWR);
          close(fd);
        }
      }
    };

    {
      cluon::UDPReceiver streamUdpReceiver{localHostname, clientPortA, 
        onStreamData, serverPortA};
    
      cluon::UDPReceiver controlUdpReceiver{localHostname, clientPortB,
        onControlData, serverPortB};

      uint32_t const heartbeatInterval = 50;
      uint32_t h = 0;
      while (od4->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        if (h > heartbeatInterval) {
          curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, CURL_RTSPREQ_OPTIONS);
          curl_easy_perform(curl);
          h = 0;
        } else {
          h++;
        }
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


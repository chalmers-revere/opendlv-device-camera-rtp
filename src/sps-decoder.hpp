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

#ifndef SPS_DECODER_HPP
#define SPS_DECODER_HPP

struct SpsInfo {
  uint32_t width;
  uint32_t height;
  uint32_t fps;
};

namespace {

inline uint32_t extractUnsignedExpGolomb(uint8_t const *buf,
    uint32_t const wordLen, uint32_t &bitOffset)
{
  uint32_t zeroCount = 0;
  while (bitOffset < wordLen * 8) {
    if (buf[bitOffset / 8] & (0x80 >> (bitOffset % 8))) {
      break;
    }
    bitOffset++;
    zeroCount++;
  }
  bitOffset++;

  uint32_t ret = 0;
  for (uint32_t i = 0; i < zeroCount; i++) {
    ret <<= 1;
    if (buf[bitOffset / 8] & (0x80 >> (bitOffset % 8))) {
      ret += 1;
    }
    bitOffset++;
  }
  return (1 << zeroCount) - 1 + ret;
}


inline int32_t extractSignedExpGolomb(uint8_t const *buf,
    uint32_t const wordLen, uint32_t &bitOffset)
{
  int32_t v = extractUnsignedExpGolomb(buf, wordLen, bitOffset);
  int32_t ret = static_cast<int32_t>(ceil(static_cast<double>(v) / 2.0));
  if (v % 2 == 0) {
    ret = -ret;
  }
  return ret;
}


inline uint32_t extract(uint8_t const *buf, uint32_t const wordLen,
    uint32_t &bitOffset)
{
  uint32_t ret = 0;
  for (uint32_t i = 0; i < wordLen; ++i) {
    ret <<= 1;
    if (buf[bitOffset / 8] & (0x80 >> (bitOffset % 8))) {
      ret += 1;
    }
    bitOffset++;
  }
  return ret;
}

}

inline SpsInfo decodeSps(uint8_t const *buf, uint32_t const len)
{
  SpsInfo spsInfo;
  
  uint32_t bitOffset = 0;

  uint32_t forbiddenZeroBit = extract(buf, 1, bitOffset);
  uint32_t nalRefIdc = extract(buf, 2, bitOffset);
  uint32_t nalUnitType = extract(buf, 5, bitOffset);

  if(nalUnitType != 7) {
    return spsInfo;
  }

  uint32_t profileIdc = extract(buf, 8, bitOffset);

  uint32_t constraintSet0Flag = extract(buf, 1, bitOffset);
  uint32_t constraintSet1Flag = extract(buf, 1, bitOffset);
  uint32_t constraintSet2Flag = extract(buf, 1, bitOffset);
  uint32_t constraintSet3Flag = extract(buf, 1, bitOffset);
  
  uint32_t reservedBits = extract(buf, 4, bitOffset);
  uint32_t levelIdc = extract(buf, 8, bitOffset);

  uint32_t seqParameterSetId = extractUnsignedExpGolomb(buf, len, bitOffset);

  if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 
      || profileIdc == 144 ) {
    
    uint32_t chromaFormatIdc = extractUnsignedExpGolomb(buf, len, bitOffset);
    if (chromaFormatIdc == 3 ) {
      uint32_t residualColourTransformFlag = extract(buf, 1, bitOffset);
    }

    uint32_t bitDepthLumaMinus8 = extractUnsignedExpGolomb(buf, len,bitOffset);
    uint32_t bitDepthChromaMinus8 = extractUnsignedExpGolomb(buf, len,
        bitOffset);
    uint32_t qpprimeYZeroTransformBypassFlag = extract(buf, 1, bitOffset);
    uint32_t seqScalingMatrixPresentFlag = extract(buf, 1, bitOffset);

    uint32_t seqScalingListPresentFlag[8];
    if (seqScalingMatrixPresentFlag) {
      for (uint32_t i = 0; i < 8; ++i) {
        seqScalingListPresentFlag[i] = extract(buf, 1, bitOffset);
      }
    }
  }

  uint32_t log2MaxFrameNumMinus4 = extractUnsignedExpGolomb(buf, len,
      bitOffset);
  uint32_t picOrderCntType = extractUnsignedExpGolomb(buf, len, bitOffset);
  if (picOrderCntType == 0) {
    uint32_t log2MaxPicOrderCntLsbMinus4 = 
      extractUnsignedExpGolomb(buf, len, bitOffset);
  } else if (picOrderCntType == 1) {
    uint32_t deltaPicOrderAlwaysZeroFlag = extract(buf, 1, bitOffset);
    int32_t offsetForNonRefPic = extractSignedExpGolomb(buf, len, bitOffset);
    int32_t offsetForTopToBottomField = extractSignedExpGolomb(buf, len,
        bitOffset);
    uint32_t numRefFramesInPicOrderCntCycle = extractUnsignedExpGolomb(buf, len,
        bitOffset);

    int32_t *offsetForRefFrame = new int32_t[numRefFramesInPicOrderCntCycle];
    for (uint32_t i = 0; i < numRefFramesInPicOrderCntCycle; ++i) {
      offsetForRefFrame[i] = extractSignedExpGolomb(buf, len, bitOffset);
    }
    delete [] offsetForRefFrame;
  }
  uint32_t numRefFrames = extractUnsignedExpGolomb(buf, len, bitOffset);
  uint32_t gapsInFrameNumValueAllowedFlag = extract(buf, 1, bitOffset);
  uint32_t picWidthInMbsMinus1 = extractUnsignedExpGolomb(buf, len, bitOffset);
  uint32_t picHeightInMapUnitsMinus1 = 
    extractUnsignedExpGolomb(buf, len, bitOffset);

  spsInfo.width = (picWidthInMbsMinus1 + 1) * 16;
  spsInfo.height = (picHeightInMapUnitsMinus1 + 1) * 16;

  uint32_t frameMbsOnlyFlag = extract(buf, 1, bitOffset);
  if (!frameMbsOnlyFlag) {
    uint32_t mbAdaptiveFrameFieldFlag = extract(buf, 1, bitOffset);
  }

  uint32_t direct8x8InferenceFlag = extract(buf, 1, bitOffset);
  uint32_t frameCroppingFlag = extract(buf, 1, bitOffset);
  if (frameCroppingFlag) {
    uint32_t frameCropLeftOffset = extractUnsignedExpGolomb(buf, len, bitOffset);
    uint32_t frameCropRightOffset = extractUnsignedExpGolomb(buf, len, bitOffset);
    uint32_t frameCropTopOffset = extractUnsignedExpGolomb(buf, len, bitOffset);
    uint32_t frameCropBottomOffset = extractUnsignedExpGolomb(buf, len, bitOffset);
  }

  uint32_t vuiParameterPresentFlag = extract(buf, 1, bitOffset);
  if (vuiParameterPresentFlag) {
    uint32_t aspectRatioInfoPresentFlag = extract(buf, 1, bitOffset);
    if (aspectRatioInfoPresentFlag) {
      uint32_t aspectRatioIdc = extract(buf, 8, bitOffset);
      if (aspectRatioIdc == 255) {
        uint32_t sarWidth = extract(buf, 16, bitOffset);
        uint32_t sarHeight = extract(buf, 16, bitOffset);
      }
    }

    uint32_t overscanInfoPresentFlag = extract(buf, 1, bitOffset);
    if (overscanInfoPresentFlag) {
      uint32_t overscanAppropriateFlagu = extract(buf, 1, bitOffset);
    }

    uint32_t videoSignalTypePresentFlag = extract(buf, 1, bitOffset);
    if (videoSignalTypePresentFlag) {
      uint32_t videoFormat = extract(buf, 3, bitOffset);
      uint32_t videoFullRangeFlag = extract(buf, 1, bitOffset);

      uint32_t colourDescriptionPresentFlag = extract(buf, 1, bitOffset);
      if (colourDescriptionPresentFlag) {
        uint32_t colourPrimaries = extract(buf, 8, bitOffset);
        uint32_t transferCharacteristics = extract(buf, 8, bitOffset);
        uint32_t matrixCoefficients = extract(buf, 8, bitOffset);
      }
    }
    
    uint32_t chromaLocInfoPresentFlag = extract(buf, 1, bitOffset);
    if (chromaLocInfoPresentFlag) {
      uint32_t chromaSampleLocTypeTopField = 
        extractUnsignedExpGolomb(buf, len, bitOffset);
      uint32_t chromaSampleLocTypeBottomField = 
        extractUnsignedExpGolomb(buf, len, bitOffset);
    }

    uint32_t timingInfoPresentFlag = extract(buf, 1, bitOffset);
    if (timingInfoPresentFlag) {
      uint32_t numUnitsInTick = extract(buf, 32, bitOffset);
      uint32_t timeScale = extract(buf, 32, bitOffset);
      uint32_t fps = timeScale / numUnitsInTick;
      uint32_t fixedFrameRateFlag = extract(buf, 1, bitOffset);
      if (fixedFrameRateFlag) {
        fps = fps / 2;
      }
      spsInfo.fps = fps;
    }
  }

  return spsInfo;
}

#endif

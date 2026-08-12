#include "ChipIntelliAudio.h"

#include <ArduinoEvent.h>

extern "C" {
#include "FreeRTOS.h"
#include "audio_play_api.h"
#include "ci_flash_data_info.h"
#include "prompt_player.h"
#include "queue.h"
#include "system_msg_deal.h"
#include "task.h"

uint32_t ci_arduino_audio_started_message_count(void);
uint32_t ci_arduino_prompt_play_voice_sequence(
    const uint16_t *voiceIds, uint8_t count,
    play_done_callback_t playDoneCallback, bool preemptive);
}

struct ChipIntelliAudioClass::PlaybackRequest {
  enum class Kind : uint8_t {
    VoiceSequence,
    Beep,
    CommandId,
    SemanticId,
    CommandText,
    Stop,
  };

  static constexpr size_t kCommandTextCapacity = 96U;

  Kind kind;
  bool interruptCurrent;
  int optionIndex;
  uint32_t value;
  uint8_t count;
  uint16_t voiceIds[kMaxVoiceSequenceLength];
  char commandText[kCommandTextCapacity];
};

namespace {
constexpr uint32_t kInitTimeoutMs = 10000;
constexpr uint32_t kIdleStabilityMs = 500;
constexpr UBaseType_t kPlaybackTaskPriority = 3U;
constexpr uint16_t kPlaybackTaskStackDepth = 768U;
constexpr UBaseType_t kPlaybackQueueLength = 8U;
constexpr UBaseType_t kInterruptQueueLength = 4U;
constexpr uint32_t kPowersOfTen[] = {
    1U,          10U,        100U,       1000U,      10000U,
    100000U,     1000000U,   10000000U,  100000000U, 1000000000U,
};
constexpr ChipIntelliAudioClass::NumberVoiceIds kDefaultNumberVoiceIds = {
    {300U, 301U, 302U, 303U, 304U, 305U, 306U, 307U, 308U, 309U},
    310U,  // 十
    311U,  // 百
    312U,  // 千
    313U,  // 万
    314U,  // 亿
    315U,  // 负
    316U,  // 点
};

// Every language owns its selected voice.bin and reuses IDs beginning at 300.
// Digits deliberately have the same IDs in every language so decimal digits
// can be appended without a language-specific lookup table.
constexpr uint16_t kDigitVoiceBase = 300U;

constexpr uint16_t kEnglishTen = 310U;
constexpr uint16_t kEnglishTwenty = 320U;
constexpr uint16_t kEnglishHundred = 328U;
constexpr uint16_t kEnglishThousand = 329U;
constexpr uint16_t kEnglishMillion = 330U;
constexpr uint16_t kEnglishBillion = 331U;
constexpr uint16_t kEnglishNegative = 332U;
constexpr uint16_t kEnglishDecimalPoint = 333U;

constexpr uint16_t kJapaneseTen = 310U;
constexpr uint16_t kJapaneseHundred = 319U;
constexpr uint16_t kJapaneseThousand = 328U;
constexpr uint16_t kJapaneseTenThousand = 337U;
constexpr uint16_t kJapaneseHundredMillion = 338U;
constexpr uint16_t kJapaneseNegative = 339U;
constexpr uint16_t kJapaneseDecimalPoint = 340U;

constexpr uint16_t kKoreanTen = 310U;
constexpr uint16_t kKoreanHundred = 311U;
constexpr uint16_t kKoreanThousand = 312U;
constexpr uint16_t kKoreanTenThousand = 313U;
constexpr uint16_t kKoreanHundredMillion = 314U;
constexpr uint16_t kKoreanNegative = 315U;
constexpr uint16_t kKoreanDecimalPoint = 316U;

constexpr uint16_t kRussianTen = 310U;
constexpr uint16_t kRussianTwenty = 320U;
constexpr uint16_t kRussianHundred = 328U;
constexpr uint16_t kRussianFeminineOne = 337U;
constexpr uint16_t kRussianFeminineTwo = 338U;
constexpr uint16_t kRussianThousand = 339U;
constexpr uint16_t kRussianMillion = 342U;
constexpr uint16_t kRussianBillion = 345U;
constexpr uint16_t kRussianNegative = 348U;
constexpr uint16_t kRussianDecimalPoint = 349U;

constexpr uint16_t kSpanishTen = 310U;
constexpr uint16_t kSpanishThirty = 330U;
constexpr uint16_t kSpanishAnd = 337U;
constexpr uint16_t kSpanishUn = 338U;
constexpr uint16_t kSpanishTwentyOneApocopated = 339U;
constexpr uint16_t kSpanishCien = 340U;
constexpr uint16_t kSpanishCiento = 341U;
constexpr uint16_t kSpanishTwoHundred = 342U;
constexpr uint16_t kSpanishThousand = 350U;
constexpr uint16_t kSpanishMillion = 351U;
constexpr uint16_t kSpanishMillions = 352U;
constexpr uint16_t kSpanishNegative = 353U;
constexpr uint16_t kSpanishDecimalPoint = 354U;

constexpr uint16_t kThaiFinalOne = 310U;
constexpr uint16_t kThaiTwentyPrefix = 311U;
constexpr uint16_t kThaiTen = 312U;
constexpr uint16_t kThaiHundred = 313U;
constexpr uint16_t kThaiThousand = 314U;
constexpr uint16_t kThaiTenThousand = 315U;
constexpr uint16_t kThaiHundredThousand = 316U;
constexpr uint16_t kThaiMillion = 317U;
constexpr uint16_t kThaiNegative = 318U;
constexpr uint16_t kThaiDecimalPoint = 319U;

constexpr uint16_t kGermanEin = 310U;
constexpr uint16_t kGermanEine = 311U;
constexpr uint16_t kGermanTen = 312U;
constexpr uint16_t kGermanTwenty = 322U;
constexpr uint16_t kGermanAnd = 330U;
constexpr uint16_t kGermanHundred = 331U;
constexpr uint16_t kGermanThousand = 332U;
constexpr uint16_t kGermanMillion = 333U;
constexpr uint16_t kGermanMillions = 334U;
constexpr uint16_t kGermanBillion = 335U;
constexpr uint16_t kGermanBillions = 336U;
constexpr uint16_t kGermanNegative = 337U;
constexpr uint16_t kGermanDecimalPoint = 338U;

constexpr uint16_t kIndonesianTen = 310U;
constexpr uint16_t kIndonesianEleven = 311U;
constexpr uint16_t kIndonesianTeenSuffix = 312U;
constexpr uint16_t kIndonesianTensSuffix = 313U;
constexpr uint16_t kIndonesianOneHundred = 314U;
constexpr uint16_t kIndonesianHundred = 315U;
constexpr uint16_t kIndonesianOneThousand = 316U;
constexpr uint16_t kIndonesianThousand = 317U;
constexpr uint16_t kIndonesianMillion = 318U;
constexpr uint16_t kIndonesianBillion = 319U;
constexpr uint16_t kIndonesianNegative = 320U;
constexpr uint16_t kIndonesianDecimalPoint = 321U;

constexpr uint16_t kVietnameseFinalOne = 310U;
constexpr uint16_t kVietnameseFinalFour = 311U;
constexpr uint16_t kVietnameseFinalFive = 312U;
constexpr uint16_t kVietnameseTen = 313U;
constexpr uint16_t kVietnameseTensSuffix = 314U;
constexpr uint16_t kVietnameseHundred = 315U;
constexpr uint16_t kVietnameseZeroTens = 316U;
constexpr uint16_t kVietnameseThousand = 317U;
constexpr uint16_t kVietnameseMillion = 318U;
constexpr uint16_t kVietnameseBillion = 319U;
constexpr uint16_t kVietnameseNegative = 320U;
constexpr uint16_t kVietnameseDecimalPoint = 321U;

constexpr uint16_t kFrenchTen = 310U;
constexpr uint16_t kFrenchTwenty = 320U;
constexpr uint16_t kFrenchSixty = 324U;
constexpr uint16_t kFrenchEighty = 325U;
constexpr uint16_t kFrenchEightyCompound = 326U;
constexpr uint16_t kFrenchAnd = 327U;
constexpr uint16_t kFrenchHundred = 328U;
constexpr uint16_t kFrenchThousand = 329U;
constexpr uint16_t kFrenchMillion = 330U;
constexpr uint16_t kFrenchMillions = 331U;
constexpr uint16_t kFrenchBillion = 332U;
constexpr uint16_t kFrenchBillions = 333U;
constexpr uint16_t kFrenchNegative = 334U;
constexpr uint16_t kFrenchDecimalPoint = 335U;

constexpr uint16_t kPortugueseTen = 310U;
constexpr uint16_t kPortugueseTwenty = 320U;
constexpr uint16_t kPortugueseAnd = 328U;
constexpr uint16_t kPortugueseOneHundred = 329U;
constexpr uint16_t kPortugueseHundredAnd = 330U;
constexpr uint16_t kPortugueseTwoHundred = 331U;
constexpr uint16_t kPortugueseThousand = 339U;
constexpr uint16_t kPortugueseMillion = 340U;
constexpr uint16_t kPortugueseMillions = 341U;
constexpr uint16_t kPortugueseBillion = 342U;
constexpr uint16_t kPortugueseBillions = 343U;
constexpr uint16_t kPortugueseNegative = 344U;
constexpr uint16_t kPortugueseDecimalPoint = 345U;

constexpr uint16_t kPersianTen = 310U;
constexpr uint16_t kPersianTwenty = 320U;
constexpr uint16_t kPersianHundred = 328U;
constexpr uint16_t kPersianAnd = 337U;
constexpr uint16_t kPersianThousand = 338U;
constexpr uint16_t kPersianMillion = 339U;
constexpr uint16_t kPersianBillion = 340U;
constexpr uint16_t kPersianNegative = 341U;
constexpr uint16_t kPersianDecimalPoint = 342U;

constexpr uint16_t kTurkishTen = 310U;
constexpr uint16_t kTurkishHundred = 319U;
constexpr uint16_t kTurkishThousand = 320U;
constexpr uint16_t kTurkishMillion = 321U;
constexpr uint16_t kTurkishBillion = 322U;
constexpr uint16_t kTurkishNegative = 323U;
constexpr uint16_t kTurkishDecimalPoint = 324U;

constexpr uint16_t kArabicTen = 310U;
constexpr uint16_t kArabicTwenty = 320U;
constexpr uint16_t kArabicAnd = 328U;
constexpr uint16_t kArabicHundred = 329U;
constexpr uint16_t kArabicThousand = 338U;
constexpr uint16_t kArabicTwoThousand = 339U;
constexpr uint16_t kArabicThousands = 340U;
constexpr uint16_t kArabicMillion = 341U;
constexpr uint16_t kArabicTwoMillion = 342U;
constexpr uint16_t kArabicMillions = 343U;
constexpr uint16_t kArabicBillion = 344U;
constexpr uint16_t kArabicTwoBillion = 345U;
constexpr uint16_t kArabicBillions = 346U;
constexpr uint16_t kArabicNegative = 347U;
constexpr uint16_t kArabicDecimalPoint = 348U;

bool isAsciiWhitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\f' || value == '\v';
}

class VoiceSequenceBuilder {
 public:
  VoiceSequenceBuilder(uint16_t *output, size_t capacity)
      : _output(output), _capacity(capacity), _count(0U), _valid(true) {}

  bool append(uint16_t voiceId) {
    if (!_valid || _output == nullptr || _count >= _capacity) {
      _valid = false;
      return false;
    }
    _output[_count++] = voiceId;
    return true;
  }

  size_t size() const { return _valid ? _count : 0U; }

 private:
  uint16_t *_output;
  size_t _capacity;
  size_t _count;
  bool _valid;
};

void appendNumberGroup(uint16_t group, bool omitLeadingOne,
                       const ChipIntelliAudioClass::NumberVoiceIds &voiceIds,
                       VoiceSequenceBuilder &output) {
  const uint16_t placeValues[] = {
      0U, voiceIds.ten, voiceIds.hundred, voiceIds.thousand,
  };
  const uint16_t divisors[] = {1000U, 100U, 10U, 1U};
  bool emittedDigit = false;
  bool pendingZero = false;

  for (size_t index = 0U; index < 4U; ++index) {
    const uint16_t divisor = divisors[index];
    const uint8_t digit = static_cast<uint8_t>((group / divisor) % 10U);
    const uint8_t position = static_cast<uint8_t>(3U - index);

    if (digit == 0U) {
      if (emittedDigit) {
        pendingZero = true;
      }
      continue;
    }

    if (pendingZero) {
      output.append(voiceIds.digits[0]);
      pendingZero = false;
    }

    const bool omitDigit = omitLeadingOne && !emittedDigit &&
                           position == 1U && digit == 1U;
    if (!omitDigit) {
      output.append(voiceIds.digits[digit]);
    }
    if (position != 0U) {
      output.append(placeValues[position]);
    }
    emittedDigit = true;
  }
}

void appendUnsignedNumber(
    uint32_t value,
    const ChipIntelliAudioClass::NumberVoiceIds &voiceIds,
    VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(voiceIds.digits[0]);
    return;
  }

  const uint16_t groups[] = {
      static_cast<uint16_t>(value % 10000U),
      static_cast<uint16_t>((value / 10000U) % 10000U),
      static_cast<uint16_t>(value / 100000000U),
  };
  bool emittedGroup = false;
  bool pendingZero = false;

  for (int index = 2; index >= 0; --index) {
    const uint16_t group = groups[index];
    if (group == 0U) {
      if (emittedGroup) {
        pendingZero = true;
      }
      continue;
    }

    const bool highestGroup = !emittedGroup;
    if (emittedGroup && (pendingZero || group < 1000U)) {
      output.append(voiceIds.digits[0]);
    }

    appendNumberGroup(group, highestGroup, voiceIds, output);
    if (index == 2) {
      output.append(voiceIds.hundredMillion);
    } else if (index == 1) {
      output.append(voiceIds.tenThousand);
    }
    emittedGroup = true;
    pendingZero = false;
  }
}

void appendEnglishBelowHundred(uint8_t value,
                               VoiceSequenceBuilder &output) {
  if (value < 10U) {
    output.append(kDigitVoiceBase + value);
  } else if (value < 20U) {
    output.append(kEnglishTen + value - 10U);
  } else {
    output.append(kEnglishTwenty + value / 10U - 2U);
    if ((value % 10U) != 0U) {
      output.append(kDigitVoiceBase + value % 10U);
    }
  }
}

void appendEnglishGroup(uint16_t value, VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    output.append(kDigitVoiceBase + value / 100U);
    output.append(kEnglishHundred);
    value %= 100U;
  }
  if (value != 0U) {
    appendEnglishBelowHundred(static_cast<uint8_t>(value), output);
  }
}

void appendEnglishNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }

  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 1000000000U),
      static_cast<uint16_t>((value / 1000000U) % 1000U),
      static_cast<uint16_t>((value / 1000U) % 1000U),
      static_cast<uint16_t>(value % 1000U),
  };
  const uint16_t scales[] = {
      kEnglishBillion, kEnglishMillion, kEnglishThousand, 0U,
  };
  for (size_t index = 0U; index < 4U; ++index) {
    if (groups[index] == 0U) {
      continue;
    }
    appendEnglishGroup(groups[index], output);
    if (scales[index] != 0U) {
      output.append(scales[index]);
    }
  }
}

void appendJapaneseGroup(uint16_t value, VoiceSequenceBuilder &output) {
  const uint16_t divisors[] = {1000U, 100U, 10U};
  const uint16_t firstVoiceIds[] = {
      kJapaneseThousand, kJapaneseHundred, kJapaneseTen,
  };
  for (size_t index = 0U; index < 3U; ++index) {
    const uint8_t digit = static_cast<uint8_t>((value / divisors[index]) % 10U);
    if (digit != 0U) {
      // Complete multiples such as 三百 and 八千 preserve Japanese sound
      // changes that cannot be produced by joining isolated digit/unit clips.
      output.append(firstVoiceIds[index] + digit - 1U);
    }
  }
  const uint8_t ones = static_cast<uint8_t>(value % 10U);
  if (ones != 0U) {
    output.append(kDigitVoiceBase + ones);
  }
}

void appendJapaneseNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }

  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 100000000U),
      static_cast<uint16_t>((value / 10000U) % 10000U),
      static_cast<uint16_t>(value % 10000U),
  };
  const uint16_t scales[] = {
      kJapaneseHundredMillion, kJapaneseTenThousand, 0U,
  };
  for (size_t index = 0U; index < 3U; ++index) {
    if (groups[index] == 0U) {
      continue;
    }
    appendJapaneseGroup(groups[index], output);
    if (scales[index] != 0U) {
      output.append(scales[index]);
    }
  }
}

void appendKoreanGroup(uint16_t value, VoiceSequenceBuilder &output) {
  const uint16_t divisors[] = {1000U, 100U, 10U, 1U};
  const uint16_t placeVoiceIds[] = {
      kKoreanThousand, kKoreanHundred, kKoreanTen, 0U,
  };
  for (size_t index = 0U; index < 4U; ++index) {
    const uint8_t digit = static_cast<uint8_t>((value / divisors[index]) % 10U);
    if (digit == 0U) {
      continue;
    }
    const bool omitOne = digit == 1U && placeVoiceIds[index] != 0U;
    if (!omitOne) {
      output.append(kDigitVoiceBase + digit);
    }
    if (placeVoiceIds[index] != 0U) {
      output.append(placeVoiceIds[index]);
    }
  }
}

void appendKoreanNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }

  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 100000000U),
      static_cast<uint16_t>((value / 10000U) % 10000U),
      static_cast<uint16_t>(value % 10000U),
  };
  const uint16_t scales[] = {
      kKoreanHundredMillion, kKoreanTenThousand, 0U,
  };
  for (size_t index = 0U; index < 3U; ++index) {
    if (groups[index] == 0U) {
      continue;
    }
    // Korean normally says 만 for exactly 10,000, but 일억 for 100,000,000.
    if (!(index == 1U && groups[index] == 1U)) {
      appendKoreanGroup(groups[index], output);
    }
    if (scales[index] != 0U) {
      output.append(scales[index]);
    }
  }
}

void appendGermanBelowHundred(uint8_t value,
                              VoiceSequenceBuilder &output) {
  if (value < 10U) {
    output.append(kDigitVoiceBase + value);
  } else if (value < 20U) {
    output.append(kGermanTen + value - 10U);
  } else {
    const uint8_t ones = value % 10U;
    if (ones != 0U) {
      output.append(ones == 1U ? kGermanEin : kDigitVoiceBase + ones);
      output.append(kGermanAnd);
    }
    output.append(kGermanTwenty + value / 10U - 2U);
  }
}

void appendGermanBelowThousand(uint16_t value,
                               VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    const uint8_t hundreds = static_cast<uint8_t>(value / 100U);
    output.append(hundreds == 1U ? kGermanEin
                                 : kDigitVoiceBase + hundreds);
    output.append(kGermanHundred);
    value %= 100U;
  }
  if (value != 0U) {
    appendGermanBelowHundred(static_cast<uint8_t>(value), output);
  }
}

void appendGermanBelowMillion(uint32_t value,
                              VoiceSequenceBuilder &output) {
  const uint16_t thousands = static_cast<uint16_t>(value / 1000U);
  if (thousands != 0U) {
    if (thousands == 1U) {
      output.append(kGermanEin);
    } else {
      appendGermanBelowThousand(thousands, output);
    }
    output.append(kGermanThousand);
  }
  const uint16_t remainder = static_cast<uint16_t>(value % 1000U);
  if (remainder != 0U) {
    appendGermanBelowThousand(remainder, output);
  }
}

void appendGermanNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }

  const uint8_t billions = static_cast<uint8_t>(value / 1000000000U);
  if (billions != 0U) {
    if (billions == 1U) {
      output.append(kGermanEine);
      output.append(kGermanBillion);
    } else {
      output.append(kDigitVoiceBase + billions);
      output.append(kGermanBillions);
    }
  }

  const uint16_t millions =
      static_cast<uint16_t>((value / 1000000U) % 1000U);
  if (millions != 0U) {
    if (millions == 1U) {
      output.append(kGermanEine);
      output.append(kGermanMillion);
    } else {
      appendGermanBelowThousand(millions, output);
      output.append(kGermanMillions);
    }
  }

  const uint32_t remainder = value % 1000000U;
  if (remainder != 0U) {
    appendGermanBelowMillion(remainder, output);
  }
}

void appendRussianGroup(uint16_t value, bool feminine,
                        VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    output.append(kRussianHundred + value / 100U - 1U);
    value %= 100U;
  }
  if (value >= 20U) {
    output.append(kRussianTwenty + value / 10U - 2U);
    value %= 10U;
  } else if (value >= 10U) {
    output.append(kRussianTen + value - 10U);
    return;
  }
  if (value != 0U) {
    if (feminine && value == 1U) {
      output.append(kRussianFeminineOne);
    } else if (feminine && value == 2U) {
      output.append(kRussianFeminineTwo);
    } else {
      output.append(kDigitVoiceBase + value);
    }
  }
}

uint16_t russianScaleVoice(uint16_t value, uint16_t singularVoice) {
  const uint8_t lastTwo = static_cast<uint8_t>(value % 100U);
  if (lastTwo >= 11U && lastTwo <= 14U) {
    return singularVoice + 2U;
  }
  const uint8_t last = static_cast<uint8_t>(value % 10U);
  if (last == 1U) {
    return singularVoice;
  }
  if (last >= 2U && last <= 4U) {
    return singularVoice + 1U;
  }
  return singularVoice + 2U;
}

void appendRussianNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 1000000000U),
      static_cast<uint16_t>((value / 1000000U) % 1000U),
      static_cast<uint16_t>((value / 1000U) % 1000U),
      static_cast<uint16_t>(value % 1000U),
  };
  const uint16_t scaleVoices[] = {
      kRussianBillion, kRussianMillion, kRussianThousand, 0U,
  };
  for (size_t index = 0U; index < 4U; ++index) {
    if (groups[index] == 0U) {
      continue;
    }
    appendRussianGroup(groups[index], index == 2U, output);
    if (scaleVoices[index] != 0U) {
      output.append(russianScaleVoice(groups[index], scaleVoices[index]));
    }
  }
}

void appendSpanishBelowHundred(uint8_t value, bool apocopated,
                               VoiceSequenceBuilder &output) {
  if (value < 30U) {
    if (apocopated && value == 1U) {
      output.append(kSpanishUn);
    } else if (apocopated && value == 21U) {
      output.append(kSpanishTwentyOneApocopated);
    } else {
      output.append(value < 10U ? kDigitVoiceBase + value
                               : kSpanishTen + value - 10U);
    }
    return;
  }

  output.append(kSpanishThirty + value / 10U - 3U);
  const uint8_t ones = value % 10U;
  if (ones != 0U) {
    output.append(kSpanishAnd);
    output.append(apocopated && ones == 1U ? kSpanishUn
                                           : kDigitVoiceBase + ones);
  }
}

void appendSpanishGroup(uint16_t value, bool apocopated,
                        VoiceSequenceBuilder &output) {
  if (value == 100U) {
    output.append(kSpanishCien);
    return;
  }
  if (value > 100U) {
    const uint8_t hundreds = static_cast<uint8_t>(value / 100U);
    output.append(hundreds == 1U ? kSpanishCiento
                                 : kSpanishTwoHundred + hundreds - 2U);
    value %= 100U;
  }
  if (value != 0U) {
    appendSpanishBelowHundred(static_cast<uint8_t>(value), apocopated,
                              output);
  }
}

void appendSpanishNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }

  const uint8_t billions = static_cast<uint8_t>(value / 1000000000U);
  if (billions != 0U) {
    if (billions != 1U) {
      appendSpanishGroup(billions, true, output);
    }
    output.append(kSpanishThousand);
    output.append(kSpanishMillions);
  }

  const uint16_t millions =
      static_cast<uint16_t>((value / 1000000U) % 1000U);
  if (millions != 0U) {
    if (millions == 1U) {
      output.append(kSpanishUn);
      output.append(kSpanishMillion);
    } else {
      appendSpanishGroup(millions, true, output);
      output.append(kSpanishMillions);
    }
  }

  const uint16_t thousands =
      static_cast<uint16_t>((value / 1000U) % 1000U);
  if (thousands != 0U) {
    if (thousands != 1U) {
      appendSpanishGroup(thousands, true, output);
    }
    output.append(kSpanishThousand);
  }

  const uint16_t remainder = static_cast<uint16_t>(value % 1000U);
  if (remainder != 0U) {
    appendSpanishGroup(remainder, false, output);
  }
}

void appendThaiGroup(uint32_t value, bool hasHigherGroup,
                     VoiceSequenceBuilder &output) {
  const uint32_t divisors[] = {100000U, 10000U, 1000U, 100U};
  const uint16_t units[] = {
      kThaiHundredThousand, kThaiTenThousand, kThaiThousand, kThaiHundred,
  };
  bool emitted = hasHigherGroup;
  for (size_t index = 0U; index < 4U; ++index) {
    const uint8_t digit =
        static_cast<uint8_t>((value / divisors[index]) % 10U);
    if (digit != 0U) {
      output.append(kDigitVoiceBase + digit);
      output.append(units[index]);
      emitted = true;
    }
  }

  const uint8_t tens = static_cast<uint8_t>((value / 10U) % 10U);
  if (tens != 0U) {
    if (tens == 2U) {
      output.append(kThaiTwentyPrefix);
    } else if (tens != 1U) {
      output.append(kDigitVoiceBase + tens);
    }
    output.append(kThaiTen);
    emitted = true;
  }

  const uint8_t ones = static_cast<uint8_t>(value % 10U);
  if (ones != 0U) {
    output.append(ones == 1U && emitted ? kThaiFinalOne
                                        : kDigitVoiceBase + ones);
  }
}

void appendThaiNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint32_t millions = value / 1000000U;
  if (millions != 0U) {
    appendThaiGroup(millions, false, output);
    output.append(kThaiMillion);
  }
  const uint32_t remainder = value % 1000000U;
  if (remainder != 0U) {
    appendThaiGroup(remainder, millions != 0U, output);
  }
}

void appendIndonesianBelowHundred(uint8_t value,
                                  VoiceSequenceBuilder &output) {
  if (value < 10U) {
    output.append(kDigitVoiceBase + value);
  } else if (value == 10U) {
    output.append(kIndonesianTen);
  } else if (value == 11U) {
    output.append(kIndonesianEleven);
  } else if (value < 20U) {
    output.append(kDigitVoiceBase + value - 10U);
    output.append(kIndonesianTeenSuffix);
  } else {
    output.append(kDigitVoiceBase + value / 10U);
    output.append(kIndonesianTensSuffix);
    if ((value % 10U) != 0U) {
      output.append(kDigitVoiceBase + value % 10U);
    }
  }
}

void appendIndonesianGroup(uint16_t value, VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    const uint8_t hundreds = static_cast<uint8_t>(value / 100U);
    if (hundreds == 1U) {
      output.append(kIndonesianOneHundred);
    } else {
      output.append(kDigitVoiceBase + hundreds);
      output.append(kIndonesianHundred);
    }
    value %= 100U;
  }
  if (value != 0U) {
    appendIndonesianBelowHundred(static_cast<uint8_t>(value), output);
  }
}

void appendIndonesianBelowMillion(uint32_t value,
                                  VoiceSequenceBuilder &output) {
  const uint16_t thousands = static_cast<uint16_t>(value / 1000U);
  if (thousands != 0U) {
    if (thousands == 1U) {
      output.append(kIndonesianOneThousand);
    } else {
      appendIndonesianGroup(thousands, output);
      output.append(kIndonesianThousand);
    }
  }
  const uint16_t remainder = static_cast<uint16_t>(value % 1000U);
  if (remainder != 0U) {
    appendIndonesianGroup(remainder, output);
  }
}

void appendIndonesianNumber(uint32_t value,
                            VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint8_t billions = static_cast<uint8_t>(value / 1000000000U);
  if (billions != 0U) {
    appendIndonesianGroup(billions, output);
    output.append(kIndonesianBillion);
  }
  const uint16_t millions =
      static_cast<uint16_t>((value / 1000000U) % 1000U);
  if (millions != 0U) {
    appendIndonesianGroup(millions, output);
    output.append(kIndonesianMillion);
  }
  const uint32_t remainder = value % 1000000U;
  if (remainder != 0U) {
    appendIndonesianBelowMillion(remainder, output);
  }
}

void appendVietnameseBelowHundred(uint8_t value,
                                  VoiceSequenceBuilder &output) {
  if (value < 10U) {
    output.append(kDigitVoiceBase + value);
    return;
  }
  const uint8_t tens = value / 10U;
  const uint8_t ones = value % 10U;
  if (tens == 1U) {
    output.append(kVietnameseTen);
  } else {
    output.append(kDigitVoiceBase + tens);
    output.append(kVietnameseTensSuffix);
  }
  if (ones == 1U && tens > 1U) {
    output.append(kVietnameseFinalOne);
  } else if (ones == 4U && tens > 1U) {
    output.append(kVietnameseFinalFour);
  } else if (ones == 5U) {
    output.append(kVietnameseFinalFive);
  } else if (ones != 0U) {
    output.append(kDigitVoiceBase + ones);
  }
}

void appendVietnameseGroup(uint16_t value, bool forceHundreds,
                           VoiceSequenceBuilder &output) {
  if (value >= 100U || forceHundreds) {
    output.append(kDigitVoiceBase + value / 100U);
    output.append(kVietnameseHundred);
    value %= 100U;
    if (value != 0U && value < 10U) {
      output.append(kVietnameseZeroTens);
    }
  }
  if (value != 0U) {
    appendVietnameseBelowHundred(static_cast<uint8_t>(value), output);
  }
}

void appendVietnameseNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 1000000000U),
      static_cast<uint16_t>((value / 1000000U) % 1000U),
      static_cast<uint16_t>((value / 1000U) % 1000U),
      static_cast<uint16_t>(value % 1000U),
  };
  const uint16_t scales[] = {
      kVietnameseBillion, kVietnameseMillion, kVietnameseThousand, 0U,
  };
  bool emitted = false;
  for (size_t index = 0U; index < 4U; ++index) {
    if (groups[index] == 0U) {
      continue;
    }
    appendVietnameseGroup(groups[index], emitted && groups[index] < 100U,
                          output);
    if (scales[index] != 0U) {
      output.append(scales[index]);
    }
    emitted = true;
  }
}

void appendFrenchBelowHundred(uint8_t value,
                              VoiceSequenceBuilder &output) {
  if (value < 20U) {
    output.append(value < 10U ? kDigitVoiceBase + value
                             : kFrenchTen + value - 10U);
    return;
  }
  if (value < 70U) {
    output.append(kFrenchTwenty + value / 10U - 2U);
    const uint8_t ones = value % 10U;
    if (ones == 1U) {
      output.append(kFrenchAnd);
    }
    if (ones != 0U) {
      output.append(kDigitVoiceBase + ones);
    }
    return;
  }
  if (value < 80U) {
    output.append(kFrenchSixty);
    const uint8_t remainder = value - 60U;
    if (remainder == 11U) {
      output.append(kFrenchAnd);
    }
    output.append(kFrenchTen + remainder - 10U);
    return;
  }
  if (value == 80U) {
    output.append(kFrenchEighty);
    return;
  }
  output.append(kFrenchEightyCompound);
  const uint8_t remainder = value - 80U;
  output.append(remainder < 10U ? kDigitVoiceBase + remainder
                                : kFrenchTen + remainder - 10U);
}

void appendFrenchGroup(uint16_t value, VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    const uint8_t hundreds = static_cast<uint8_t>(value / 100U);
    if (hundreds != 1U) {
      output.append(kDigitVoiceBase + hundreds);
    }
    output.append(kFrenchHundred);
    value %= 100U;
  }
  if (value != 0U) {
    appendFrenchBelowHundred(static_cast<uint8_t>(value), output);
  }
}

void appendFrenchNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint8_t billions = static_cast<uint8_t>(value / 1000000000U);
  if (billions != 0U) {
    appendFrenchGroup(billions, output);
    output.append(billions == 1U ? kFrenchBillion : kFrenchBillions);
  }
  const uint16_t millions =
      static_cast<uint16_t>((value / 1000000U) % 1000U);
  if (millions != 0U) {
    appendFrenchGroup(millions, output);
    output.append(millions == 1U ? kFrenchMillion : kFrenchMillions);
  }
  const uint16_t thousands =
      static_cast<uint16_t>((value / 1000U) % 1000U);
  if (thousands != 0U) {
    if (thousands != 1U) {
      appendFrenchGroup(thousands, output);
    }
    output.append(kFrenchThousand);
  }
  const uint16_t remainder = static_cast<uint16_t>(value % 1000U);
  if (remainder != 0U) {
    appendFrenchGroup(remainder, output);
  }
}

void appendPortugueseBelowHundred(uint8_t value,
                                  VoiceSequenceBuilder &output) {
  if (value < 20U) {
    output.append(value < 10U ? kDigitVoiceBase + value
                             : kPortugueseTen + value - 10U);
    return;
  }
  output.append(kPortugueseTwenty + value / 10U - 2U);
  const uint8_t ones = value % 10U;
  if (ones != 0U) {
    output.append(kPortugueseAnd);
    output.append(kDigitVoiceBase + ones);
  }
}

void appendPortugueseGroup(uint16_t value, VoiceSequenceBuilder &output) {
  if (value == 100U) {
    output.append(kPortugueseOneHundred);
    return;
  }
  if (value > 100U) {
    const uint8_t hundreds = static_cast<uint8_t>(value / 100U);
    output.append(hundreds == 1U
                      ? kPortugueseHundredAnd
                      : kPortugueseTwoHundred + hundreds - 2U);
    value %= 100U;
    if (value != 0U) {
      output.append(kPortugueseAnd);
    }
  }
  if (value != 0U) {
    appendPortugueseBelowHundred(static_cast<uint8_t>(value), output);
  }
}

bool portugueseScaleNeedsAnd(uint32_t remainder) {
  return remainder != 0U &&
         (remainder < 100U || (remainder % 100U) == 0U);
}

void appendPortugueseNumber(uint32_t value,
                            VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint8_t billions = static_cast<uint8_t>(value / 1000000000U);
  if (billions != 0U) {
    appendPortugueseGroup(billions, output);
    output.append(billions == 1U ? kPortugueseBillion
                                 : kPortugueseBillions);
    if (portugueseScaleNeedsAnd(value % 1000000000U)) {
      output.append(kPortugueseAnd);
    }
  }
  const uint16_t millions =
      static_cast<uint16_t>((value / 1000000U) % 1000U);
  if (millions != 0U) {
    appendPortugueseGroup(millions, output);
    output.append(millions == 1U ? kPortugueseMillion
                                 : kPortugueseMillions);
    if (portugueseScaleNeedsAnd(value % 1000000U)) {
      output.append(kPortugueseAnd);
    }
  }
  const uint16_t thousands =
      static_cast<uint16_t>((value / 1000U) % 1000U);
  if (thousands != 0U) {
    if (thousands != 1U) {
      appendPortugueseGroup(thousands, output);
    }
    output.append(kPortugueseThousand);
    if (portugueseScaleNeedsAnd(value % 1000U)) {
      output.append(kPortugueseAnd);
    }
  }
  const uint16_t remainder = static_cast<uint16_t>(value % 1000U);
  if (remainder != 0U) {
    appendPortugueseGroup(remainder, output);
  }
}

void appendPersianGroup(uint16_t value, VoiceSequenceBuilder &output) {
  bool emitted = false;
  if (value >= 100U) {
    output.append(kPersianHundred + value / 100U - 1U);
    value %= 100U;
    emitted = true;
  }
  if (value >= 20U) {
    if (emitted) {
      output.append(kPersianAnd);
    }
    output.append(kPersianTwenty + value / 10U - 2U);
    value %= 10U;
    emitted = true;
  } else if (value >= 10U) {
    if (emitted) {
      output.append(kPersianAnd);
    }
    output.append(kPersianTen + value - 10U);
    return;
  }
  if (value != 0U) {
    if (emitted) {
      output.append(kPersianAnd);
    }
    output.append(kDigitVoiceBase + value);
  }
}

void appendPersianNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 1000000000U),
      static_cast<uint16_t>((value / 1000000U) % 1000U),
      static_cast<uint16_t>((value / 1000U) % 1000U),
      static_cast<uint16_t>(value % 1000U)};
  const uint16_t scales[] = {
      kPersianBillion, kPersianMillion, kPersianThousand, 0U};
  bool emitted = false;
  for (size_t index = 0U; index < 4U; ++index) {
    if (groups[index] == 0U) continue;
    if (emitted) output.append(kPersianAnd);
    if (index != 2U || groups[index] != 1U) {
      appendPersianGroup(groups[index], output);
    }
    if (scales[index] != 0U) output.append(scales[index]);
    emitted = true;
  }
}

void appendTurkishGroup(uint16_t value, VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    const uint8_t hundreds = static_cast<uint8_t>(value / 100U);
    if (hundreds != 1U) output.append(kDigitVoiceBase + hundreds);
    output.append(kTurkishHundred);
    value %= 100U;
  }
  if (value >= 10U) {
    output.append(kTurkishTen + value / 10U - 1U);
    value %= 10U;
  }
  if (value != 0U) output.append(kDigitVoiceBase + value);
}

void appendTurkishNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 1000000000U),
      static_cast<uint16_t>((value / 1000000U) % 1000U),
      static_cast<uint16_t>((value / 1000U) % 1000U),
      static_cast<uint16_t>(value % 1000U)};
  const uint16_t scales[] = {
      kTurkishBillion, kTurkishMillion, kTurkishThousand, 0U};
  for (size_t index = 0U; index < 4U; ++index) {
    if (groups[index] == 0U) continue;
    if (index != 2U || groups[index] != 1U) {
      appendTurkishGroup(groups[index], output);
    }
    if (scales[index] != 0U) output.append(scales[index]);
  }
}

void appendArabicBelowHundred(uint8_t value,
                              VoiceSequenceBuilder &output) {
  if (value < 20U) {
    output.append(value < 10U ? kDigitVoiceBase + value
                             : kArabicTen + value - 10U);
    return;
  }
  const uint8_t ones = value % 10U;
  if (ones != 0U) {
    output.append(kDigitVoiceBase + ones);
    output.append(kArabicAnd);
  }
  output.append(kArabicTwenty + value / 10U - 2U);
}

void appendArabicGroup(uint16_t value, VoiceSequenceBuilder &output) {
  if (value >= 100U) {
    output.append(kArabicHundred + value / 100U - 1U);
    value %= 100U;
    if (value != 0U) output.append(kArabicAnd);
  }
  if (value != 0U) {
    appendArabicBelowHundred(static_cast<uint8_t>(value), output);
  }
}

void appendArabicScale(uint16_t value, uint16_t singular,
                       uint16_t dual, uint16_t plural,
                       VoiceSequenceBuilder &output) {
  if (value == 1U) {
    output.append(singular);
  } else if (value == 2U) {
    output.append(dual);
  } else {
    appendArabicGroup(value, output);
    output.append(value <= 10U ? plural : singular);
  }
}

void appendArabicNumber(uint32_t value, VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(kDigitVoiceBase);
    return;
  }
  const uint16_t groups[] = {
      static_cast<uint16_t>(value / 1000000000U),
      static_cast<uint16_t>((value / 1000000U) % 1000U),
      static_cast<uint16_t>((value / 1000U) % 1000U),
      static_cast<uint16_t>(value % 1000U)};
  bool emitted = false;
  for (size_t index = 0U; index < 4U; ++index) {
    if (groups[index] == 0U) continue;
    if (emitted) output.append(kArabicAnd);
    if (index == 0U) {
      appendArabicScale(groups[index], kArabicBillion,
                        kArabicTwoBillion, kArabicBillions, output);
    } else if (index == 1U) {
      appendArabicScale(groups[index], kArabicMillion,
                        kArabicTwoMillion, kArabicMillions, output);
    } else if (index == 2U) {
      appendArabicScale(groups[index], kArabicThousand,
                        kArabicTwoThousand, kArabicThousands, output);
    } else {
      appendArabicGroup(groups[index], output);
    }
    emitted = true;
  }
}

uint16_t negativeVoiceId(ChipIntelliAudioClass::NumberLanguage language) {
  switch (language) {
    case ChipIntelliAudioClass::NumberLanguage::English:
      return kEnglishNegative;
    case ChipIntelliAudioClass::NumberLanguage::Japanese:
      return kJapaneseNegative;
    case ChipIntelliAudioClass::NumberLanguage::Korean:
      return kKoreanNegative;
    case ChipIntelliAudioClass::NumberLanguage::Russian:
      return kRussianNegative;
    case ChipIntelliAudioClass::NumberLanguage::Spanish:
      return kSpanishNegative;
    case ChipIntelliAudioClass::NumberLanguage::Thai:
      return kThaiNegative;
    case ChipIntelliAudioClass::NumberLanguage::German:
      return kGermanNegative;
    case ChipIntelliAudioClass::NumberLanguage::Indonesian:
      return kIndonesianNegative;
    case ChipIntelliAudioClass::NumberLanguage::Vietnamese:
      return kVietnameseNegative;
    case ChipIntelliAudioClass::NumberLanguage::French:
      return kFrenchNegative;
    case ChipIntelliAudioClass::NumberLanguage::Portuguese:
      return kPortugueseNegative;
    case ChipIntelliAudioClass::NumberLanguage::Persian:
      return kPersianNegative;
    case ChipIntelliAudioClass::NumberLanguage::Turkish:
      return kTurkishNegative;
    case ChipIntelliAudioClass::NumberLanguage::Arabic:
      return kArabicNegative;
    case ChipIntelliAudioClass::NumberLanguage::Chinese:
    default:
      return kDefaultNumberVoiceIds.negative;
  }
}

uint16_t decimalPointVoiceId(
    ChipIntelliAudioClass::NumberLanguage language) {
  switch (language) {
    case ChipIntelliAudioClass::NumberLanguage::English:
      return kEnglishDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Japanese:
      return kJapaneseDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Korean:
      return kKoreanDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Russian:
      return kRussianDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Spanish:
      return kSpanishDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Thai:
      return kThaiDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::German:
      return kGermanDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Indonesian:
      return kIndonesianDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Vietnamese:
      return kVietnameseDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::French:
      return kFrenchDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Portuguese:
      return kPortugueseDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Persian:
      return kPersianDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Turkish:
      return kTurkishDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Arabic:
      return kArabicDecimalPoint;
    case ChipIntelliAudioClass::NumberLanguage::Chinese:
    default:
      return kDefaultNumberVoiceIds.decimalPoint;
  }
}

void appendLocalizedNumber(uint32_t value,
                           ChipIntelliAudioClass::NumberLanguage language,
                           VoiceSequenceBuilder &output) {
  switch (language) {
    case ChipIntelliAudioClass::NumberLanguage::English:
      appendEnglishNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Japanese:
      appendJapaneseNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Korean:
      appendKoreanNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Russian:
      appendRussianNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Spanish:
      appendSpanishNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Thai:
      appendThaiNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::German:
      appendGermanNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Indonesian:
      appendIndonesianNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Vietnamese:
      appendVietnameseNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::French:
      appendFrenchNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Portuguese:
      appendPortugueseNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Persian:
      appendPersianNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Turkish:
      appendTurkishNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Arabic:
      appendArabicNumber(value, output);
      break;
    case ChipIntelliAudioClass::NumberLanguage::Chinese:
    default:
      appendUnsignedNumber(value, kDefaultNumberVoiceIds, output);
      break;
  }
}

uint32_t signedMagnitude(int32_t value) {
  const uint32_t unsignedValue = static_cast<uint32_t>(value);
  return value < 0 ? 0U - unsignedValue : unsignedValue;
}

bool sdkAudioReady() {
  bool flashReady = false;
  is_ci_flash_data_info_inited(&flashReady);

  // vol_get() starts at VOLUME_MAX + 1. It enters the configured range only
  // after the SDK system task has handled SYS_MSG_TYPE_AUDIO_IN_STARTED,
  // initialized the persisted volume and enabled the board audio path.
  uint8_t volumeLevel = vol_get();
  if (flashReady && audio_play_task_handle != nullptr &&
      ci_arduino_audio_started_message_count() != 0U &&
      (volumeLevel < VOLUME_MIN || volumeLevel > VOLUME_MAX)) {
    // The vendor startup path leaves its VOLUME_MAX + 1 sentinel unchanged if
    // NVDM contains an out-of-range byte. Repair it once the audio-start
    // message has been consumed instead of waiting for begin() to time out.
    volumeLevel = vol_set(VOLUME_DEFAULT);
  }
  return flashReady && audio_play_task_handle != nullptr &&
         volumeLevel >= VOLUME_MIN && volumeLevel <= VOLUME_MAX;
}
}  // namespace

class ChipIntelliAudioFactory {
 public:
  static ChipIntelliAudioClass &instance() {
    static ChipIntelliAudioClass instance;
    return instance;
  }
};

ChipIntelliAudioClass &ChipIntelliAudio =
    ChipIntelliAudioFactory::instance();

extern "C" int chipintelli_audio_mute_requested(void) {
  return ChipIntelliAudio.isMuted() ? 1 : 0;
}

extern "C" void chipintelli_sdk_prompt_unlocked(void) {
  ChipIntelliAudio.dispatchFinishedCallbacks();
}

ChipIntelliAudioClass::ChipIntelliAudioClass()
    : _finishedCallback(nullptr),
      _finishedContext(nullptr),
      _finishedGeneration(0U),
      _begun(false),
      _muted(false),
      _unmutedVolume(70),
      _playbackQueue(nullptr),
      _interruptQueue(nullptr),
      _playbackTask(nullptr),
      _sdkFinished{0U, 0U},
      _activeCallbackSlot(0U),
      _playbackActive(false),
      _droppedFinishedCallbacks(0U) {}

bool ChipIntelliAudioClass::ensurePlaybackTask() {
  if (_playbackQueue == nullptr) {
    _playbackQueue = xQueueCreate(kPlaybackQueueLength,
                                  sizeof(PlaybackRequest));
  }
  if (_interruptQueue == nullptr) {
    _interruptQueue = xQueueCreate(kInterruptQueueLength,
                                   sizeof(PlaybackRequest));
  }
  if (_playbackQueue == nullptr || _interruptQueue == nullptr) {
    return false;
  }
  if (_playbackTask != nullptr) {
    return true;
  }

  TaskHandle_t task = nullptr;
  if (xTaskCreate(playbackTaskEntry, "ciaudio", kPlaybackTaskStackDepth,
                  this, kPlaybackTaskPriority, &task) != pdPASS) {
    return false;
  }
  _playbackTask = task;
  return true;
}

void ChipIntelliAudioClass::playbackTaskEntry(void *context) {
  static_cast<ChipIntelliAudioClass *>(context)->playbackTaskLoop();
}

bool ChipIntelliAudioClass::enqueuePlaybackRequest(
    const PlaybackRequest &request) {
  if (!_begun || _playbackTask == nullptr) {
    return false;
  }

  QueueHandle_t queue = static_cast<QueueHandle_t>(
      request.interruptCurrent ? _interruptQueue : _playbackQueue);
  if (queue == nullptr || xQueueSend(queue, &request, 0) != pdPASS) {
    return false;
  }
  xTaskNotifyGive(static_cast<TaskHandle_t>(_playbackTask));
  return true;
}

bool ChipIntelliAudioClass::startPlaybackRequest(
    const PlaybackRequest &request, bool interruptCurrent) {
  play_done_callback_t callback = _activeCallbackSlot == 0U
                                      ? sdkPlaybackFinished0
                                      : sdkPlaybackFinished1;
  switch (request.kind) {
    case PlaybackRequest::Kind::VoiceSequence:
      return ci_arduino_prompt_play_voice_sequence(
                 request.voiceIds, request.count, callback,
                 interruptCurrent) == 0U;

    case PlaybackRequest::Kind::Beep: {
      cmd_handle_t beepHandle = cmd_info_find_command_by_string("<beep>");
      const uint16_t beepCommandId = cmd_info_get_command_id(beepHandle);
      if (beepCommandId == INVALID_SHORT_ID) {
        return false;
      }

      prompt_play_info_t prompts[MAX_COMBINATION_COUNT];
      for (uint8_t index = 0U; index < request.count; ++index) {
        prompts[index].cmd_id = beepCommandId;
        prompts[index].select_index = 0;
      }
      return prompt_play_by_multi_cmd_id(prompts, request.count, callback) ==
             0U;
    }

    case PlaybackRequest::Kind::CommandId:
      return prompt_play_by_cmd_id(static_cast<uint16_t>(request.value),
                                   request.optionIndex, callback,
                                   interruptCurrent) == 0U;

    case PlaybackRequest::Kind::SemanticId:
      return prompt_play_by_semantic_id(request.value, request.optionIndex,
                                        callback, interruptCurrent) == 0U;

    case PlaybackRequest::Kind::CommandText:
      return prompt_play_by_cmd_string(
                 const_cast<char *>(request.commandText), request.optionIndex,
                 callback, interruptCurrent) == 0U;

    case PlaybackRequest::Kind::Stop:
      return false;
  }
  return false;
}

void ChipIntelliAudioClass::finishLogicalPlayback() {
  taskENTER_CRITICAL();
  _playbackActive = false;
  const bool hasCallback = _finishedCallback != nullptr;
  const uint32_t callbackGeneration = _finishedGeneration;
  taskEXIT_CRITICAL();

  if (hasCallback &&
      !chipintelli_arduino_post_event(finishedEvent, this,
                                     callbackGeneration)) {
    taskENTER_CRITICAL();
    ++_droppedFinishedCallbacks;
    taskEXIT_CRITICAL();
  }
}

void ChipIntelliAudioClass::finishedEvent(void *context, uint32_t value) {
  ChipIntelliAudioClass *audio =
      static_cast<ChipIntelliAudioClass *>(context);
  if (audio == nullptr) return;
  taskENTER_CRITICAL();
  const bool current = value == audio->_finishedGeneration;
  FinishedCallback callback = current ? audio->_finishedCallback : nullptr;
  void *callbackContext = current ? audio->_finishedContext : nullptr;
  taskEXIT_CRITICAL();
  if (callback != nullptr) callback(callbackContext);
}

void ChipIntelliAudioClass::playbackTaskLoop() {
  while (true) {
    bool progressed = false;

    taskENTER_CRITICAL();
    const uint8_t callbackSlot = _activeCallbackSlot;
    const bool active = _playbackActive;
    bool sdkFinished = active && _sdkFinished[callbackSlot] != 0U;
    if (sdkFinished) {
      --_sdkFinished[callbackSlot];
    }
    taskEXIT_CRITICAL();

    if (sdkFinished) {
      finishLogicalPlayback();
      progressed = true;
    }

    PlaybackRequest request = {};
    QueueHandle_t interruptQueue =
        static_cast<QueueHandle_t>(_interruptQueue);
    if (interruptQueue != nullptr &&
        xQueueReceive(interruptQueue, &request, 0) == pdPASS) {
      taskENTER_CRITICAL();
      const bool interruptedPlayback = _playbackActive;
      taskEXIT_CRITICAL();
      if (interruptedPlayback) {
        finishLogicalPlayback();
      }

      if (request.kind == PlaybackRequest::Kind::Stop) {
        prompt_stop_play();
        taskENTER_CRITICAL();
        _sdkFinished[0] = 0U;
        _sdkFinished[1] = 0U;
        _playbackActive = false;
        taskEXIT_CRITICAL();
      } else {
        taskENTER_CRITICAL();
        _activeCallbackSlot ^= 1U;
        _sdkFinished[_activeCallbackSlot] = 0U;
        taskEXIT_CRITICAL();

        const bool accepted = startPlaybackRequest(request, true);
        if (accepted) {
          taskENTER_CRITICAL();
          _playbackActive = true;
          taskEXIT_CRITICAL();
        } else {
          finishLogicalPlayback();
        }
      }
      continue;
    }

    taskENTER_CRITICAL();
    const bool playbackActive = _playbackActive;
    taskEXIT_CRITICAL();
    QueueHandle_t playbackQueue = static_cast<QueueHandle_t>(_playbackQueue);
    if (!playbackActive && playbackQueue != nullptr &&
        xQueueReceive(playbackQueue, &request, 0) == pdPASS) {
      taskENTER_CRITICAL();
      _activeCallbackSlot ^= 1U;
      _sdkFinished[_activeCallbackSlot] = 0U;
      taskEXIT_CRITICAL();

      const bool accepted = startPlaybackRequest(request, false);
      if (accepted) {
        taskENTER_CRITICAL();
        _playbackActive = true;
        taskEXIT_CRITICAL();
      } else {
        finishLogicalPlayback();
      }
      continue;
    }

    if (!progressed) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
  }
}

bool ChipIntelliAudioClass::begin() {
  if (_begun) {
    return ensurePlaybackTask();
  }
  if (!chipintelli_sdk_begin()) {
    return false;
  }

  const uint32_t started = millis();
  uint32_t idleSince = 0;
  while (true) {
    if (!sdkAudioReady() || prompt_is_playing() != 0U) {
      idleSince = 0;
    } else if (idleSince == 0) {
      idleSince = millis();
    } else if ((millis() - idleSince) >= kIdleStabilityMs) {
      break;
    }

    if ((millis() - started) >= kInitTimeoutMs) {
      return false;
    }
    delay(1);
  }

  prompt_player_enable(ENABLE);
  const int32_t currentGain = audio_play_get_vol_gain();
  _unmutedVolume = currentGain <= 0
                         ? 0U
                         : static_cast<uint8_t>(currentGain >= 100
                                                    ? 100
                                                    : currentGain);
  if (!ensurePlaybackTask()) {
    return false;
  }
  _muted = false;
  _begun = true;
  return true;
}

void ChipIntelliAudioClass::end() {
  // A stop can complete a queued prompt. Clear the user callback first so
  // end() never delivers a late completion into application teardown code.
  onFinished(nullptr);
  stop();
  setMuted(false);
  _begun = false;
}

/**
 * @brief 直接按语音 ID 播放 voice.bin 中的一条语音资源。
 *
 * 本函数不会查询命令词表，也没有播报选项；voiceId 本身直接标识要播放的
 * 语音资源。这是它与 playCommand() 的主要区别。
 *
 * @param voiceId 语音资源 ID，取值范围为 0～65535，必须与当前工程烧录的
 *                voice.bin 中的 ID 一致。
 * @param interruptCurrent true 表示加入抢占队列；false 表示加入有序播放队列。
 * @return true 表示异步队列已复制请求；false 表示尚未调用 begin() 或队列已满。
 *         返回 true 不保证资源一定存在或最终播放成功。
 */
bool ChipIntelliAudioClass::playVoice(uint16_t voiceId,
                                     bool interruptCurrent) {
  return playVoiceSequence(&voiceId, 1U, interruptCurrent);
}

bool ChipIntelliAudioClass::playLocalizedNumber(
    const String &numberText, NumberLanguage language,
    bool interruptCurrent) {
  const char *text = numberText.c_str();
  size_t begin = 0U;
  size_t end = numberText.length();
  while (begin < end && isAsciiWhitespace(text[begin])) {
    ++begin;
  }
  while (end > begin && isAsciiWhitespace(text[end - 1U])) {
    --end;
  }
  if (begin == end) {
    return false;
  }

  bool negative = false;
  if (text[begin] == '+' || text[begin] == '-') {
    negative = text[begin] == '-';
    ++begin;
  }
  if (begin == end) {
    return false;
  }

  uint32_t integerPart = 0U;
  bool decimalPointSeen = false;
  bool digitSeen = false;
  bool nonzeroDigitSeen = false;
  size_t fractionalStart = end;
  size_t fractionalDigits = 0U;

  for (size_t index = begin; index < end; ++index) {
    const char current = text[index];
    if (current >= '0' && current <= '9') {
      const uint8_t digit = static_cast<uint8_t>(current - '0');
      digitSeen = true;
      nonzeroDigitSeen = nonzeroDigitSeen || digit != 0U;
      if (decimalPointSeen) {
        ++fractionalDigits;
        if (fractionalDigits > kMaxVoiceSequenceLength) {
          return false;
        }
      } else {
        if (integerPart > (UINT32_MAX - digit) / 10U) {
          return false;
        }
        integerPart = integerPart * 10U + digit;
      }
      continue;
    }

    if (current == '.' && !decimalPointSeen) {
      decimalPointSeen = true;
      fractionalStart = index + 1U;
      continue;
    }
    return false;
  }

  if (!digitSeen) {
    return false;
  }

  uint16_t sequence[kMaxVoiceSequenceLength];
  VoiceSequenceBuilder builder(sequence, kMaxVoiceSequenceLength);
  if (negative && nonzeroDigitSeen) {
    builder.append(negativeVoiceId(language));
  }
  appendLocalizedNumber(integerPart, language, builder);

  if (decimalPointSeen && fractionalDigits != 0U) {
    builder.append(decimalPointVoiceId(language));
    for (size_t index = fractionalStart; index < end; ++index) {
      builder.append(kDigitVoiceBase + text[index] - '0');
    }
  }

  const size_t count = builder.size();
  return count != 0U &&
         playVoiceSequence(sequence, count, interruptCurrent);
}

bool ChipIntelliAudioClass::playVoiceSequence(const uint16_t *voiceIds,
                                              size_t count,
                                              bool interruptCurrent) {
  if (!_begun || voiceIds == nullptr || count == 0U ||
      count > kMaxVoiceSequenceLength) {
    return false;
  }

  PlaybackRequest request = {};
  request.kind = PlaybackRequest::Kind::VoiceSequence;
  request.interruptCurrent = interruptCurrent;
  request.count = static_cast<uint8_t>(count);
  for (size_t index = 0U; index < count; ++index) {
    request.voiceIds[index] = voiceIds[index];
  }
  return enqueuePlaybackRequest(request);
}

size_t ChipIntelliAudioClass::buildNumberVoiceSequence(
    int32_t value, const NumberVoiceIds &voiceIds, uint16_t *output,
    size_t capacity) const {
  VoiceSequenceBuilder builder(output, capacity);
  if (value < 0) {
    builder.append(voiceIds.negative);
  }
  appendUnsignedNumber(signedMagnitude(value), voiceIds, builder);
  return builder.size();
}

size_t ChipIntelliAudioClass::buildFixedPointVoiceSequence(
    int32_t value, uint8_t fractionalDigits,
    const NumberVoiceIds &voiceIds, uint16_t *output,
    size_t capacity) const {
  if (fractionalDigits > 9U) {
    return 0U;
  }
  if (fractionalDigits == 0U) {
    return buildNumberVoiceSequence(value, voiceIds, output, capacity);
  }

  VoiceSequenceBuilder builder(output, capacity);
  if (value < 0) {
    builder.append(voiceIds.negative);
  }

  const uint32_t magnitude = signedMagnitude(value);
  const uint32_t scale = kPowersOfTen[fractionalDigits];
  appendUnsignedNumber(magnitude / scale, voiceIds, builder);
  builder.append(voiceIds.decimalPoint);

  uint32_t remainder = magnitude % scale;
  for (uint8_t digitsLeft = fractionalDigits; digitsLeft > 0U;
       --digitsLeft) {
    const uint32_t divisor = kPowersOfTen[digitsLeft - 1U];
    builder.append(voiceIds.digits[remainder / divisor]);
    remainder %= divisor;
  }
  return builder.size();
}

bool ChipIntelliAudioClass::playNumber(
    int32_t value, const NumberVoiceIds &voiceIds) {
  uint16_t sequence[kMaxVoiceSequenceLength];
  const size_t count = buildNumberVoiceSequence(
      value, voiceIds, sequence, kMaxVoiceSequenceLength);
  return count != 0U && playVoiceSequence(sequence, count);
}

bool ChipIntelliAudioClass::playFixedPoint(
    int32_t value, uint8_t fractionalDigits,
    const NumberVoiceIds &voiceIds) {
  uint16_t sequence[kMaxVoiceSequenceLength];
  const size_t count = buildFixedPointVoiceSequence(
      value, fractionalDigits, voiceIds, sequence,
      kMaxVoiceSequenceLength);
  return count != 0U && playVoiceSequence(sequence, count);
}

bool ChipIntelliAudioClass::playBeep(unsigned int count) {
  if (!_begun || count == 0U || count > MAX_COMBINATION_COUNT) {
    return false;
  }

  PlaybackRequest request = {};
  request.kind = PlaybackRequest::Kind::Beep;
  request.interruptCurrent = true;
  request.count = static_cast<uint8_t>(count);
  return enqueuePlaybackRequest(request);
}

/**
 * @brief unsigned long 命令 ID 兼容重载，检查范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(unsigned long commandId,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief long 命令 ID 兼容重载，检查正负号和范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 为负数或越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(long commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (commandId < 0 || commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief unsigned int 命令 ID 兼容重载，检查范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(unsigned int commandId,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief int 命令 ID 兼容重载，检查正负号和范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 为负数或越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(int commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (commandId < 0 || commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief 按 16 位命令 ID 查找命令词表，并播放该命令关联的提示音。
 *
 * 与 playVoice() 不同，本函数先通过 commandId 查找命令配置，再根据
 * optionIndex 选择该命令关联的一组提示音；一组选项可以由多段语音组合。
 *
 * @param commandId 命令词表中的 16 位命令 ID，不是 voice.bin 的语音 ID。
 * @param optionIndex 从 0 开始的播报选项索引。-1 表示由 SDK 按资源配置
 *                    选择：随机类型随机选择，否则使用第 0 项。超出已配置
 *                    选项范围的非负值也会回退到上述 SDK 选择规则。
 * @param interruptCurrent true 表示加入抢占队列；false 表示加入有序播放队列。
 * @return true 表示异步队列已复制请求；false 表示尚未调用 begin() 或队列已满。
 *         命令查找稍后在播放任务中执行，true 不代表资源存在或播放已经完成。
 */
bool ChipIntelliAudioClass::playCommand(uint16_t commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (!_begun) {
    return false;
  }

  PlaybackRequest request = {};
  request.kind = PlaybackRequest::Kind::CommandId;
  request.interruptCurrent = interruptCurrent;
  request.optionIndex = optionIndex;
  request.value = commandId;
  return enqueuePlaybackRequest(request);
}

bool ChipIntelliAudioClass::playSemantic(uint32_t semanticId, int optionIndex,
                                        bool interruptCurrent) {
  if (!_begun) {
    return false;
  }

  PlaybackRequest request = {};
  request.kind = PlaybackRequest::Kind::SemanticId;
  request.interruptCurrent = interruptCurrent;
  request.optionIndex = optionIndex;
  request.value = semanticId;
  return enqueuePlaybackRequest(request);
}

/**
 * @brief 按命令文本查找命令词表，并播放该命令关联的提示音。
 *
 * commandText 只用于查找资源包内已经配置的命令，不会把任意文本转换成语音，
 * 因此本函数不是 TTS 接口。
 *
 * @param commandText 以 '\0' 结尾的命令文本，必须与资源包中的命令字符串匹配；
 *                    不能为 nullptr 或空字符串。
 * @param optionIndex 从 0 开始的播报选项索引。-1 表示由 SDK 按资源配置
 *                    选择：随机类型随机选择，否则使用第 0 项。超出已配置
 *                    选项范围的非负值也会回退到上述 SDK 选择规则。
 * @param interruptCurrent true 表示加入抢占队列；false 表示加入有序播放队列。
 * @return true 表示异步队列已复制请求；false 表示尚未调用 begin()、文本无效
 *         或队列已满。命令查找稍后在播放任务中执行，true 不代表播放完成。
 */
bool ChipIntelliAudioClass::playCommand(const char *commandText,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (!_begun || commandText == nullptr || commandText[0] == '\0') {
    return false;
  }

  size_t length = 0U;
  while (commandText[length] != '\0' &&
         length < PlaybackRequest::kCommandTextCapacity) {
    ++length;
  }
  if (length == PlaybackRequest::kCommandTextCapacity) {
    return false;
  }

  PlaybackRequest request = {};
  request.kind = PlaybackRequest::Kind::CommandText;
  request.interruptCurrent = interruptCurrent;
  request.optionIndex = optionIndex;
  for (size_t index = 0U; index <= length; ++index) {
    request.commandText[index] = commandText[index];
  }
  return enqueuePlaybackRequest(request);
}

bool ChipIntelliAudioClass::stop() {
  if (!_begun) {
    return true;
  }

  QueueHandle_t playbackQueue = static_cast<QueueHandle_t>(_playbackQueue);
  QueueHandle_t interruptQueue = static_cast<QueueHandle_t>(_interruptQueue);
  if (playbackQueue == nullptr || interruptQueue == nullptr) {
    return false;
  }
  xQueueReset(playbackQueue);
  xQueueReset(interruptQueue);

  PlaybackRequest request = {};
  request.kind = PlaybackRequest::Kind::Stop;
  request.interruptCurrent = true;
  return enqueuePlaybackRequest(request);
}

bool ChipIntelliAudioClass::isPlaying() const {
  if (!_begun) {
    return false;
  }

  taskENTER_CRITICAL();
  const bool active = _playbackActive;
  taskEXIT_CRITICAL();
  QueueHandle_t playbackQueue = static_cast<QueueHandle_t>(_playbackQueue);
  QueueHandle_t interruptQueue = static_cast<QueueHandle_t>(_interruptQueue);
  return active || prompt_is_playing() != 0U ||
         (playbackQueue != nullptr &&
          uxQueueMessagesWaiting(playbackQueue) != 0U) ||
         (interruptQueue != nullptr &&
          uxQueueMessagesWaiting(interruptQueue) != 0U);
}

bool ChipIntelliAudioClass::isReady() const {
  return _begun;
}

void ChipIntelliAudioClass::setVolume(uint8_t percent) {
  if (!_begun) {
    return;
  }
  if (percent > 100U) {
    percent = 100U;
  }
  _unmutedVolume = percent;
  audio_play_set_vol_gain(_muted ? 0 : percent);
}

uint8_t ChipIntelliAudioClass::volume() const {
  if (!_begun) {
    return 0U;
  }
  if (_muted) {
    return _unmutedVolume;
  }
  int32_t gain = audio_play_get_vol_gain();
  if (gain <= 0) {
    return 0U;
  }
  if (gain >= 100) {
    return 100U;
  }
  return static_cast<uint8_t>(gain);
}

void ChipIntelliAudioClass::setMuted(bool muted) {
  if (!_begun) {
    return;
  }
  if (_muted == muted) {
    return;
  }

  if (muted) {
    const int32_t gain = audio_play_get_vol_gain();
    _unmutedVolume = gain <= 0
                           ? 0U
                           : static_cast<uint8_t>(gain >= 100 ? 100 : gain);
    _muted = true;
    audio_play_set_vol_gain(0);
    return;
  }

  _muted = false;
  audio_play_set_vol_gain(_unmutedVolume);
}

void ChipIntelliAudioClass::mute() {
  setMuted(true);
}

void ChipIntelliAudioClass::unmute() {
  setMuted(false);
}

bool ChipIntelliAudioClass::isMuted() const {
  return _muted;
}

void ChipIntelliAudioClass::onFinished(FinishedCallback callback,
                                       void *context) {
  taskENTER_CRITICAL();
  ++_finishedGeneration;
  _finishedCallback = callback;
  _finishedContext = callback != nullptr ? context : nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliAudioClass::sdkPlaybackFinished0(void *commandHandle) {
  (void)commandHandle;
  ChipIntelliAudio.recordSdkPlaybackFinished(0U);
}

uint32_t ChipIntelliAudioClass::droppedFinishedCallbacks() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _droppedFinishedCallbacks;
  taskEXIT_CRITICAL();
  return dropped;
}

void ChipIntelliAudioClass::sdkPlaybackFinished1(void *commandHandle) {
  (void)commandHandle;
  ChipIntelliAudio.recordSdkPlaybackFinished(1U);
}

void ChipIntelliAudioClass::recordSdkPlaybackFinished(
    uint8_t callbackSlot) {
  taskENTER_CRITICAL();
  if (_sdkFinished[callbackSlot] != UINT32_MAX) {
    ++_sdkFinished[callbackSlot];
  }
  taskEXIT_CRITICAL();
}

void ChipIntelliAudioClass::dispatchFinishedCallbacks() {
  taskENTER_CRITICAL();
  TaskHandle_t task = static_cast<TaskHandle_t>(_playbackTask);
  taskEXIT_CRITICAL();
  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

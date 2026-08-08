# 多语言变量数字播报规范

## 语言选择

十五种语言分别使用独立的 `voice.bin`，每套数字词元都从 ID 300 开始。一个固件只选择
一种语言，不会同时占用多套编号。语言宏必须写在头文件之前；未声明时默认中文：

```cpp
#define CHIPINTELLI_LANGUAGE CHIPINTELLI_LANGUAGE_EN
#include <ChipIntelliAudio.h>
```

可选值如下：

| 宏值 | 语言 | 词元 ID |
| --- | --- | --- |
| `CHIPINTELLI_LANGUAGE_ZH` | 中文 | 300～316 |
| `CHIPINTELLI_LANGUAGE_EN` | 英文 | 300～333 |
| `CHIPINTELLI_LANGUAGE_JA` | 日文 | 300～340 |
| `CHIPINTELLI_LANGUAGE_KO` | 韩文 | 300～316 |
| `CHIPINTELLI_LANGUAGE_RU` | 俄文 | 300～349 |
| `CHIPINTELLI_LANGUAGE_ES` | 西班牙文 | 300～354 |
| `CHIPINTELLI_LANGUAGE_TH` | 泰文 | 300～319 |
| `CHIPINTELLI_LANGUAGE_DE` | 德文 | 300～338 |
| `CHIPINTELLI_LANGUAGE_ID` | 印度尼西亚文 | 300～321 |
| `CHIPINTELLI_LANGUAGE_VI` | 越南文 | 300～321 |
| `CHIPINTELLI_LANGUAGE_FR` | 法文 | 300～335 |
| `CHIPINTELLI_LANGUAGE_PT` | 葡萄牙文（巴西） | 300～345 |
| `CHIPINTELLI_LANGUAGE_FA` | 波斯文 | 300～342 |
| `CHIPINTELLI_LANGUAGE_TR` | 土耳其文 | 300～324 |
| `CHIPINTELLI_LANGUAGE_AR` | 阿拉伯文（现代标准语） | 300～348 |

`citool-cli generate` 检测到 `#include <ChipIntelliAudio.h>` 后，会读取这个宏并自动把
所选语言的完整词表加入 TTS 请求。草图中不需要声明 `VOICE300` 等资源宏。

## 中文（ZH）

| ID | 文本 | ID | 文本 | ID | 文本 |
| ---: | --- | ---: | --- | ---: | --- |
| 300 | 零 | 301 | 一 | 302 | 二 |
| 303 | 三 | 304 | 四 | 305 | 五 |
| 306 | 六 | 307 | 七 | 308 | 八 |
| 309 | 九 | 310 | 十 | 311 | 百 |
| 312 | 千 | 313 | 万 | 314 | 亿 |
| 315 | 负 | 316 | 点 | | |

整数按中文四位分组规则播报，缺位时补“零”，例如 `10010` 为“一万零一十”。

## 英文（EN）

| ID | 文本 | ID | 文本 |
| ---: | --- | ---: | --- |
| 300 | zero | 301 | one |
| 302 | two | 303 | three |
| 304 | four | 305 | five |
| 306 | six | 307 | seven |
| 308 | eight | 309 | nine |
| 310 | ten | 311 | eleven |
| 312 | twelve | 313 | thirteen |
| 314 | fourteen | 315 | fifteen |
| 316 | sixteen | 317 | seventeen |
| 318 | eighteen | 319 | nineteen |
| 320 | twenty | 321 | thirty |
| 322 | forty | 323 | fifty |
| 324 | sixty | 325 | seventy |
| 326 | eighty | 327 | ninety |
| 328 | hundred | 329 | thousand |
| 330 | million | 331 | billion |
| 332 | minus | 333 | point |

采用不插入 `and` 的中性/美式规则，例如 `342` 为 “three hundred forty two”，
`1001` 为 “one thousand one”。小数点后逐位播报。

## 日文（JA）

| ID | 文本 | ID | 文本 | ID | 文本 |
| ---: | --- | ---: | --- | ---: | --- |
| 300 | 零 | 301 | 一 | 302 | 二 |
| 303 | 三 | 304 | 四 | 305 | 五 |
| 306 | 六 | 307 | 七 | 308 | 八 |
| 309 | 九 | 310 | 十 | 311 | 二十 |
| 312 | 三十 | 313 | 四十 | 314 | 五十 |
| 315 | 六十 | 316 | 七十 | 317 | 八十 |
| 318 | 九十 | 319 | 百 | 320 | 二百 |
| 321 | 三百 | 322 | 四百 | 323 | 五百 |
| 324 | 六百 | 325 | 七百 | 326 | 八百 |
| 327 | 九百 | 328 | 千 | 329 | 二千 |
| 330 | 三千 | 331 | 四千 | 332 | 五千 |
| 333 | 六千 | 334 | 七千 | 335 | 八千 |
| 336 | 九千 | 337 | 万 | 338 | 億 |
| 339 | マイナス | 340 | 点 | | |

十、百、千位使用完整倍数词元，确保“三百（さんびゃく）”“六百（ろっぴゃく）”
“八百（はっぴゃく）”“三千（さんぜん）”“八千（はっせん）”等音变正确。
整数内部不补读“零”。

## 韩文（KO）

| ID | 文本 | ID | 文本 | ID | 文本 |
| ---: | --- | ---: | --- | ---: | --- |
| 300 | 영 | 301 | 일 | 302 | 이 |
| 303 | 삼 | 304 | 사 | 305 | 오 |
| 306 | 육 | 307 | 칠 | 308 | 팔 |
| 309 | 구 | 310 | 십 | 311 | 백 |
| 312 | 천 | 313 | 만 | 314 | 억 |
| 315 | 마이너스 | 316 | 점 | | |

采用汉字数词规则；십、백、천前省略일，整数内部不补读영。正好 `10000` 读“만”，
`100000000` 读“일억”。

## 德文（DE）

| ID | 文本 | ID | 文本 |
| ---: | --- | ---: | --- |
| 300 | null | 301 | eins |
| 302 | zwei | 303 | drei |
| 304 | vier | 305 | fünf |
| 306 | sechs | 307 | sieben |
| 308 | acht | 309 | neun |
| 310 | ein | 311 | eine |
| 312 | zehn | 313 | elf |
| 314 | zwölf | 315 | dreizehn |
| 316 | vierzehn | 317 | fünfzehn |
| 318 | sechzehn | 319 | siebzehn |
| 320 | achtzehn | 321 | neunzehn |
| 322 | zwanzig | 323 | dreißig |
| 324 | vierzig | 325 | fünfzig |
| 326 | sechzig | 327 | siebzig |
| 328 | achtzig | 329 | neunzig |
| 330 | und | 331 | hundert |
| 332 | tausend | 333 | Million |
| 334 | Millionen | 335 | Milliarde |
| 336 | Milliarden | 337 | minus |
| 338 | Komma | | |

算法区分独立的 `eins`、复合词中的 `ein`、阴性单数的 `eine`，并区分
`Million/Millionen`、`Milliarde/Milliarden`。例如 `21` 为“ein und zwanzig”，
`1000000` 为“eine Million”。

## 俄文（RU）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | ноль、один、два、три、четыре、пять、шесть、семь、восемь、девять |
| 310～319 | десять、одиннадцать、двенадцать、тринадцать、четырнадцать、пятнадцать、шестнадцать、семнадцать、восемнадцать、девятнадцать |
| 320～327 | двадцать、тридцать、сорок、пятьдесят、шестьдесят、семьдесят、восемьдесят、девяносто |
| 328～336 | сто、двести、триста、четыреста、пятьсот、шестьсот、семьсот、восемьсот、девятьсот |
| 337～349 | одна、две、тысяча、тысячи、тысяч、миллион、миллиона、миллионов、миллиард、миллиарда、миллиардов、минус、запятая |

千位使用阴性形式 `одна/две`，千、百万、十亿按数值选择单数、少数或复数词形。

## 西班牙文（ES）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | cero、uno、dos、tres、cuatro、cinco、seis、siete、ocho、nueve |
| 310～319 | diez、once、doce、trece、catorce、quince、dieciséis、diecisiete、dieciocho、diecinueve |
| 320～329 | veinte、veintiuno、veintidós、veintitrés、veinticuatro、veinticinco、veintiséis、veintisiete、veintiocho、veintinueve |
| 330～339 | treinta、cuarenta、cincuenta、sesenta、setenta、ochenta、noventa、y、un、veintiún |
| 340～349 | cien、ciento、doscientos、trescientos、cuatrocientos、quinientos、seiscientos、setecientos、ochocientos、novecientos |
| 350～354 | mil、millón、millones、menos、coma |

百万以上采用短级差：`10^9` 播报为 `mil millones`；单位前使用 `un/veintiún`。

## 泰文（TH）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | ศูนย์、หนึ่ง、สอง、สาม、สี่、ห้า、หก、เจ็ด、แปด、เก้า |
| 310～319 | เอ็ด、ยี่、สิบ、ร้อย、พัน、หมื่น、แสน、ล้าน、ลบ、จุด |

十位的二使用 `ยี่`，非单独个位的一使用 `เอ็ด`；大数以 `ล้าน` 为六位分组单位。

## 印度尼西亚文（ID）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | nol、satu、dua、tiga、empat、lima、enam、tujuh、delapan、sembilan |
| 310～319 | sepuluh、sebelas、belas、puluh、seratus、ratus、seribu、ribu、juta、miliar |
| 320～321 | minus、koma |

`100` 和 `1000` 分别使用 `seratus`、`seribu`，其余百位和千位使用组合词元。

## 越南文（VI）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | không、một、hai、ba、bốn、năm、sáu、bảy、tám、chín |
| 310～319 | mốt、tư、lăm、mười、mươi、trăm、linh、nghìn、triệu、tỷ |
| 320～321 | âm、phẩy |

按位置选择 `một/mốt`、`bốn/tư`、`năm/lăm`；较高分组存在时补读必要的
`không trăm linh`。

## 法文（FR）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | zéro、un、deux、trois、quatre、cinq、six、sept、huit、neuf |
| 310～319 | dix、onze、douze、treize、quatorze、quinze、seize、dix-sept、dix-huit、dix-neuf |
| 320～329 | vingt、trente、quarante、cinquante、soixante、quatre-vingts、quatre-vingt、et、cent、mille |
| 330～335 | million、millions、milliard、milliards、moins、virgule |

70、80、90 使用法语组合规则，并区分独立的 `quatre-vingts` 与后接个位的
`quatre-vingt`。

## 葡萄牙文（PT，巴西）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | zero、um、dois、três、quatro、cinco、seis、sete、oito、nove |
| 310～319 | dez、onze、doze、treze、catorze、quinze、dezesseis、dezessete、dezoito、dezenove |
| 320～329 | vinte、trinta、quarenta、cinquenta、sessenta、setenta、oitenta、noventa、e、cem |
| 330～339 | cento、duzentos、trezentos、quatrocentos、quinhentos、seiscentos、setecentos、oitocentos、novecentos、mil |
| 340～345 | milhão、milhões、bilhão、bilhões、menos、vírgula |

使用巴西葡萄牙语词形和短级差，区分正好 `100` 的 `cem` 与复合数中的 `cento`。

## 波斯文（FA）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | صفر、یک、دو、سه、چهار、پنج、شش、هفت、هشت、نه |
| 310～319 | ده、یازده、دوازده、سیزده、چهارده、پانزده、شانزده、هفده、هجده、نوزده |
| 320～329 | بیست、سی、چهل、پنجاه、شصت、هفتاد、هشتاد、نود、صد、دویست |
| 330～339 | سیصد、چهارصد、پانصد、ششصد、هفتصد、هشتصد、نهصد、و、هزار、میلیون |
| 340～342 | میلیارد、منفی、ممیز |

同一组三位数及不同大数分组之间使用连接词 `و`。

## 土耳其文（TR）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | sıfır、bir、iki、üç、dört、beş、altı、yedi、sekiz、dokuz |
| 310～319 | on、yirmi、otuz、kırk、elli、altmış、yetmiş、seksen、doksan、yüz |
| 320～324 | bin、milyon、milyar、eksi、virgül |

`100` 与 `1000` 前省略 `bir`，其余十、百、千位直接组合。

## 阿拉伯文（AR，现代标准语）

| ID 范围 | 按 ID 顺序排列的文本 |
| --- | --- |
| 300～309 | صفر、واحد、اثنان、ثلاثة、أربعة、خمسة、ستة、سبعة、ثمانية、تسعة |
| 310～319 | عشرة、أحد عشر、اثنا عشر、ثلاثة عشر、أربعة عشر、خمسة عشر、ستة عشر、سبعة عشر、ثمانية عشر、تسعة عشر |
| 320～329 | عشرون、ثلاثون、أربعون、خمسون、ستون、سبعون、ثمانون、تسعون、و、مئة |
| 330～339 | مئتان、ثلاثمئة、أربعمئة、خمسمئة、ستمئة、سبعمئة、ثمانمئة、تسعمئة、ألف、ألفان |
| 340～348 | آلاف、مليون、مليونان、ملايين、مليار、ملياران、مليارات、سالب、فاصلة |

使用现代标准阿拉伯语，个位在十位前，并用 `و` 连接；千、百万、十亿分别提供
单数、双数和复数资源。

## 通用输入和制作要求

- 接受首尾 ASCII 空白、可选正负号和一个十进制小数点；整数部分范围为
  `0`～`4294967295`，不接受科学计数法。
- 小数部分逐位播报并保留零；`-0`、`-0.00` 不读负号。
- 一组最多 24 段音频；`interruptCurrent` 只影响第一段如何开始，后续词元连续播放。
- 同一语言的所有词元应使用相同发音人、语速、音量、采样率和编码参数。
- 裁掉过长的头尾静音，但保留少量自然边界；单位词和连接词使用可继续连读的中性语气。
- 英、日、德词表中的完整词形不可擅自合并或删除，否则会破坏不规则发音、语序或词形变化。

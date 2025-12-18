#include "all.h"

fn bool stringsEq(String* a, String* b) {
  if (a->length != b->length) {
    return false;
  }
  for (i32 i = 0; i < a->length; i++) {
    if (a->bytes[i] != b->bytes[i]) {
      return false;
    }
  }
  return true;
}

fn bool cStringEqString(str a, String* b) {
  if (strlen(a) != b->length) {
    return false;
  }
  for (i32 i = 0; i < b->length; i++) {
    if (a[i] != b->bytes[i]) {
      return false;
    }
  }
  return true;
}

fn Utf8Character classifyUtf8Character(u8 c) {
  /*two_byte utf8 starts with 1100. 192
  three_byte utf8 starts with 1110. 224
  four_byte utf8 starts with 1111. 240*/
  if (c <= 127) {
    return Utf8CharacterAscii;
  } else if (c >= 192 && c < 224) {
    return Utf8CharacterTwoByte;
  } else if (c >= 224 && c < 240) {
    return Utf8CharacterThreeByte;
  } else if (c >= 240) {
    return Utf8CharacterFourByte;
  } else {
    assert(false && "Not a valid utf8 starting byte");
    return Utf8Character_Count;
  }
}

fn bool isUtf8Ascii(u8 c) {
  return classifyUtf8Character(c) == Utf8CharacterAscii;
}

fn bool isUtf8TwoByte(u8 c) {
  return classifyUtf8Character(c) == Utf8CharacterTwoByte;
}

fn bool isUtf8ThreeByte(u8 c) {
  return classifyUtf8Character(c) == Utf8CharacterThreeByte;
}

fn bool isUtf8FourByte(u8 c) {
  return classifyUtf8Character(c) == Utf8CharacterFourByte;
}

fn u8 lowerAscii(u8 c) {
  if (c >= 65 && c <= 90) {
    return c + 32;
  }
  return c;
}

fn u8 upperAscii(u8 c) {
  if (c >= 97 && c <= 122) {
    return c - 32;
  }
  return c;
}

fn bool isAlphaUnderscoreSpace(u8 c) {
  return ((c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || c == ' '
        || c == '_');
}

fn bool isSimplePrintable(u8 c) {
  return (c >= ' ' && c <= '~');
}

typedef struct StrDecode {
	u32 codepoint;
	u32 size;
} StrDecode;

fn StrDecode strDecodeUTF8(u8 *str, u32 cap){
	u8 length[] = {
		1, 1, 1, 1, // 000xx
		1, 1, 1, 1,
		1, 1, 1, 1,
		1, 1, 1, 1,
		0, 0, 0, 0, // 100xx
		0, 0, 0, 0,
		2, 2, 2, 2, // 110xx
		3, 3,       // 1110x
		4,          // 11110
		0,          // 11111
	};
	u8 first_byte_mask[] = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
	u8 final_shift[] = { 0, 18, 12, 6, 0 };

	StrDecode result = {0};
	if (cap > 0){
		result.codepoint = '#';
		result.size = 1;

		u8 byte = str[0];
		u8 l = length[byte >> 3];
		if (0 < l && l <= cap){
			u32 cp = (byte & first_byte_mask[l]) << 18;
			switch (l){
				case 4: cp |= ((str[3] & 0x3F) << 0);
				case 3: cp |= ((str[2] & 0x3F) << 6);
				case 2: cp |= ((str[1] & 0x3F) << 12);
				default: break;
			}
			cp >>= final_shift[l];

			result.codepoint = cp;
			result.size = l;
		}
	}

	return result;
}

fn u32 strEncodeUTF8(u8 *dst, u32 codepoint){
	u32 size = 0;
	if (codepoint < (1 << 8)) {
		dst[0] = codepoint;
		size = 1;
	} else if (codepoint < (1 << 11)) {
		dst[0] = 0xC0 | (codepoint >> 6);
		dst[1] = 0x80 | (codepoint & 0x3F);
		size = 2;
	}
	else if (codepoint < (1 << 16)) {
		dst[0] = 0xE0 | (codepoint >> 12);
		dst[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		dst[2] = 0x80 | (codepoint & 0x3F);
		size = 3;
	} else if (codepoint < (1 << 21)) {
		dst[0] = 0xF0 | (codepoint >> 18);
		dst[1] = 0x80 | ((codepoint >> 12) & 0x3F);
		dst[2] = 0x80 | ((codepoint >> 6) & 0x3F);
		dst[3] = 0x80 | (codepoint & 0x3F);
		size = 4;
	} else {
		dst[0] = '#';
		size = 1;
	}
	return size;
}

fn StrDecode strDecodeUTF16(u16 *str, u32 cap){
	StrDecode result = {'#', 1};
	u16 x = str[0];
	if (x < 0xD800 || 0xDFFF < x) {
		result.codepoint = x;
	} else if (cap >= 2) {
		u16 y = str[1];
		if (0xD800 <= x && x < 0xDC00 &&
			0xDC00 <= y && y < 0xE000
		) {
			u16 xj = x - 0xD800;
			u16 yj = y - 0xDc00;
			u32 xy = (xj << 10) | yj;
			result.codepoint = xy + 0x10000;
			result.size = 2;
		}
	}
	return result;
}

fn u32 strEncodeUTF16(u16 *dst, u32 codepoint){
	u32 size = 0;
	if (codepoint < 0x10000) {
		dst[0] = codepoint;
		size = 1;
	} else {
		u32 cpj = codepoint - 0x10000;
		dst[0] = (cpj >> 10) + 0xD800;
		dst[1] = (cpj & 0x3FF) + 0xDC00;
		size = 2;
	}
	return(size);
}

fn StringUTF16Const str16FromStr8(Arena* arena, String string) {
	u16* memory = arenaAllocArray(arena, u16, string.length * 2 + 1);

	u16* dptr = memory;
	u8* ptr = (u8*)string.bytes;
	u8* opl = (u8*)string.bytes + string.length;
	for (; ptr < opl;){
		StrDecode decode = strDecodeUTF8(ptr, (u64)(opl - ptr));
		u32 enc_size = strEncodeUTF16(dptr, decode.codepoint);
		ptr += decode.size;
		dptr += enc_size;
	}

	*dptr = 0;

	u64 alloc_count = string.length*2 + 1;
	u64 string_count = (u64)(dptr - memory);
	u64 unused_count = alloc_count - string_count - 1;
	arenaDealloc(arena, unused_count * sizeof(*memory));

	StringUTF16Const result = { memory, string_count };
	return result;
}

/*
 * mbus-decode.c
 *
 *  Created on: 21.12.2019
 *      Author: Stefan
 */
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "typedefs.h"
#include "mbus.h"
#include "mbus-decode.h"

/*int bcd_to_int(int bcd)
 {
 int res = 0;
 for (int p = 1; bcd; p *= 10)
 {
 res += (bcd & 0xf) * p;
 bcd >>= 4;
 }
 return res;
 }*/

// Decode BCD data
long long mbus_data_bcd_decode(unsigned char *bcd_data, uint8_t bcd_data_size)
{
	long long val = 0;
	int16_t i;

	if (bcd_data)
	{
		for (i = bcd_data_size; i > 0; i--)
		{
			val = (val * 10);

			if (bcd_data[i - 1] >> 4 < 0xA)
			{
				val += ((bcd_data[i - 1] >> 4) & 0xF);
			}

			val = (val * 10) + (bcd_data[i - 1] & 0xF);
		}

		// hex code Fh in the MSD position signals a negative BCD number
		if (bcd_data[bcd_data_size - 1] >> 4 == 0xF)
		{
			val *= -1;
		}

		return val;
	}

	return -1;
}

// Decode INTEGER data
int mbus_data_int_decode(unsigned char *int_data, uint8_t int_data_size, uint32_t *value, uint8_t* neg)
{
	int16_t i;
	*value = 0;

	if (!int_data || (int_data_size < 1))
	{
		return -1;
	}

	*neg = int_data[int_data_size - 1] & 0x80;

	for (i = int_data_size; i > 0; i--)
	{
		if (*neg)
		{
			*value = (*value << 8) + (int_data[i - 1] ^ 0xFF);
		}
		else
		{
			*value = (*value << 8) + int_data[i - 1];
		}
	}

	if (*neg)
	{
		*value = *value /* * -1*/ - 1;
	}

	return 0;
}

int mbus_data_long_decode(unsigned char *int_data, uint8_t int_data_size, long *value)
{
	int16_t i;
	uint8_t neg;
	*value = 0;

	if (!int_data || (int_data_size < 1))
	{
		return -1;
	}

	neg = int_data[int_data_size - 1] & 0x80;

	for (i = int_data_size; i > 0; i--)
	{
		if (neg)
		{
			*value = (*value << 8) + (int_data[i - 1] ^ 0xFF);
		}
		else
		{
			*value = (*value << 8) + int_data[i - 1];
		}
	}

	if (neg)
	{
		*value = *value * -1 - 1;
	}

	return 0;
}

int mbus_data_long_long_decode(unsigned char *int_data, uint8_t int_data_size, uint32_t *value)
{
	int16_t i;
	uint8_t neg;
	*value = 0;

	if (!int_data || (int_data_size < 1))
	{
		return -1;
	}

	neg = int_data[int_data_size - 1] & 0x80;

	for (i = int_data_size; i > 0; i--)
	{
		if (neg)
		{
			*value = (*value << 8) + (int_data[i - 1] ^ 0xFF);
		}
		else
		{
			*value = (*value << 8) + int_data[i - 1];
		}
	}

	if (neg)
	{
		*value = *value * -1 - 1;
	}

	return 0;
}

// Decode float data
// see also http://en.wikipedia.org/wiki/Single-precision_floating-point_format
float mbus_data_float_decode(unsigned char *float_data)
{
#ifdef _HAS_NON_IEEE754_FLOAT
	float val = 0.0f;
	long temp = 0, fraction;
	int sign,exponent;
	uint8_t i;

	if (float_data)
	{
		for (i = 4; i > 0; i--)
		{
			temp = (temp << 8) + float_data[i-1];
		}

		// first bit = sign bit
		sign = (temp >> 31) ? -1 : 1;

		// decode 8 bit exponent
		exponent = ((temp & 0x7F800000) >> 23) - 127;

		// decode explicit 23 bit fraction
		fraction = temp & 0x007FFFFF;

		if ((exponent != -127) &&
				(exponent != 128))
		{
			// normalized value, add bit 24
			fraction |= 0x800000;
		}

		// calculate float value
		val = (float) sign * fraction * pow(2.0f, -23.0f + exponent);

		return val;
	}
#else
	if (float_data)
	{
		union
		{
			uint32_t u32;
			float f;
		} data;
		memcpy(&(data.u32), float_data, sizeof(uint32_t));
		return data.f;
	}
#endif

	return -1.0f;
}

// Decode string data.
void mbus_data_str_decode(unsigned char *dst, const unsigned char *src, uint8_t len)
{
	uint8_t i;

	i = 0;

	if (src && dst)
	{
		dst[len] = '\0';
		while (len > 0)
		{
			dst[i++] = src[--len];
		}
	}
}

// Decode binary data.
void mbus_data_bin_decode(unsigned char *dst, const unsigned char *src, uint8_t len, uint8_t max_len)
{
	uint8_t i, pos;

	i = 0;
	pos = 0;

	if (src && dst)
	{
		while ((i < len) && ((pos + 3) < max_len))
		{
			pos += snprintf((char*)&dst[pos], max_len - pos, "%.2X ", src[i]);
			i++;
		}

		if (pos > 0)
		{
			// remove last space
			pos--;
		}

		dst[pos] = '\0';
	}
}

/// Decode time data
///
/// Usable for the following types:
///   I = 6 bytes (Date and time)
///   F = 4 bytes (Date and time)
///   G = 2 bytes (Date)
///
/// TODO:
///   J = 3 bytes (Time)
///
//------------------------------------------------------------------------------
void mbus_data_tm_decode(struct tm *t, unsigned char *t_data, uint8_t t_data_size)
{
	if (t == 0)
	{
		return;
	}

	t->tm_sec = 0;
	t->tm_min = 0;
	t->tm_hour = 0;
	t->tm_mday = 0;
	t->tm_mon = 0;
	t->tm_year = 0;
	t->tm_wday = 0;
	t->tm_yday = 0;
	t->tm_isdst = 0;

	if (t_data)
	{
		if (t_data_size == 6)                // Type I = Compound CP48: Date and Time
		{
			if ((t_data[1] & 0x80) == 0)     // Time valid ?
			{
				t->tm_sec = t_data[0] & 0x3F;
				t->tm_min = t_data[1] & 0x3F;
				t->tm_hour = t_data[2] & 0x1F;
				t->tm_mday = t_data[3] & 0x1F;
				t->tm_mon = (t_data[4] & 0x0F) - 1;
				t->tm_year = 100 + (((t_data[3] & 0xE0) >> 5) | ((t_data[4] & 0xF0) >> 1));
				t->tm_isdst = (t_data[0] & 0x40) ? 1 : 0;  // day saving time
			}
		}
		else if (t_data_size == 4)           // Type F = Compound CP32: Date and Time
		{
			if ((t_data[0] & 0x80) == 0)     // Time valid ?
			{
				t->tm_min = t_data[0] & 0x3F;
				t->tm_hour = t_data[1] & 0x1F;
				t->tm_mday = t_data[2] & 0x1F;
				t->tm_mon = (t_data[3] & 0x0F) - 1;
				t->tm_year = 100 + (((t_data[2] & 0xE0) >> 5) | ((t_data[3] & 0xF0) >> 1));
				t->tm_isdst = (t_data[1] & 0x80) ? 1 : 0;  // day saving time
			}
		}
		else if (t_data_size == 2)           // Type G: Compound CP16: Date
		{
			t->tm_mday = t_data[0] & 0x1F;
			t->tm_mon = (t_data[1] & 0x0F) - 1;
			t->tm_year = 100 + (((t_data[0] & 0xE0) >> 5) | ((t_data[1] & 0xF0) >> 1));
		}
	}
}


// Generate manufacturer code from 2-byte encoded data
void mbus_decode_manufacturer(unsigned char* m_str, unsigned char byte1, unsigned char byte2)
{
	uint8_t neg = 0;
	uint32_t m_id;

	m_str[0] = byte1;
	m_str[1] = byte2;

	mbus_data_int_decode(m_str, 2, &m_id, &neg);

	m_str[0] = (char) (((m_id >> 10) & 0x001F) + 64);
	m_str[1] = (char) (((m_id >> 5) & 0x001F) + 64);
	m_str[2] = (char) (((m_id) & 0x001F) + 64);
	m_str[3] = 0;
}

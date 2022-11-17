/*
 * mbus-decode.h
 *
 *  Created on: 21.12.2019
 *      Author: Stefan
 */

#ifndef MBUS_DECODE_H_
#define MBUS_DECODE_H_

void mbus_decode_manufacturer(unsigned char* m_str, unsigned char byte1, unsigned char byte2);
long long mbus_data_bcd_decode(unsigned char *bcd_data, uint8_t bcd_data_size);
int mbus_data_int_decode(unsigned char *int_data, uint8_t int_data_size, uint32_t *value, uint8_t* neg);
int mbus_data_long_decode(unsigned char *int_data, uint8_t int_data_size, long *value);
int mbus_data_long_long_decode(unsigned char *int_data, uint8_t int_data_size, uint32_t* value);
float mbus_data_float_decode(unsigned char *float_data);
void mbus_data_tm_decode(struct tm *t, unsigned char *t_data, uint8_t t_data_size);
void mbus_data_str_decode(unsigned char *dst, const unsigned char *src, uint8_t len);
void mbus_data_bin_decode(unsigned char *dst, const unsigned char *src, uint8_t len, uint8_t max_len);

#endif /* MBUS_DECODE_H_ */

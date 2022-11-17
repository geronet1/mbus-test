#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "typedefs.h"
#include "mbus.h"
#include "verbose.h"
#include "mbus-decode.h"

int mbus_parse_telegram(unsigned char* tg, uint8_t size, allmess_zaehler* z)
{
	int i, length;
	unsigned char sum = 0;

	memset(z, 0, sizeof(*z));

	verbose(2, "Data:\n{");
	for (i = 0; i < size; i++)
	{
		verbose(2, "0x%02X, ", tg[i]);
	}
	verbose(2, "}\n");

	// Anfang prüfen
	if (tg[0] != MBUS_FRAME_LONG_START || tg[3] != MBUS_FRAME_LONG_START)
		return MBUS_RECV_RESULT_INVALID;

	if (tg[1] == tg[2])
		length = tg[2];
	else
		return MBUS_RECV_RESULT_INVALID;

	// Länge prüfen
	if (length + 6 != size)
	{
		fprintf(stderr, "Error: wrong length\n");
		return MBUS_RECV_RESULT_ERROR;
	}

	verbose(1, "Length:\t\t%d\n", length);

	// Checksumme prüfen
	for (i = 4; i < length + 4; i++)
		sum += tg[i];
	
	if ((sum & 0xFF) != tg[length + 4])
	{
		fprintf(stderr, "Error: wrong checksum. sum=0x%02X, received=0x%02X\n", sum & 0xFF, tg[length + 4]);
		return MBUS_RECV_RESULT_ERROR;
	}
	verbose(1, "Checksum OK\n");

	// Ende prüfen
	if (tg[length + 5] != MBUS_FRAME_STOP)
	{
		fprintf(stderr, "Error: wrong last byte\n");
		return MBUS_RECV_RESULT_INVALID;
	}

	verbose(1, "C-Field:\t\t0x%02X\n", tg[4]);
	verbose(1, "Primary Adress:\t\t0x%02X\n", tg[5]);
	z->slave_primary_address = tg[5];
	verbose(1, "Ci-Field:\t\t0x%02X\n", tg[6]);

	if (tg[6] != MBUS_CONTROL_INFO_RESP_VARIABLE)
	{
		fprintf(stderr, "Error: No variable response\n");
		return MBUS_RECV_RESULT_INVALID;
	}
	else
	{
		verbose(1, "variable Antwort, fixed header 12 Bytes\n");

		z->customer_number = mbus_data_bcd_decode(&(tg[7]), 4);
		mbus_decode_manufacturer(z->manufacturer, tg[11], tg[12]);
		z->generation = tg[13];
		z->medium = tg[14];
		z->reading_counter = tg[15];
		z->error_code = tg[16];
		z->signature = tg[18] << 8 | tg[17];

		unsigned char* data = &(tg[19]);
		return mbus_parse_variable_response(data, length - 3 - 12, z);
	}

	return 0;
}

int mbus_parse_variable_response(uint8_t* tg, uint8_t size, allmess_zaehler* z)
{
	int i = 0, j;

	while (i < size)
	{
		mbus_data_record record =
		{ 0 };
		record.dif = tg[i];
		verbose(2, "\nDIF %02X\n", record.dif);

		// Skip filler dif=2F
		if (record.dif == MBUS_DIB_DIF_IDLE_FILLER)
		{
			i++;
			verbose(2, "filler DIF 0x%02X skipped\n", record.dif);
			continue;
		}

		// parse DIF
		if (record.dif == MBUS_DIB_DIF_MANUFACTURER_SPECIFIC || record.dif == MBUS_DIB_DIF_MORE_RECORDS_FOLLOW)
		{
			verbose(2, "MANUFACTURER_SPECIFIC:\n");
			i++;
			while (i < size)
			{
				verbose(2, "%02X ", tg[i++]);
			}
			verbose(2, "\n");
			return 0;
		}

		record.storage_number = (record.dif & MBUS_DATA_RECORD_DIF_MASK_STORAGE_NO) >> 6;
		record.function_field = (record.dif & MBUS_DATA_RECORD_DIF_MASK_FUNCTION) >> 4;
		uint8_t data_length = mbus_dif_datalength_lookup(record.dif);
		record.value_size = data_length;
		record.data_field = record.dif & MBUS_DATA_RECORD_DIF_MASK_DATA;

		// parse DIFE
		while ((i < size) && (tg[i] & MBUS_DIB_DIF_EXTENSION_BIT))
		{
			record.dife[record.ndife++] = tg[++i];
			verbose(2, "DIFE %02X\n", record.dife[record.ndife - 1]);

			if (record.ndife >= 10)
			{
				fprintf(stderr, "Too many DIFE.\n");
				return MBUS_RECV_RESULT_ERROR;
			}
		}

		// parse VIF
		record.vif = tg[++i];
		verbose(2, "VIF %02X\n", record.vif);
		record.unit = record.vif & MBUS_DIB_VIF_WITHOUT_EXTENSION;

		// parse VIFE
		while ((i < size) && (tg[i] & MBUS_DIB_VIF_EXTENSION_BIT))
		{
			record.vife[record.nvife++] = tg[++i];
			verbose(2, "VIFE %02X\n", record.vife[record.nvife - 1]);

			if (record.nvife >= 10)
			{
				fprintf(stderr, "Too many VIFE.\n");
				return MBUS_RECV_RESULT_ERROR;
			}
		}

		verbose(2, "\tfunction_field:\t%02X\n", record.function_field);
		verbose(2, "\tdata_length:\t%d\n", data_length);
		verbose(2, "\tdata_field:\t%02X\n", record.data_field);
		verbose(2, "\tunit:\t\t%02X\n", record.unit);

		record.value = &(tg[++i]);

		verbose(2, "\tdata bytes: ");
		for (j = 0; j < data_length; j++)
		{
			verbose(2, "%02X ", record.value[j]);
		}
		verbose(2, "\n");

		// re-calculate data length, if of variable length type
		if (data_length == MBUS_DATA_RECORD_DIF_DATA_VARIABLE)
		{
			verbose(2, "\tvariable data length %02X:\n", record.value[i]);
			if (record.value[i] <= 0xBF)
				data_length = record.value[i];
			else if (record.value[i] >= 0xC0 && record.value[i] <= 0xCF)
				data_length = (record.value[i] - 0xC0) * 2;
			else if (record.value[i] >= 0xD0 && record.value[i] <= 0xDF)
				data_length = (record.value[i] - 0xD0) * 2;
			else if (record.value[i] >= 0xE0 && record.value[i] <= 0xEF)
				data_length = record.value[i] - 0xE0;
			else if (record.value[i] >= 0xF0 && record.value[i] <= 0xFA)
				data_length = record.value[i] - 0xF0;

			data_length += 1;	// add LVAR byte
		}

		mbus_parse_function_field(&record);
		mbus_parse_unit(&record, z);

		// jump over data for next dif
		i += data_length;
	}
	return 0;
}

int mbus_parse_function_field(mbus_data_record* record)
{
	verbose(2, "\t");
	switch (record->function_field)
	{
		case MBUS_DATA_RECORD_DIF_FUNCTION_INST:
			verbose(2, "Instantaneous value\n");
			break;
		case MBUS_DATA_RECORD_DIF_FUNCTION_MAX:
			verbose(2, "Maximum value\n");
			break;
		case MBUS_DATA_RECORD_DIF_FUNCTION_MIN:
			verbose(2, "Minimum value\n");
			break;
		case MBUS_DATA_RECORD_DIF_FUNCTION_ERROR:
			verbose(2, "Value during error state\n");
			break;
	}
	return 0;
}

int mbus_parse_unit(mbus_data_record* record, allmess_zaehler* z)
{
	int n = 0;
	uint8_t unit = record->unit;
	float factor = 0.0;
	uint32_t result = 0;
	uint8_t neg = 0;

	switch (unit)
	{
		case 0x00:
		case 0x00 + 1:
		case 0x00 + 2:
		case 0x00 + 3:
		case 0x00 + 4:
		case 0x00 + 5:
		case 0x00 + 6:
		case 0x00 + 7:
			// E000 0nnn Energy 10(nnn-3) Wh
			n = (unit & 0x07) - 3;
			verbose(2, "\tEnergy (%sWh)\n", mbus_unit_prefix(n));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 3);
			verbose(2, "\tresult: %d\tfactor %f\tn: %d\tneg %d\n", result, factor, n, neg);
			z->energy = result * factor;
			//z->energy_unit = 
			break;
		case 0x08:
		case 0x08 + 1:
		case 0x08 + 2:
		case 0x08 + 3:
		case 0x08 + 4:
		case 0x08 + 5:
		case 0x08 + 6:
		case 0x08 + 7:
			// 0000 1nnn Energy 10(nnn)J (0.001kJ to 10000kJ)
			n = (unit & 0x07);
			verbose(2, "\tEnergy (%sJ)\n", mbus_unit_prefix(n));
			break;
		case 0x18:
		case 0x18 + 1:
		case 0x18 + 2:
		case 0x18 + 3:
		case 0x18 + 4:
		case 0x18 + 5:
		case 0x18 + 6:
		case 0x18 + 7:
			// E001 1nnn Mass 10(nnn-3) kg 0.001kg to 10000kg
			n = (unit & 0x07);
			verbose(2, "\tMass (%skg)\n", mbus_unit_prefix(n - 3));
			break;
		case 0x28:
		case 0x28 + 1:
		case 0x28 + 2:
		case 0x28 + 3:
		case 0x28 + 4:
		case 0x28 + 5:
		case 0x28 + 6:
		case 0x28 + 7:
			// E010 1nnn Power 10(nnn-3) W 0.001W to 10000W
			n = (unit & 0x07);
			verbose(2, "\tPower (%sW)\n", mbus_unit_prefix(n - 3));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 3);
			verbose(2, "\tresult: %d\tfactor %f\n", result, factor);
			z->power = result * (factor);
			//z->power_unit
			break;
		case 0x30:
		case 0x30 + 1:
		case 0x30 + 2:
		case 0x30 + 3:
		case 0x30 + 4:
		case 0x30 + 5:
		case 0x30 + 6:
		case 0x30 + 7:
			// E011 0nnn Power 10(nnn) J/h 0.001kJ/h to 10000kJ/h
			n = (unit & 0x07);
			verbose(2, "\tPower (%sJ/h)\n", mbus_unit_prefix(n));
			break;
		case 0x10:
		case 0x10 + 1:
		case 0x10 + 2:
		case 0x10 + 3:
		case 0x10 + 4:
		case 0x10 + 5:
		case 0x10 + 6:
		case 0x10 + 7:
			// E001 0nnn Volume 10(nnn-6) m3 0.001l to 10000l
			n = (unit & 0x07);
			verbose(2, "\tVolume (%s m^3)\n", mbus_unit_prefix(n - 6));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 6);
			verbose(2, "\tresult: %d\tfactor %f\n", result, factor);
			z->volume = result * (factor * 1000);
			//z->volume_unit = 
			break;
		case 0x38:
		case 0x38 + 1:
		case 0x38 + 2:
		case 0x38 + 3:
		case 0x38 + 4:
		case 0x38 + 5:
		case 0x38 + 6:
		case 0x38 + 7:
			// E011 1nnn Volume Flow 10(nnn-6) m3/h 0.001l/h to 10000l/
			n = (unit & 0x07);
			verbose(2, "\tVolume flow (%s m^3/h)\n", mbus_unit_prefix(n - 6));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 6);
			verbose(2, "\tresult: %d\tfactor %f\n", result, factor);
			z->flow = result * (factor * 1000);
			//z->flow_unit = 
			break;
		case 0x40:
		case 0x40 + 1:
		case 0x40 + 2:
		case 0x40 + 3:
		case 0x40 + 4:
		case 0x40 + 5:
		case 0x40 + 6:
		case 0x40 + 7:
			// E100 0nnn Volume Flow ext. 10(nnn-7) m3/min 0.0001l/min to 1000l/min
			n = (unit & 0x07);
			verbose(2, "\tVolume flow (%s m^3/min)\n", mbus_unit_prefix(n - 7));
			break;
		case 0x48:
		case 0x48 + 1:
		case 0x48 + 2:
		case 0x48 + 3:
		case 0x48 + 4:
		case 0x48 + 5:
		case 0x48 + 6:
		case 0x48 + 7:
			// E100 1nnn Volume Flow ext. 10(nnn-9) m3/s 0.001ml/s to 10000ml/
			n = (unit & 0x07);
			verbose(2, "\tVolume flow (%s m^3/s)\n", mbus_unit_prefix(n - 9));
			break;
		case 0x50:
		case 0x50 + 1:
		case 0x50 + 2:
		case 0x50 + 3:
		case 0x50 + 4:
		case 0x50 + 5:
		case 0x50 + 6:
		case 0x50 + 7:
			// E101 0nnn Mass flow 10(nnn-3) kg/h 0.001kg/h to 10000kg/
			n = (unit & 0x07);
			verbose(2, "\tMass flow (%s kg/h)\n", mbus_unit_prefix(n - 3));
			break;
		case 0x58:
		case 0x58 + 1:
		case 0x58 + 2:
		case 0x58 + 3:
			// E101 10nn Flow Temperature 10(nn-3) °C 0.001°C to 1°C
			n = (unit & 0x03);
			verbose(2, "\tFlow temperature (%sdeg C)\n", mbus_unit_prefix(n - 3));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 3);
			verbose(2, "\tresult: %d\tfactor %f\n", result, factor);
			z->supply_temp = result * (factor * 10);
			break;
		case 0x5C:
		case 0x5C + 1:
		case 0x5C + 2:
		case 0x5C + 3:
			// E101 11nn Return Temperature 10(nn-3) °C 0.001°C to 1°C
			n = (unit & 0x03);
			verbose(2, "\tReturn temperature (%sdeg C)\n", mbus_unit_prefix(n - 3));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 3);
			verbose(2, "\tresult: %d\tfactor %f\n", result, factor);
			z->return_temp = result * (factor * 10);
			break;
		case 0x68:
		case 0x68 + 1:
		case 0x68 + 2:
		case 0x68 + 3:
			// E110 10nn Pressure 10(nn-3) bar 1mbar to 1000mbar
			n = (unit & 0x03);
			verbose(2, "\tPressure (%s bar)\n", mbus_unit_prefix(n - 3));
			break;
		case 0x20:
		case 0x20 + 1:
		case 0x20 + 2:
		case 0x20 + 3:
		case 0x24:
		case 0x24 + 1:
		case 0x24 + 2:
		case 0x24 + 3:
		case 0x70:
		case 0x70 + 1:
		case 0x70 + 2:
		case 0x70 + 3:
		case 0x74:
		case 0x74 + 1:
		case 0x74 + 2:
		case 0x74 + 3:
			// E010 00nn On Time
			// nn = 00 seconds
			// nn = 01 minutes
			// nn = 10   hours
			// nn = 11    days
			// E010 01nn Operating Time coded like OnTime
			// E111 00nn Averaging Duration coded like OnTime
			// E111 01nn Actuality Duration coded like OnTime
			if ((unit & 0x7C) == 0x20)
				verbose(2, "\tOn time ");
			else if ((unit & 0x7C) == 0x24)
				verbose(2, "\tOperating time ");
			else if ((unit & 0x7C) == 0x70)
				verbose(2, "\tAveraging Duration ");
			else
				verbose(2, "\tActuality Duration ");

			switch (unit & 0x03)
			{
				case 0x00:
					verbose(2, "(seconds)");
					break;
				case 0x01:
					verbose(2, "(minutes)");
					break;
				case 0x02:
					verbose(2, "(hours)");
					break;
				case 0x03:
					verbose(2, "(days)");
					break;
			}
			verbose(2, "\n");

			result = mbus_parse_data_field(record);
			verbose(2, "\tresult: %d\n", result);
			z->operating_time = result;
			z->operating_unit = unit & 0x03;

			break;
		case 0x6C:
		case 0x6C + 1:
			// E110 110n Time Point
			// n = 0        date
			// n = 1 time & date
			// data type G
			// data type F
			if (unit & 0x1)
				verbose(2, "\tTime Point (time & date)\n");
			else
				verbose(2, "\tTime Point (date)\n");

			mbus_data_tm_decode(&(z->date), record->value, record->value_size);
			break;
		case 0x60:
		case 0x60 + 1:
		case 0x60 + 2:
		case 0x60 + 3:
			// E110 00nn    Temperature Difference   10(nn-3)K   (mK to  K)
			n = (unit & 0x03);
			verbose(2, "\tTemperature Difference (%s deg C)\n", mbus_unit_prefix(n - 3));
			result = mbus_parse_data_field(record);
			factor = pow(10.0, n - 3);
			verbose(2, "\tresult: %d\tfactor %f\n", result, factor);
			z->temp_difference = result * (factor * 100);

			break;
		case 0x64:
		case 0x64 + 1:
		case 0x64 + 2:
		case 0x64 + 3:
			// E110 01nn External Temperature 10(nn-3) °C 0.001°C to 1°C
			n = (unit & 0x03);
			verbose(2, "\tExternal temperature (%s deg C)\n", mbus_unit_prefix(n - 3));
			break;
		case 0x6E:
			// E110 1110 Units for H.C.A. dimensionless
			verbose(2, "\tUnits for H.C.A.\n");
			break;
		case 0x6F:
			// E110 1111 Reserved
			verbose(2, "\tReserved\n");
			break;
		case 0x7C:
			// Custom VIF in the following string: never reached...
			verbose(2, "\tCustom VIF\n");
			break;
		case 0x78:
			// Fabrication No
			verbose(2, "\tFabrication number\n");
			result = mbus_parse_data_field(record);
			verbose(2, "\tresult: %d\n", result);
			z->fabrication_number = result;
			break;
		case 0x7A:
			// Bus Address
			verbose(2, "\tBus Address\n");
			break;
		case 0x7D:
			// VIF special char
			verbose(2, "\tVIF special char\n");
			if (record->nvife == 1)
			{
				if (record->vife[0] == MBUS_VARIABLE_VIFE_FIRMWARE_VERSION)
					z->firmware_version = record->value[0];

				if (record->vife[0] == MBUS_VARIABLE_VIFE_SOFTWARE_VERSION)
					z->software_version = record->value[0];
			}
			break;
		case 0x7F:
		case 0xFF:
			// Manufacturer specific: 7Fh / FF
			verbose(2, "\tManufacturer specific\n");
			break;
		default:
			verbose(2, "\tUnknown unit=0x%02X\n", unit);
	}
	return 0;
}

/*
 // variable length type data
 if (unit == MBUS_DATA_RECORD_DIF_DATA_VARIABLE)
 {
 if (data[i] <= 0xBF)
 {
 // ASCII string with LVAR characters
 char string[192];
 int size = data[i];
 i++;
 for (j = 0; j < size; j++)
 string[j] = data[i++];

 string[j] = '\0';
 printf("string: %s", string)
 }
 else if (data[i] >= 0xC0 && data[i] <= 0xCF)
 {
 // positive BCD number with (LVAR - C0h) • 2 digits
 int size = (data[i] - 0xC0) * 2;

 }
 else if (data[i] >= 0xD0 && data[i] <= 0xDF)
 {
 // negative BCD number with (LVAR - D0h) • 2 digits
 int size = (data[i] - 0xD0) * 2;

 }
 else if (data[i] >= 0xE0 && data[i] <= 0xEF)
 {
 // binary number with (LVAR - E0h) bytes
 int size = data[i] - 0xE0;

 }
 else if (data[i] >= 0xF0 && data[i] <= 0xFA)
 {
 // floating point number with (LVAR - F0h) bytes
 int size = data[i] - 0xF0;

 }
 return;
 }
 */

// Decode data
//
// Data format (for record->data array)
//
// Length in Bit   Code    Meaning           Code      Meaning
//      0          0000    No data           1000      Selection for Readout
//      8          0001     8 Bit Integer    1001      2 digit BCD
//     16          0010    16 Bit Integer    1010      4 digit BCD
//     24          0011    24 Bit Integer    1011      6 digit BCD
//     32          0100    32 Bit Integer    1100      8 digit BCD
//   32 / N        0101    32 Bit Real       1101      variable length
//     48          0110    48 Bit Integer    1110      12 digit BCD
//     64          0111    64 Bit Integer    1111      Special Functions
uint32_t mbus_parse_data_field(mbus_data_record* record)
{
	uint32_t result = 0;

	if (record->function_field == MBUS_DATA_RECORD_DIF_FUNCTION_ERROR)
		return 0;

	verbose(2, "\t");
	switch (record->data_field)
	{
		case 0x00: // no data
			verbose(2, "%s: DIF 0x%02X with no data\n", __func__, record->dif);
			return 0;
			break;
		case 0x01: // 1 byte integer (8 bit)
			verbose(2, "%s: DIF 0x%02X with 1 byte int\n", __func__, record->dif);
			mbus_data_int_decode(record->value, 1, &result, &(record->neg));
			return result;
			break;
		case 0x02: // 2 byte (16 bit)
			// E110 1100  Time Point (date)
			verbose(2, "%s: DIF 0x%02X with 2 byte Time Point (date)\n", __func__, record->dif);
			mbus_data_int_decode(record->value, 2, &result, &(record->neg));
			return result;
			break;
		case 0x03: // 3 byte integer (24 bit)
			verbose(2, "%s: DIF 0x%02X was decoded using 3 byte integer\n", __func__, record->dif);
			mbus_data_int_decode(record->value, 3, &result, &(record->neg));
			return result;
			break;
		case 0x04: // 4 byte (32 bit)
			// E110 1101  Time Point (date/time)
			// E011 0000  Start (date/time) of tariff
			// E111 0000  Date and time of battery change
			verbose(2, "%s: DIF 0x%02X was decoded using 4 byte integer\n", __func__, record->dif);

			mbus_data_int_decode(record->value, 4, &result, &(record->neg));
			return result;
			break;
		case 0x05: // 4 Byte Real (32 bit)
			//float_val = mbus_data_float_decode(record->data);
			verbose(2, "%s: DIF 0x%02X was decoded using 4 byte Real\n", __func__, record->dif);
			break;
		case 0x06: // 6 byte (48 bit)
			// E110 1101  Time Point (date/time)
			// E011 0000  Start (date/time) of tariff
			// E111 0000  Date and time of battery change
			verbose(2, "%s: DIF 0x%02X was decoded using 6 byte integer\n", __func__, record->dif);
			mbus_data_long_long_decode(record->value, 6, &result);
			return result;
		case 0x07: // 8 byte integer (64 bit)
			verbose(2, "%s: DIF 0x%02X was decoded using 8 byte integer\n", __func__, record->dif);
			mbus_data_long_long_decode(record->value, 8, &result);
			return result;
			//case 0x08:
		case 0x09: // 2 digit BCD (8 bit)
			verbose(2, "%s: DIF 0x%02X was decoded using 2 digit BCD\n", __func__, record->dif);
			return mbus_data_bcd_decode(record->value, 1);
		case 0x0A: // 4 digit BCD (16 bit)
			verbose(2, "%s: DIF 0x%02X was decoded using 4 digit BCD\n", __func__, record->dif);
			return mbus_data_bcd_decode(record->value, 2);
		case 0x0B: // 6 digit BCD (24 bit)
			verbose(2, "%s: DIF 0x%02X was decoded using 6 digit BCD\n", __func__, record->dif);
			return mbus_data_bcd_decode(record->value, 3);
		case 0x0C: // 8 digit BCD (32 bit)
			verbose(2, "%s: DIF 0x%02X was decoded using 8 digit BCD\n", __func__, record->dif);
			return mbus_data_bcd_decode(record->value, 4);
		case 0x0E: // 12 digit BCD (48 bit)
			// TODO: 12 digit passt nicht in uint32_t
			verbose(2, "%s: DIF 0x%02X was decoded using 12 digit BCD\n", __func__, record->dif);
			return mbus_data_bcd_decode(record->value, 6);
		case 0x0F: // special functions
			break;
		case 0x0D: // variable length
		default:
			verbose(2, "%s: Unknown DIF (0x%02X)", __func__, record->dif);
	}

	return 0;
}

// Look up the data length from a DIF field in the data record.
// See the table on page 41 the M-BUS specification.
unsigned char mbus_dif_datalength_lookup(unsigned char dif)
{
	switch (dif & MBUS_DATA_RECORD_DIF_MASK_DATA)
	{
		case 0x0:
			return 0;
		case 0x1:
			return 1;
		case 0x2:
			return 2;
		case 0x3:
			return 3;
		case 0x4:
			return 4;
		case 0x5:
			return 4;
		case 0x6:
			return 6;
		case 0x7:
			return 8;
		case 0x8:
			return 0;
		case 0x9:
			return 1;
		case 0xA:
			return 2;
		case 0xB:
			return 3;
		case 0xC:
			return 4;
		case 0xD:
// variable data length,
// data length stored in data field
			return MBUS_DATA_RECORD_DIF_DATA_VARIABLE;
		case 0xE:
			return 6;
		case 0xF:
			return 8;

		default: // never reached
			return 0x00;

	}
}

// Lookup the unit description from a VIF field in a data record
const char* mbus_unit_prefix(int exp)
{
	static char buff[256];
//	float f;

	switch (exp)
	{
		case 0:
			buff[0] = 0;
//			f = 0;
			break;
		case -3:
			snprintf(buff, sizeof(buff), "m");
//			f = 0.001;
			break;
		case -6:
			snprintf(buff, sizeof(buff), "my");
//			f = 0.000001;
			break;
		case 1:
			snprintf(buff, sizeof(buff), "10 ");
//			f = 10;
			break;
		case 2:
			snprintf(buff, sizeof(buff), "100 ");
//			f = 100;
			break;
		case 3:
			snprintf(buff, sizeof(buff), "k");
//			f = 1000;
			break;
		case 4:
			snprintf(buff, sizeof(buff), "10 k");
//			f = 10000;
			break;
		case 5:
			snprintf(buff, sizeof(buff), "100 k");
//			f = 1000000;
			break;
		case 6:
			snprintf(buff, sizeof(buff), "M");
//			f = 1000;
			break;

		case 9:
			snprintf(buff, sizeof(buff), "G");
//			f = 1000000000;
			break;

		default:
			snprintf(buff, sizeof(buff), "1e%d ", exp);
	}

	return buff;
}

void mbus_print(allmess_zaehler* z, int human, int unit)
{
	// Trennzeichen einstellbar
	char c = ';';
	if (!human)
	{
		printf("primary_address:%d", z->slave_primary_address);
		printf("%ccustomer_number:%d", c, z->customer_number);
		printf("%cmanufacturer:%s", c, z->manufacturer);
		printf("%cgeneration:%d", c, z->generation);
		printf("%cmedium:%d", c, z->medium);
		printf("%creading_counter:%d", c, z->reading_counter);
		printf("%cerror_code:%d", c, z->error_code);
		printf("%csignature:%d", c, z->signature);
		printf("%cfabrication_number:%d", c, z->fabrication_number);
		printf("%cenergy:%d", c, z->energy);
		if (unit) printf(" kWh");
		printf("%cvolume:%d", c, z->volume);
		if (unit) printf(" l");
		printf("%cpower:%d", c, z->power);
		if (unit) printf(" W");
		printf("%cflow:%d", c, z->flow);
		if (unit) printf(" l/h");
		printf("%csupply_temp:%0.1f", c, (float) z->supply_temp / 10.0);
		if (unit) printf(" °C");
		printf("%creturn_temp:%0.1f", c, (float) z->return_temp / 10.0);
		if (unit) printf(" °C");
		printf("%ctemp_difference:%0.2f", c, (float) z->temp_difference / 100.0);
		if (unit) printf(" °C");
		printf("%cdate:%02d.%02d.%d", c, z->date.tm_mday, z->date.tm_mon + 1, z->date.tm_year + 1900);
		printf("%ctime:%02d.%02d.%02d", c, z->date.tm_hour, z->date.tm_min, z->date.tm_sec);
		printf("%coperating_time:%d", c, z->operating_time);
		if (unit)
			switch (z->operating_unit)
			{
				case 0x00:
					printf(" s");
					break;
				case 0x01:
					printf(" m");
					break;
				case 0x02:
					printf(" h");
					break;
				case 0x03:
					printf(" d");
					break;
			}
		printf("%cfirmware_version:%d", c, z->firmware_version);
		printf("%csoftware_version:%d", c, z->software_version);
		printf("%calarm_code:%d", c, z->alarm_code);
		printf("\n");
		return;
	}

	printf("-------------------------------------------------\n");
	printf("primary_address:\t0x%02X\n", z->slave_primary_address);
	printf("customer_number:\t%d\n", z->customer_number);
	printf("manufacturer:\t\t%s\n", z->manufacturer);
	printf("generation:\t\t%d\n", z->generation);

	printf("medium:\t\t\t%d\t", z->medium);
	if (z->medium == 0x04)
		printf(" = Wärme");
	printf("\n");

	printf("reading_counter:\t%d\n", z->reading_counter);

	printf("error_code:\t\t%d\n", z->error_code);
	if (z->error_code & _BV(2))
		printf("\tBit 2:\t Batterie leer\n");
	if (z->error_code & _BV(3))
		printf("\tBit 3:\t Permanenter Fehler\n");
	if (z->error_code & _BV(4))
		printf("\tBit 4:\t Temporärer Fehler\n");

	printf("signature:\t\t%d\n", z->signature);
	printf("fabrication_number:\t%d\n", z->fabrication_number);
	printf("energy:\t\t\t%d kWh\n", z->energy);
	printf("volume:\t\t\t%d l\n", z->volume);
	printf("power:\t\t\t%d W\n", z->power);
	printf("flow:\t\t\t%d l/h\n", z->flow);
	printf("supply_temp:\t\t%0.1f °C\n", (float) z->supply_temp / 10.0);
	printf("return_temp:\t\t%0.1f °C\n", (float) z->return_temp / 10.0);
	printf("temp_difference:\t%0.2f K\n", (float) z->temp_difference / 100.0);

	printf("date:\t\t\t%02d.%02d.%d\n", z->date.tm_mday, z->date.tm_mon + 1, z->date.tm_year + 1900);
	printf("time:\t\t\t%02d:%02d:%02d\n", z->date.tm_hour, z->date.tm_min, z->date.tm_sec);

	printf("operating_time:\t\t%d ", z->operating_time);
	switch (z->operating_unit)
	{
		case 0x00:
			printf("s");
			break;
		case 0x01:
			printf("m");
			break;
		case 0x02:
			printf("h");
			break;
		case 0x03:
			printf("d");
			break;
	}
	printf("\n");

	printf("firmware_version:\t%d\n", z->firmware_version);
	printf("software_version:\t%d\n", z->software_version);
	printf("alarm_code:\t\t%d\n", z->alarm_code);

	printf("-------------------------------------------------\n");

}

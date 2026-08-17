#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "SusamuneMp3Validate.h"

#define MP3_SCAN_MAX (64u * 1024u)

static uint32_t ReadBE32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool ParseFrame(const uint8_t *p, uint32_t available,
	uint32_t *frameSize, uint32_t *format)
{
	static const uint16_t bitrateV1[3][16] = {
		{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
		{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}
	};
	static const uint16_t bitrateV2[3][16] = {
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}
	};
	static const uint16_t rates[3] = {44100, 48000, 32000};
	uint32_t word;
	uint32_t version;
	uint32_t layerBits;
	uint32_t layer;
	uint32_t bitrateIndex;
	uint32_t rateIndex;
	uint32_t rate;
	uint32_t bitrate;
	uint32_t padding;
	uint32_t size;

	if (available < 4)
		return false;
	word = ReadBE32(p);
	if ((word & 0xffe00000u) != 0xffe00000u)
		return false;
	version = (word >> 19) & 3u;
	layerBits = (word >> 17) & 3u;
	bitrateIndex = (word >> 12) & 15u;
	rateIndex = (word >> 10) & 3u;
	padding = (word >> 9) & 1u;
	if (version == 1u || layerBits == 0u || bitrateIndex == 0u ||
	    bitrateIndex == 15u || rateIndex == 3u)
		return false;

	layer = 4u - layerBits;
	rate = rates[rateIndex];
	if (version == 2u)
		rate /= 2u;
	else if (version == 0u)
		rate /= 4u;
	bitrate = (version == 3u ? bitrateV1 : bitrateV2)[layer - 1u][bitrateIndex];
	if (layer == 1u)
		size = ((12u * bitrate * 1000u / rate) + padding) * 4u;
	else if (layer == 3u && version != 3u)
		size = 72u * bitrate * 1000u / rate + padding;
	else
		size = 144u * bitrate * 1000u / rate + padding;
	if (size < 4u || size > available)
		return false;
	if (frameSize != NULL)
		*frameSize = size;
	if (format != NULL)
		*format = (version << 4) | (layer << 2) | rateIndex;
	return true;
}

static uint32_t AudioStart(const uint8_t *data, uint32_t size)
{
	uint32_t start = 0;
	uint32_t tagSize;

	if (size < 10u || memcmp(data, "ID3", 3) != 0)
		return 0;
	if ((data[6] | data[7] | data[8] | data[9]) & 0x80u)
		return size;
	tagSize = ((uint32_t)data[6] << 21) | ((uint32_t)data[7] << 14) |
	          ((uint32_t)data[8] << 7) | (uint32_t)data[9];
	start = 10u + tagSize;
	if ((data[5] & 0x10u) != 0)
		start += 10u;
	return start < size ? start : size;
}

bool SusamuneMp3Validate(const uint8_t *data, uint32_t size)
{
	uint32_t start;
	uint32_t scanEnd;
	uint32_t offset;

	if (data == NULL || size < 8u)
		return false;
	start = AudioStart(data, size);
	if (start >= size)
		return false;
	scanEnd = size;
	if (scanEnd - start > MP3_SCAN_MAX)
		scanEnd = start + MP3_SCAN_MAX;
	for (offset = start; offset + 8u <= scanEnd; offset++)
	{
		uint32_t firstSize;
		uint32_t firstFormat;
		uint32_t secondSize;
		uint32_t secondFormat;

		if (!ParseFrame(data + offset, size - offset, &firstSize, &firstFormat))
			continue;
		if (offset + firstSize >= size)
			continue;
		if (!ParseFrame(data + offset + firstSize,
			size - offset - firstSize, &secondSize, &secondFormat))
			continue;
		(void)secondSize;
		if (firstFormat == secondFormat)
			return true;
	}
	return false;
}

#ifdef SUSAMUNE_MP3_VALIDATE_SELFTEST
#include <stdio.h>
#include <stdlib.h>

static uint32_t FirstFrame(const uint8_t *data, uint32_t size,
	uint32_t *frameSize)
{
	uint32_t offset;
	uint32_t start = AudioStart(data, size);

	for (offset = start; offset + 4u <= size; offset++)
	{
		if (ParseFrame(data + offset, size - offset, frameSize, NULL))
			return offset;
	}
	return size;
}

int main(int argc, char **argv)
{
	FILE *file;
	long fileSize;
	uint8_t *data;
	uint8_t hostile[256];
	uint8_t badTag[16] = {'I', 'D', '3', 4, 0, 0, 0x7f, 0x7f, 0x7f, 0x7f};
	uint32_t firstSize = 0;
	uint32_t first;
	uint32_t second;
	bool ok;

	if (argc != 2)
		return 2;
	file = fopen(argv[1], "rb");
	if (file == NULL)
		return 3;
	fseek(file, 0, SEEK_END);
	fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (fileSize <= 0 || fileSize > 16L * 1024L * 1024L)
	{
		fclose(file);
		return 4;
	}
	data = (uint8_t *)malloc((size_t)fileSize);
	if (data == NULL || fread(data, 1, (size_t)fileSize, file) != (size_t)fileSize)
	{
		fclose(file);
		free(data);
		return 5;
	}
	fclose(file);

	ok = SusamuneMp3Validate(data, (uint32_t)fileSize);
	first = FirstFrame(data, (uint32_t)fileSize, &firstSize);
	second = first + firstSize;
	if (!ok || first >= (uint32_t)fileSize || second >= (uint32_t)fileSize)
	{
		free(data);
		return 6;
	}
	if (SusamuneMp3Validate(data, second + 3u))
	{
		free(data);
		return 7;
	}
	memset(hostile, 0xff, sizeof(hostile));
	if (SusamuneMp3Validate(hostile, sizeof(hostile)) ||
	    SusamuneMp3Validate(badTag, sizeof(badTag)))
	{
		free(data);
		return 8;
	}
	printf("valid size=%lu first=%lu frame=%lu second=%lu; "
		"truncated/hostile/oversized-ID3 rejected\n",
		(unsigned long)fileSize, (unsigned long)first,
		(unsigned long)firstSize, (unsigned long)second);
	free(data);
	return 0;
}
#endif

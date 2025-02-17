#include "pscx_gte_divider.h"

// Unsigned Newtown-Raphson look up table.
static const uint8_t UNSIGNED_NEWTOWN_RAPHSON_TABLE[] = {
	0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1,
	0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3,
	0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5,
	0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8,
	0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb,
	0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
	0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4,
	0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a,
	0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90,
	0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86,
	0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d,
	0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
	0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c,
	0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
	0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d,
	0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55,
	0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e,
	0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
	0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41,
	0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b,
	0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35,
	0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f,
	0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a,
	0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
	0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f,
	0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
	0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15,
	0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
	0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c,
	0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
	0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04,
	0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
	0x00,
};

template<typename T>
uint32_t calculateLeadingZeros(T value)
{
	T inputValue = value;

	// Keep shifting x by one until leftmost bit 
	// does not become 1.
	uint32_t totalBits = sizeof(inputValue) * 8;
	if (inputValue == 0x0)
	{
		return totalBits;
	}

	uint32_t numLeadingZeros = 0x0;
	while (!(inputValue & (1 << (totalBits - 1))))
	{
		inputValue <<= 1;
		numLeadingZeros++;
	}

	return numLeadingZeros;
}

template uint32_t calculateLeadingZeros<uint32_t>(uint32_t value);

uint32_t divide(uint16_t numerator, uint16_t divisor)
{
	uint32_t shift = calculateLeadingZeros(divisor);
	
	uint64_t numeratorShifted = ((uint64_t)numerator) << shift;
	uint16_t divisorShifted = divisor << shift;

	uint64_t reciprocal = calculateReciprocal(divisorShifted);
	uint64_t reciprocalShifted = (numeratorShifted * reciprocal + 0x8000) >> 16;

	if (reciprocalShifted <= 0x1ffff)
		return (uint32_t)reciprocalShifted;
	return 0x1ffff;
}

uint32_t calculateReciprocal(uint16_t divisor)
{
	uint16_t index = ((divisor & 0x7fff) + 0x40) >> 7;
	int32_t factor = (int32_t)UNSIGNED_NEWTOWN_RAPHSON_TABLE[index] + 0x101;
	int32_t divisorSigned = (divisor | 0x8000);

	return (factor * (0x20000 + ((divisorSigned * (-factor) + 0x80) >> 8)) + 0x80) >> 8;
}

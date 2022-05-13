#include "native_library.h"

namespace base {

	const char *int2char(int value, char* buffer)
	{
		const char *result = buffer;
		unsigned int buffer_pos = 0;

		if (value < 0) {
			buffer[buffer_pos++] = '-';
			value = -value;
		}

		int number_of_digits = 0;
		int t = value;
		do {
			++number_of_digits;
		} while (t /= 10);

		buffer_pos += number_of_digits;

		do {
			int last_digit = value % 10;
			buffer[--buffer_pos] = '0' + last_digit;
			value /= 10;
		} while (value);
		return result;
	}
} //namespace

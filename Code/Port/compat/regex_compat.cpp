/*
** regex_compat.cpp - gnu_regex.h on std::regex. See gnu_regex.h.
*/
#include "gnu_regex.h"
#include <regex>
#include <string>

static reg_syntax_t _Syntax = 0;

extern "C" reg_syntax_t re_set_syntax(reg_syntax_t syntax)
{
	reg_syntax_t old = _Syntax;
	_Syntax = syntax;
	return old;
}

extern "C" const char * re_compile_pattern(const char * pattern, size_t length, regex_t * buffer)
{
	regfree(buffer);
	try {
		buffer->impl = new std::regex(std::string(pattern, length), std::regex::extended);
	} catch (const std::regex_error &) {
		return "Invalid regular expression";
	}
	return 0;
}

extern "C" int re_match(regex_t * buffer, const char * string, int size, int pos, struct re_registers *)
{
	if (buffer == 0 || buffer->impl == 0 || pos < 0 || pos > size) {
		return -2;
	}
	std::cmatch m;
	// GNU re_match is anchored at pos and returns how many characters matched, or -1
	if (std::regex_search(string + pos, string + size, m, *(std::regex *)buffer->impl, std::regex_constants::match_continuous)) {
		return (int)m.length(0);
	}
	return -1;
}

extern "C" void regfree(regex_t * buffer)
{
	if (buffer != 0 && buffer->impl != 0) {
		delete (std::regex *)buffer->impl;
		buffer->impl = 0;
	}
}

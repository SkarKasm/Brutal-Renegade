/*
** gnu_regex.h (compat) - the part of the GNU regex API that wwlib/regexpr.cpp uses,
** implemented on std::regex (Port/compat/regex_compat.cpp). The original GNU regex source
** was not part of EA's release.
**
** Only what RegularExpressionClass needs: compile with re_compile_pattern under the syntax
** set by re_set_syntax, anchored match with re_match, regfree. The RE_* syntax bits are
** accepted for source compatibility; patterns are always POSIX-extended-like (ECMAScript
** grammar with [:class:] support), which is what regexpr.cpp asks for.
*/
#ifndef PORT_GNU_REGEX_H
#define PORT_GNU_REGEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long reg_syntax_t;

#define RE_BACKSLASH_ESCAPE_IN_LISTS	(1UL << 0)
#define RE_BK_PLUS_QM					(1UL << 1)
#define RE_CHAR_CLASSES					(1UL << 2)
#define RE_CONTEXT_INDEP_ANCHORS		(1UL << 3)
#define RE_CONTEXT_INDEP_OPS			(1UL << 4)
#define RE_CONTEXT_INVALID_OPS			(1UL << 5)
#define RE_DOT_NEWLINE					(1UL << 6)
#define RE_DOT_NOT_NULL					(1UL << 7)
#define RE_HAT_LISTS_NOT_NEWLINE		(1UL << 8)
#define RE_INTERVALS					(1UL << 9)
#define RE_LIMITED_OPS					(1UL << 10)
#define RE_NEWLINE_ALT					(1UL << 11)
#define RE_NO_BK_BRACES					(1UL << 12)
#define RE_NO_BK_PARENS					(1UL << 13)
#define RE_NO_BK_REFS					(1UL << 14)
#define RE_NO_BK_VBAR					(1UL << 15)
#define RE_NO_EMPTY_RANGES				(1UL << 16)

typedef struct re_pattern_buffer
{
	void *	impl;			/* std::regex, owned */
} regex_t;

struct re_registers;		/* never used by regexpr.cpp (always passed as 0) */

reg_syntax_t	re_set_syntax(reg_syntax_t syntax);
const char *	re_compile_pattern(const char * pattern, size_t length, regex_t * buffer);
int				re_match(regex_t * buffer, const char * string, int size, int pos, struct re_registers * regs);
void			regfree(regex_t * buffer);

#ifdef __cplusplus
}
#endif

#endif

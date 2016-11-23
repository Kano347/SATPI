/* Tokenizer.h

   Copyright (C) 2015, 2016 Marc Postema (mpostema09 -at- gmail.com)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html
*/
#ifndef BASE_TOKENIZER_H_INCLUDE
#define BASE_TOKENIZER_H_INCLUDE BASE_TOKENIZER_H_INCLUDE

#include <Log.h>

#include <pthread.h>
#include <unistd.h>
#include <sys/prctl.h>

namespace base {

	///
	template<class STRING_TYPE>
	class TokenizerTempl {
		public:
			// =======================================================================
			// -- Constructors and destructor ----------------------------------------
			// =======================================================================
			TokenizerTempl(STRING_TYPE &string, const STRING_TYPE &delimiters) :
					_begin(0),
					_end(0),
					_atEnd(false),
					_delimiters(delimiters),
					_string(string) {}

			virtual ~TokenizerTempl() {}

			// =======================================================================
			//  -- Other member functions --------------------------------------------
			// =======================================================================

			/// This function gets an character string that is the new token found in the tokenizing string
			/// @param token is the new token found in the tokenizing string
			/// @retval true we found an new token
			bool isNextToken(STRING_TYPE &token) {
				if (!_atEnd) {
					_end = _string.find_first_of(_delimiters, _begin);
					if (STRING_TYPE::npos == _end) {
					_end = _string.size();
					_atEnd = true;
					}
					token = _string.substr(_begin, _end - _begin);
					_begin = _end + 1;
					return true;
				} else {
					return false;
				}
			}

			// =======================================================================
			// -- Data members -------------------------------------------------------
			// =======================================================================

		private:
			/// Copy Constructor (Empty function)
			TokenizerTempl(const TokenizerTempl &);

			/// Assignment Operator (Empty function)
			TokenizerTempl &operator=(const TokenizerTempl &);

			typename STRING_TYPE::size_type _begin;
			typename STRING_TYPE::size_type _end;
			bool _atEnd;

			/// This specifies some standard delimiters were to look for
			const STRING_TYPE _delimiters;

			/// This is the string that can be tokenized
			STRING_TYPE _string;
	};

	typedef TokenizerTempl<std::string> StringTokenizer;

} // namespace base

#endif // BASE_TOKENIZER_H_INCLUDE

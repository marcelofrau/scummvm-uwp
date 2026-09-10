/* Copyright  (C) 2010-2020 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (encoding_utf.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <boolean.h>
#include <compat/strl.h>
#include <retro_inline.h>

#include <encodings/utf.h>

#if defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#elif defined(_XBOX)
#include <xtl.h>
#endif

/**
 * utf8_to_utf16_string_alloc:
 *
 * @return Returned pointer MUST be freed by the caller if non-NULL.
 **/
wchar_t *utf8_to_utf16_string_alloc(const char *str)
{
#ifdef _WIN32
   int _len       = 0;
#else
   size_t _len    = 0;
#endif
   wchar_t *buf   = NULL;

   if (!str || !*str)
      return NULL;

#ifdef _WIN32
   if ((_len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0)))
   {
      if (!(buf = (wchar_t*)calloc(_len, sizeof(wchar_t))))
         return NULL;

      if ((MultiByteToWideChar(CP_UTF8, 0, str, -1, buf, _len)) < 0)
      {
         free(buf);
         return NULL;
      }
   }
   else
   {
      if ((_len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0)))
      {
         if (!(buf = (wchar_t*)calloc(_len, sizeof(wchar_t))))
            return NULL;

         if ((MultiByteToWideChar(CP_ACP, 0, str, -1, buf, _len)) < 0)
         {
            free(buf);
            return NULL;
         }
      }
   }
#else
   if ((_len = mbstowcs(NULL, str, 0) + 1))
   {
      if (!(buf = (wchar_t*)calloc(_len, sizeof(wchar_t))))
         return NULL;

      if ((mbstowcs(buf, str, _len)) == (size_t)-1)
      {
         free(buf);
         return NULL;
      }
   }
#endif

   return buf;
}

/**
 * utf16_to_utf8_string_alloc:
 *
 * @return Returned pointer MUST be freed by the caller if non-NULL.
 **/
char *utf16_to_utf8_string_alloc(const wchar_t *str)
{
#ifdef _WIN32
   int _len       = 0;
#else
   size_t _len    = 0;
#endif
   char *buf      = NULL;

   if (!str || !*str)
      return NULL;

#ifdef _WIN32
   {
      UINT code_page = CP_UTF8;

      if (!(_len = WideCharToMultiByte(code_page,
            0, str, -1, NULL, 0, NULL, NULL)))
      {
         code_page   = CP_ACP;
         _len        = WideCharToMultiByte(code_page,
               0, str, -1, NULL, 0, NULL, NULL);
      }

      if (!(buf = (char*)calloc(_len, sizeof(char))))
         return NULL;

      if (WideCharToMultiByte(code_page,
            0, str, -1, buf, _len, NULL, NULL) < 0)
      {
         free(buf);
         return NULL;
      }
   }
#else
   if ((_len = wcstombs(NULL, str, 0) + 1))
   {
      if (!(buf = (char*)calloc(_len, sizeof(char))))
         return NULL;

      if (wcstombs(buf, str, _len) == (size_t)-1)
      {
         free(buf);
         return NULL;
      }
   }
#endif

   return buf;
}

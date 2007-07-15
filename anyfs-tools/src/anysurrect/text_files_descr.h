/*
 *	text_files_descr.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

char *text_ASCII_surrect();
char *text_EIGTH_BIT_surrect();
char *text_UTF8_surrect();
char *text_UTF16BE_surrect();
char *text_UTF16LE_surrect();

_declspec(dllexport) 
const char* text_ASCII_indicator="ASCII";
_declspec(dllexport) 
const char* text_EIGTH_BIT_indicator="8-bit text";
_declspec(dllexport) 
const char* text_UTF8_indicator="UTF8";
_declspec(dllexport) 
const char* text_UTF16BE_indicator="UTF16BE";
_declspec(dllexport) 
const char* text_UTF16LE_indicator="UTF16LE";

_declspec(dllexport) 
const int text_ASCII_text=1;
_declspec(dllexport) 
const int text_EIGTH_BIT_text=1;
_declspec(dllexport) 
const int text_UTF8_text=1;
_declspec(dllexport) 
const int text_UTF16BE_text=1;
_declspec(dllexport) 
const int text_UTF16LE_text=1;
